#include "RealOceanHalfResCausticsShaders.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneManagement.h"
#include "SceneRenderTargetParameters.h"
#include "SceneTexturesConfig.h"
#include "ShaderParameterStruct.h"
#include "RealOceanRenderBridge.h"
#include "RealOceanReleaseConfig.h"

#if !UE_BUILD_SHIPPING || REALOCEAN_RELEASE_CAUSTICS_PATH != 0

namespace
{
    class FRealOceanHalfResCausticsPS final : public FGlobalShader
    {
        DECLARE_GLOBAL_SHADER(FRealOceanHalfResCausticsPS);
        SHADER_USE_PARAMETER_STRUCT(FRealOceanHalfResCausticsPS, FGlobalShader);

    public:
        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
            SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
            SHADER_PARAMETER(FIntPoint, FullExtent)
            SHADER_PARAMETER(FIntPoint, FullViewRectMin)
            SHADER_PARAMETER(FIntPoint, FullViewRectSize)
            SHADER_PARAMETER(float, CameraDepthMeters)
            SHADER_PARAMETER(FVector3f, SunDirectionToSourceWS)
            SHADER_PARAMETER(FVector3f, SunColor)
            SHADER_PARAMETER(float, SunRelativeIntensity)
            SHADER_PARAMETER(FVector3f, AbsorptionPerMeter)
            SHADER_PARAMETER(FVector3f, ScatteringPerMeter)
            SHADER_PARAMETER(float, SunCosZenithAir)
            SHADER_PARAMETER(float, AirIOR)
            SHADER_PARAMETER(float, WaterIOR)
            SHADER_PARAMETER(float, OceanTimeSeconds)
            SHADER_PARAMETER(float, TravelDirectionDegrees)
            SHADER_PARAMETER(float, WindDirectionDegrees)
            SHADER_PARAMETER(float, SwellSignificantWaveHeightMeters)
            SHADER_PARAMETER(float, WindSeaSignificantWaveHeightMeters)
            SHADER_PARAMETER(float, DominantSwellPeriodSeconds)
            SHADER_PARAMETER(float, WindSeaPeakPeriodSeconds)
            SHADER_PARAMETER(float, DirectionalSpread)
            SHADER_PARAMETER(float, WindSeaDirectionalSpread)
            SHADER_PARAMETER(float, Steepness)
            SHADER_PARAMETER(float, WindSeaSteepnessScale)
            SHADER_PARAMETER(float, GravityMS2)
            SHADER_PARAMETER(FVector4f, AnchorPhase0123)
            SHADER_PARAMETER(FVector4f, AnchorPhase4567)
            SHADER_PARAMETER(float, EffectiveMaxDepthMeters)
            SHADER_PARAMETER(float, DerivativeStepMeters)
            SHADER_PARAMETER(float, ScreenSpaceStability)
            SHADER_PARAMETER(float, Contrast)
            SHADER_PARAMETER(float, FocusClamp)
            SHADER_PARAMETER(float, CausticsIntensity)
            SHADER_PARAMETER(float, SolveIterations)
            SHADER_PARAMETER(float, DerivativeScale)
            SHADER_PARAMETER(float, QualityMode)
            RENDER_TARGET_BINDING_SLOTS()
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
        }
    };

    class FRealOceanHalfResTemporalPS final : public FGlobalShader
    {
        DECLARE_GLOBAL_SHADER(FRealOceanHalfResTemporalPS);
        SHADER_USE_PARAMETER_STRUCT(FRealOceanHalfResTemporalPS, FGlobalShader);

    public:
        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
            SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CurrentCausticsTexture)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryCausticsTexture)
            SHADER_PARAMETER_SAMPLER(SamplerState, HistorySampler)
            SHADER_PARAMETER(FIntPoint, FullExtent)
            SHADER_PARAMETER(FIntPoint, HalfExtent)
            SHADER_PARAMETER(FIntPoint, FullViewRectMin)
            SHADER_PARAMETER(FIntPoint, FullViewRectSize)
            SHADER_PARAMETER(int32, HistoryValid)
            SHADER_PARAMETER(float, HistoryWeight)
            SHADER_PARAMETER(float, HistoryDepthRejectMeters)
            SHADER_PARAMETER(float, HistoryMotionRejectPixels)
            SHADER_PARAMETER(float, HistoryClampExpansion)
            SHADER_PARAMETER(float, PatternHistoryConfidence)
            SHADER_PARAMETER(float, HistoryRadianceRejectRatio)
            SHADER_PARAMETER(int32, SceneVelocityEnabled)
            SHADER_PARAMETER(float, ObjectVelocityRejectPixels)
            RENDER_TARGET_BINDING_SLOTS()
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
        }
    };

    class FRealOceanHalfResCompositePS final : public FGlobalShader
    {
        DECLARE_GLOBAL_SHADER(FRealOceanHalfResCompositePS);
        SHADER_USE_PARAMETER_STRUCT(FRealOceanHalfResCompositePS, FGlobalShader);

    public:
        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
            SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HalfResCausticsTexture)
            SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
            SHADER_PARAMETER(FIntPoint, FullExtent)
            SHADER_PARAMETER(FIntPoint, HalfExtent)
            SHADER_PARAMETER(FIntPoint, FullViewRectMin)
            SHADER_PARAMETER(FIntPoint, FullViewRectSize)
            SHADER_PARAMETER(float, CameraDepthMeters)
            SHADER_PARAMETER(float, DepthSharpness)
            SHADER_PARAMETER(float, CompositeStrength)
            SHADER_PARAMETER(float, PreExposure)
            SHADER_PARAMETER(int32, VisualizeOnly)
            RENDER_TARGET_BINDING_SLOTS()
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
        }
    };

    IMPLEMENT_GLOBAL_SHADER(FRealOceanHalfResCausticsPS,
        "/Plugin/RealOcean/RealOceanHalfResCaustics.usf", "HalfResCausticsPS", SF_Pixel);
    IMPLEMENT_GLOBAL_SHADER(FRealOceanHalfResTemporalPS,
        "/Plugin/RealOcean/RealOceanHalfResCaustics.usf", "TemporalResolvePS", SF_Pixel);
    IMPLEMENT_GLOBAL_SHADER(FRealOceanHalfResCompositePS,
        "/Plugin/RealOcean/RealOceanHalfResCaustics.usf", "CompositeCausticsPS", SF_Pixel);
}

