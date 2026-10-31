#include "RealOceanFoamClipmapShaders.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "RealOceanRenderBridge.h"
#include "RealOceanReleaseConfig.h"

#if !UE_BUILD_SHIPPING || REALOCEAN_RELEASE_FOAM_PATH != 0

namespace
{
    class FRealOceanFoamClipmapCS final : public FGlobalShader
    {
        DECLARE_GLOBAL_SHADER(FRealOceanFoamClipmapCS);
        SHADER_USE_PARAMETER_STRUCT(FRealOceanFoamClipmapCS, FGlobalShader);

    public:
        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousFoamTexture)
            SHADER_PARAMETER_SAMPLER(SamplerState, PreviousFoamSampler)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputFoamTexture)
            SHADER_PARAMETER(FIntPoint, ClipmapExtent)
            SHADER_PARAMETER(FVector2f, CenterDeltaMeters)
            SHADER_PARAMETER(FVector2f, CurrentCenterOffsetMeters)
            SHADER_PARAMETER(FVector2f, AdvectionVelocityMps)
            SHADER_PARAMETER(float, DeltaTimeSeconds)
            SHADER_PARAMETER(float, CoverageMeters)
            SHADER_PARAMETER(float, FoamHalfLifeSeconds)
            SHADER_PARAMETER(float, InjectionPerSecond)
            SHADER_PARAMETER(float, DiffusionPerSecond)
            SHADER_PARAMETER(int32, ResetHistory)
            SHADER_PARAMETER(float, WindSpeedMps)
            SHADER_PARAMETER(float, CombinedHsMeters)
            SHADER_PARAMETER(float, BreakingStrength)
            SHADER_PARAMETER(float, WhitecapCoverageScale)
            SHADER_PARAMETER(float, SwellDirectionDegrees)
            SHADER_PARAMETER(float, WindDirectionDegrees)
            SHADER_PARAMETER(float, SwellHsMeters)
            SHADER_PARAMETER(float, WindSeaHsMeters)
            SHADER_PARAMETER(float, SwellPeriodSeconds)
            SHADER_PARAMETER(float, WindSeaPeakPeriodSeconds)
            SHADER_PARAMETER(float, SwellDirectionalSpread)
            SHADER_PARAMETER(float, WindSeaDirectionalSpread)
            SHADER_PARAMETER(float, Steepness)
            SHADER_PARAMETER(float, WindSeaSteepnessScale)
            SHADER_PARAMETER(float, GravityMS2)
            SHADER_PARAMETER(FVector4f, AnchorPhase0123)
            SHADER_PARAMETER(FVector4f, AnchorPhase4567)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
        }
    };

    IMPLEMENT_GLOBAL_SHADER(FRealOceanFoamClipmapCS,
        "/Plugin/RealOcean/RealOceanFoamClipmap.usf", "FoamClipmapCS", SF_Compute);

    static FVector2d QuantizedCenterWorldMeters(const FRealOceanRenderState& OceanState)
    {
        // Game thread computes this in double precision from the exact render-camera position and
        // the same texel snap used by the MPC material transform. Keep view-local rounding out of it.
        return OceanState.FoamClipmapCenterWorldMeters;
    }
}