FScreenPassTexture AddRealOceanHalfResCausticsPass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessMaterialInputs& Inputs,
    const FRealOceanRenderState& OceanState,
    int32 Mode,
    float DepthSharpness,
    float CompositeStrength,
    bool bTemporalEnabled,
    float HistoryWeight,
    float HistoryDepthRejectMeters,
    float HistoryMotionRejectPixels,
    float HistoryClampExpansion,
    float PatternHistoryConfidence,
    float HistoryRadianceRejectRatio,
    bool bSceneVelocityEnabled,
    float ObjectVelocityRejectPixels,
    TRefCountPtr<IPooledRenderTarget>* InOutHistoryTarget)
{
    const FScreenPassTexture SceneColor = Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
    if (!SceneColor.IsValid() || !OceanState.bConfigured || !OceanState.bCausticsEnabled || OceanState.CameraDepthMeters <= 0.01f)
    {
        if (InOutHistoryTarget)
        {
            *InOutHistoryTarget = nullptr;
        }
        return SceneColor;
    }

    const FIntPoint FullExtent = SceneColor.Texture->Desc.Extent;
    const FIntPoint HalfExtent(
        FMath::Max(1, FMath::DivideAndRoundUp(FullExtent.X, 2)),
        FMath::Max(1, FMath::DivideAndRoundUp(FullExtent.Y, 2)));

    FRDGTextureDesc HalfDesc = FRDGTextureDesc::Create2D(
        HalfExtent,
        PF_FloatRGBA,
        FClearValueBinding::Transparent,
        TexCreate_ShaderResource | TexCreate_RenderTargetable);
    FRDGTextureRef HalfTexture = GraphBuilder.CreateTexture(HalfDesc, TEXT("RealOcean.Experimental.HalfResCaustics"));

    const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
    TShaderMapRef<FRealOceanHalfResCausticsPS> HalfShader(ShaderMap);
    FRealOceanHalfResCausticsPS::FParameters* HalfParams = GraphBuilder.AllocParameters<FRealOceanHalfResCausticsPS::FParameters>();
    HalfParams->View = View.ViewUniformBuffer;
    HalfParams->SceneTextures = Inputs.SceneTextures;
    HalfParams->FullExtent = FullExtent;
    HalfParams->FullViewRectMin = SceneColor.ViewRect.Min;
    HalfParams->FullViewRectSize = SceneColor.ViewRect.Size();
    HalfParams->CameraDepthMeters = OceanState.CameraDepthMeters;
    HalfParams->SunDirectionToSourceWS = OceanState.SunDirectionToSourceWS;
    HalfParams->SunColor = OceanState.SunColor;
    HalfParams->SunRelativeIntensity = OceanState.SunRelativeIntensity;
    HalfParams->AbsorptionPerMeter = OceanState.AbsorptionPerMeter;
    HalfParams->ScatteringPerMeter = OceanState.ScatteringPerMeter;
    HalfParams->SunCosZenithAir = OceanState.SunCosZenithAir;
    HalfParams->AirIOR = OceanState.AirIOR;
    HalfParams->WaterIOR = OceanState.WaterIOR;
    HalfParams->OceanTimeSeconds = static_cast<float>(OceanState.OceanTimeSeconds);
    HalfParams->TravelDirectionDegrees = OceanState.TravelDirectionDegrees;
    HalfParams->WindDirectionDegrees = OceanState.WindDirectionDegrees;
    HalfParams->SwellSignificantWaveHeightMeters = OceanState.SwellSignificantWaveHeightMeters;
    HalfParams->WindSeaSignificantWaveHeightMeters = OceanState.WindSeaSignificantWaveHeightMeters;
    HalfParams->DominantSwellPeriodSeconds = OceanState.DominantSwellPeriodSeconds;
    HalfParams->WindSeaPeakPeriodSeconds = OceanState.WindSeaPeakPeriodSeconds;
    HalfParams->DirectionalSpread = OceanState.DirectionalSpread;
    HalfParams->WindSeaDirectionalSpread = OceanState.WindSeaDirectionalSpread;
    HalfParams->Steepness = OceanState.Steepness;
    HalfParams->WindSeaSteepnessScale = OceanState.WindSeaSteepnessScale;
    HalfParams->GravityMS2 = OceanState.GravityMS2;
    HalfParams->AnchorPhase0123 = OceanState.AnchorPhase0123;
    HalfParams->AnchorPhase4567 = OceanState.AnchorPhase4567;
    HalfParams->EffectiveMaxDepthMeters = OceanState.EffectiveMaxDepthMeters;
    HalfParams->DerivativeStepMeters = OceanState.DerivativeStepMeters;
    HalfParams->ScreenSpaceStability = OceanState.ScreenSpaceStability;
    HalfParams->Contrast = OceanState.Contrast;
    HalfParams->FocusClamp = OceanState.FocusClamp;
    HalfParams->CausticsIntensity = OceanState.CausticsIntensity;
    HalfParams->SolveIterations = OceanState.SolveIterations;
    HalfParams->DerivativeScale = OceanState.DerivativeScale;
    HalfParams->QualityMode = OceanState.QualityMode;
    HalfParams->RenderTargets[0] = FRenderTargetBinding(HalfTexture, ERenderTargetLoadAction::EClear);

    FPixelShaderUtils::AddFullscreenPass(
        GraphBuilder,
        ShaderMap,
        RDG_EVENT_NAME("RealOcean.Experimental.HalfResCausticsField"),
        HalfShader,
        HalfParams,
        FIntRect(FIntPoint::ZeroValue, HalfExtent));

    FRDGTextureRef ReconstructedTexture = HalfTexture;

    if (bTemporalEnabled && InOutHistoryTarget)
    {
        bool bHistoryValid = InOutHistoryTarget->IsValid();
        if (bHistoryValid)
        {
            const FPooledRenderTargetDesc& HistoryDesc = (*InOutHistoryTarget)->GetDesc();
            bHistoryValid = HistoryDesc.Extent == HalfExtent && HistoryDesc.Format == PF_FloatRGBA;
        }

        FRDGTextureRef HistoryTexture = HalfTexture;
        if (bHistoryValid)
        {
            HistoryTexture = GraphBuilder.RegisterExternalTexture(
                *InOutHistoryTarget,
                TEXT("RealOcean.Experimental.HalfResCausticsHistory"),
                ERDGTextureFlags::None);
        }

        FRDGTextureRef ResolvedTexture = GraphBuilder.CreateTexture(
            HalfDesc,
            TEXT("RealOcean.Experimental.HalfResCausticsTemporalResolved"));

        TShaderMapRef<FRealOceanHalfResTemporalPS> TemporalShader(ShaderMap);
        FRealOceanHalfResTemporalPS::FParameters* TemporalParams = GraphBuilder.AllocParameters<FRealOceanHalfResTemporalPS::FParameters>();
        TemporalParams->View = View.ViewUniformBuffer;
        // Request both depth and velocity explicitly. If velocity is absent for a pixel the shader
        // falls back to depth + ClipToPrevClip camera reprojection.
        TemporalParams->SceneTextures = CreateSceneTextureShaderParameters(
            GraphBuilder,
            View,
            ESceneTextureSetupMode::SceneDepth | ESceneTextureSetupMode::SceneVelocity);
        TemporalParams->CurrentCausticsTexture = HalfTexture;
        TemporalParams->HistoryCausticsTexture = HistoryTexture;
        TemporalParams->HistorySampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
        TemporalParams->FullExtent = FullExtent;
        TemporalParams->HalfExtent = HalfExtent;
        TemporalParams->FullViewRectMin = SceneColor.ViewRect.Min;
        TemporalParams->FullViewRectSize = SceneColor.ViewRect.Size();
        TemporalParams->HistoryValid = bHistoryValid ? 1 : 0;
        TemporalParams->HistoryWeight = FMath::Clamp(HistoryWeight, 0.0f, 0.98f);
        TemporalParams->HistoryDepthRejectMeters = FMath::Max(0.01f, HistoryDepthRejectMeters);
        TemporalParams->HistoryMotionRejectPixels = FMath::Max(1.0f, HistoryMotionRejectPixels);
        TemporalParams->HistoryClampExpansion = FMath::Max(0.0f, HistoryClampExpansion);
        TemporalParams->PatternHistoryConfidence = FMath::Clamp(PatternHistoryConfidence, 0.0f, 1.0f);
        TemporalParams->HistoryRadianceRejectRatio = FMath::Max(0.05f, HistoryRadianceRejectRatio);
        TemporalParams->SceneVelocityEnabled = bSceneVelocityEnabled ? 1 : 0;
        TemporalParams->ObjectVelocityRejectPixels = FMath::Max(1.0f, ObjectVelocityRejectPixels);
        TemporalParams->RenderTargets[0] = FRenderTargetBinding(ResolvedTexture, ERenderTargetLoadAction::EClear);

        FPixelShaderUtils::AddFullscreenPass(
            GraphBuilder,
            ShaderMap,
            RDG_EVENT_NAME("RealOcean.Experimental.HalfResCausticsTemporalResolve.M34"),
            TemporalShader,
            TemporalParams,
            FIntRect(FIntPoint::ZeroValue, HalfExtent));

        GraphBuilder.QueueTextureExtraction(
            ResolvedTexture,
            InOutHistoryTarget,
            ERDGResourceExtractionFlags::None);

        ReconstructedTexture = ResolvedTexture;
    }
    else if (InOutHistoryTarget)
    {
        *InOutHistoryTarget = nullptr;
    }

    FScreenPassRenderTarget Output = Inputs.OverrideOutput;
    if (!Output.IsValid())
    {
        Output = FScreenPassRenderTarget::CreateFromInput(
            GraphBuilder,
            SceneColor,
            ERenderTargetLoadAction::ENoAction,
            TEXT("RealOcean.Experimental.HalfResComposite"));
    }

    TShaderMapRef<FRealOceanHalfResCompositePS> CompositeShader(ShaderMap);
    FRealOceanHalfResCompositePS::FParameters* CompositeParams = GraphBuilder.AllocParameters<FRealOceanHalfResCompositePS::FParameters>();
    CompositeParams->View = View.ViewUniformBuffer;
    CompositeParams->SceneTextures = Inputs.SceneTextures;
    CompositeParams->SceneColorTexture = SceneColor.Texture;
    CompositeParams->HalfResCausticsTexture = ReconstructedTexture;
    CompositeParams->SceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    CompositeParams->FullExtent = FullExtent;
    CompositeParams->HalfExtent = HalfExtent;
    CompositeParams->FullViewRectMin = SceneColor.ViewRect.Min;
    CompositeParams->FullViewRectSize = SceneColor.ViewRect.Size();
    CompositeParams->CameraDepthMeters = OceanState.CameraDepthMeters;
    CompositeParams->DepthSharpness = FMath::Max(0.0f, DepthSharpness);
    CompositeParams->CompositeStrength = FMath::Max(0.0f, CompositeStrength);
    CompositeParams->PreExposure = View.State ? View.State->GetPreExposure() : 1.0f;
    CompositeParams->VisualizeOnly = (Mode == 2) ? 1 : 0;
    CompositeParams->RenderTargets[0] = Output.GetRenderTargetBinding();

    FPixelShaderUtils::AddFullscreenPass(
        GraphBuilder,
        ShaderMap,
        RDG_EVENT_NAME("RealOcean.Experimental.HalfResCausticsComposite"),
        CompositeShader,
        CompositeParams,
        Output.ViewRect);

    return FScreenPassTexture(Output.Texture, Output.ViewRect);
}


#else

// Full-resolution Shipping builds intentionally do not register/compile the experimental RDG
// caustics global shaders. Keep a link-safe passthrough implementation in case this symbol is
// referenced by generic module code; the SceneViewExtension is not registered for path 0.
FScreenPassTexture AddRealOceanHalfResCausticsPass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessMaterialInputs& Inputs,
    const FRealOceanRenderState& OceanState,
    int32 Mode,
    float DepthSharpness,
    float CompositeStrength,
    bool bTemporalEnabled,
    float HistoryWeight,
    float HistoryDepthRejectMeters,
    float HistoryMotionRejectPixels,
    float HistoryClampExpansion,
    float PatternHistoryConfidence,
    float HistoryRadianceRejectRatio,
    bool bSceneVelocityEnabled,
    float ObjectVelocityRejectPixels,
    TRefCountPtr<IPooledRenderTarget>* InOutHistoryTarget)
{
    if (InOutHistoryTarget)
    {
        *InOutHistoryTarget = nullptr;
    }
    return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
}

#endif // !UE_BUILD_SHIPPING || REALOCEAN_RELEASE_CAUSTICS_PATH != 0