bool AddRealOceanFoamClipmapPass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FRealOceanRenderState& OceanState,
    FRealOceanFoamClipmapHistory& InOutHistory,
    bool bForceReset)
{
    if (!OceanState.bConfigured || !OceanState.bFoamClipmapEnabled || OceanState.FoamClipmapResolution <= 0)
    {
        InOutHistory.bValid = false;
        return false;
    }

    const FSceneInterface* Scene = View.Family ? View.Family->Scene : nullptr;
    const FTextureRHIRef ExternalTextureRHI = GetRealOceanFoamClipmapTexture_RenderThread(Scene);
    if (!ExternalTextureRHI.IsValid())
    {
        InOutHistory.bValid = false;
        return false;
    }

    const FIntVector ExternalSize = ExternalTextureRHI->GetSizeXYZ();
    const FIntPoint Extent(ExternalSize.X, ExternalSize.Y);
    if (Extent.X <= 0 || Extent.Y <= 0 || Extent.X != Extent.Y)
    {
        InOutHistory.bValid = false;
        return false;
    }

    const FVector2d CurrentCenterWorldMeters = QuantizedCenterWorldMeters(OceanState);
    const double CurrentTime = static_cast<double>(OceanState.OceanTimeSeconds);
    double DeltaTime = InOutHistory.bValid ? (CurrentTime - InOutHistory.LastOceanTimeSeconds) : 0.0;

    constexpr double TimeEpsilon = 1.0e-9;
    bool bReset = bForceReset || !InOutHistory.bValid || DeltaTime < -TimeEpsilon || DeltaTime > 0.25;
    FVector2d CenterMotion = FVector2d::ZeroVector;
    if (!bReset)
    {
        const double Coverage = FMath::Max(1.0, static_cast<double>(OceanState.FoamClipmapCoverageMeters));
        CenterMotion = CurrentCenterWorldMeters - InOutHistory.PreviousCenterWorldMeters;
        // If the camera teleports farther than half the field there is no useful overlap to reproject.
        bReset = FMath::Abs(CenterMotion.X) > Coverage * 0.5 || FMath::Abs(CenterMotion.Y) > Coverage * 0.5;

        // M29 quality presets cap compute cadence only while the texel-snapped clipmap center is stable.
        // If the center changes, update immediately so material UVs never point at stale world-space data.
        // A paused ocean clock keeps history intact; it either skips entirely or performs a zero-dt reprojection.
        const double UpdateRateHz = FMath::Clamp(static_cast<double>(OceanState.FoamClipmapUpdateRateHz), 10.0, 120.0);
        const double MinUpdateInterval = 1.0 / UpdateRateHz;
        const bool bCenterStable = CenterMotion.SizeSquared() <= 1.0e-12;
        const bool bOceanClockPaused = FMath::Abs(DeltaTime) <= TimeEpsilon;
        if (!bReset && bCenterStable && (bOceanClockPaused || DeltaTime < MinUpdateInterval))
        {
            return true;
        }
    }

    const float DeltaTimeSeconds = bReset ? (1.0f / 60.0f) : static_cast<float>(FMath::Clamp(DeltaTime, 0.0, 0.25));
    const FVector2d CenterDeltaD = bReset ? FVector2d::ZeroVector : (CurrentCenterWorldMeters - InOutHistory.PreviousCenterWorldMeters);

    TRefCountPtr<IPooledRenderTarget> ExternalPooled = CreateRenderTarget(ExternalTextureRHI, TEXT("RealOcean.FoamClipmap.Persistent"));
    FRDGTextureRef PersistentTexture = GraphBuilder.RegisterExternalTexture(
        ExternalPooled,
        TEXT("RealOcean.FoamClipmap.Persistent"),
        ERDGTextureFlags::None);

    FRDGTextureDesc NextDesc = FRDGTextureDesc::Create2D(
        Extent,
        PF_R16F,
        FClearValueBinding::Black,
        TexCreate_ShaderResource | TexCreate_UAV);
    FRDGTextureRef NextTexture = GraphBuilder.CreateTexture(NextDesc, TEXT("RealOcean.FoamClipmap.Next"));

    const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
    TShaderMapRef<FRealOceanFoamClipmapCS> ComputeShader(ShaderMap);
    FRealOceanFoamClipmapCS::FParameters* Params = GraphBuilder.AllocParameters<FRealOceanFoamClipmapCS::FParameters>();
    Params->PreviousFoamTexture = PersistentTexture;
    Params->PreviousFoamSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    Params->OutputFoamTexture = GraphBuilder.CreateUAV(NextTexture);
    Params->ClipmapExtent = Extent;
    Params->CenterDeltaMeters = FVector2f(CenterDeltaD);
    Params->CurrentCenterOffsetMeters = OceanState.FoamClipmapCenterOffsetMeters;
    Params->AdvectionVelocityMps = OceanState.FoamAdvectionVelocityMps;
    Params->DeltaTimeSeconds = DeltaTimeSeconds;
    Params->CoverageMeters = FMath::Max(1.0f, OceanState.FoamClipmapCoverageMeters);
    Params->FoamHalfLifeSeconds = FMath::Max(0.1f, OceanState.FoamDecayHalfLifeSeconds);
    Params->InjectionPerSecond = FMath::Max(0.0f, OceanState.FoamClipmapInjectionPerSecond);
    Params->DiffusionPerSecond = FMath::Clamp(OceanState.FoamClipmapDiffusionPerSecond, 0.0f, 1.0f);
    Params->ResetHistory = bReset ? 1 : 0;
    Params->WindSpeedMps = FMath::Max(0.0f, OceanState.WindSpeedMps);
    Params->CombinedHsMeters = FMath::Max(0.0f, OceanState.SignificantWaveHeightMeters);
    Params->BreakingStrength = FMath::Max(0.0f, OceanState.BreakingStrength);
    Params->WhitecapCoverageScale = FMath::Max(0.0f, OceanState.WhitecapCoverageScale);
    Params->SwellDirectionDegrees = OceanState.TravelDirectionDegrees;
    Params->WindDirectionDegrees = OceanState.WindDirectionDegrees;
    Params->SwellHsMeters = OceanState.SwellSignificantWaveHeightMeters;
    Params->WindSeaHsMeters = OceanState.WindSeaSignificantWaveHeightMeters;
    Params->SwellPeriodSeconds = OceanState.DominantSwellPeriodSeconds;
    Params->WindSeaPeakPeriodSeconds = OceanState.WindSeaPeakPeriodSeconds;
    Params->SwellDirectionalSpread = OceanState.DirectionalSpread;
    Params->WindSeaDirectionalSpread = OceanState.WindSeaDirectionalSpread;
    Params->Steepness = OceanState.Steepness;
    Params->WindSeaSteepnessScale = OceanState.WindSeaSteepnessScale;
    Params->GravityMS2 = OceanState.GravityMS2;
    Params->AnchorPhase0123 = OceanState.AnchorPhase0123;
    Params->AnchorPhase4567 = OceanState.AnchorPhase4567;

    FComputeShaderUtils::AddPass(
        GraphBuilder,
        RDG_EVENT_NAME("RealOcean.FoamClipmap.Update.M32 %dx%d Rate=%dHz Reset=%d", Extent.X, Extent.Y, FMath::RoundToInt(OceanState.FoamClipmapUpdateRateHz), bReset ? 1 : 0),
        ComputeShader,
        Params,
        FComputeShaderUtils::GetGroupCount(Extent, FIntPoint(8, 8)));

    AddCopyTexturePass(GraphBuilder, NextTexture, PersistentTexture);

    InOutHistory.bValid = true;
    InOutHistory.LastOceanTimeSeconds = CurrentTime;
    InOutHistory.PreviousCenterWorldMeters = CurrentCenterWorldMeters;
    return true;
}

#else

bool AddRealOceanFoamClipmapPass(
    FRDGBuilder&,
    const FSceneView&,
    const FRealOceanRenderState&,
    FRealOceanFoamClipmapHistory& InOutHistory,
    bool)
{
    InOutHistory.bValid = false;
    return false;
}

#endif
