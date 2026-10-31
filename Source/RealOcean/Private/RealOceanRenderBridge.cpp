#include "RealOceanRenderBridge.h"

#include "RenderingThread.h"
#include "TextureResource.h"

namespace
{
    struct FRealOceanRenderBridgeEntry
    {
        FRealOceanRenderState State;
        FTextureRHIRef FoamClipmapTexture;
    };

    TMap<const FSceneInterface*, FRealOceanRenderBridgeEntry> GRealOceanRenderBridges;
    FRealOceanRenderState GRealOceanDefaultRenderState;
}

void PushRealOceanRenderState_GameThread(const FSceneInterface* Scene, const FRealOceanRenderState& State)
{
    check(IsInGameThread());
    if (!Scene)
    {
        return;
    }

    const FRealOceanRenderState Copy = State;
    ENQUEUE_RENDER_COMMAND(RealOceanPushRenderState)(
        [Scene, Copy](FRHICommandListImmediate& RHICmdList)
        {
            GRealOceanRenderBridges.FindOrAdd(Scene).State = Copy;
        });
}

const FRealOceanRenderState& GetRealOceanRenderState_RenderThread(const FSceneInterface* Scene)
{
    check(IsInRenderingThread() || IsInParallelRenderingThread());
    if (Scene)
    {
        if (const FRealOceanRenderBridgeEntry* Entry = GRealOceanRenderBridges.Find(Scene))
        {
            return Entry->State;
        }
    }
    return GRealOceanDefaultRenderState;
}

void PushRealOceanFoamClipmapResource_GameThread(const FSceneInterface* Scene, FTextureRenderTargetResource* Resource)
{
    check(IsInGameThread());
    if (!Scene)
    {
        return;
    }

    ENQUEUE_RENDER_COMMAND(RealOceanPushFoamClipmapResource)(
        [Scene, Resource](FRHICommandListImmediate& RHICmdList)
        {
            FRealOceanRenderBridgeEntry& Entry = GRealOceanRenderBridges.FindOrAdd(Scene);
            Entry.FoamClipmapTexture = Resource ? Resource->GetRenderTargetTexture() : FTextureRHIRef();
        });
}

FTextureRHIRef GetRealOceanFoamClipmapTexture_RenderThread(const FSceneInterface* Scene)
{
    check(IsInRenderingThread() || IsInParallelRenderingThread());
    if (Scene)
    {
        if (const FRealOceanRenderBridgeEntry* Entry = GRealOceanRenderBridges.Find(Scene))
        {
            return Entry->FoamClipmapTexture;
        }
    }
    return FTextureRHIRef();
}

void RemoveRealOceanRenderBridge_GameThread(const FSceneInterface* Scene)
{
    check(IsInGameThread());
    if (!Scene)
    {
        return;
    }

    ENQUEUE_RENDER_COMMAND(RealOceanRemoveRenderBridge)(
        [Scene](FRHICommandListImmediate& RHICmdList)
        {
            GRealOceanRenderBridges.Remove(Scene);
        });
}


FRealOceanRenderState BuildRealOceanViewRenderState_RenderThread(const FRealOceanRenderState& BaseState, const FVector& ViewOriginCm)
{
    FRealOceanRenderState Out = BaseState;
    constexpr double TwoPi = 6.28318530717958647692;
    const FVector2d CameraMeters(ViewOriginCm.X * 0.01, ViewOriginCm.Y * 0.01);
    float Phases[8] = {};
    for (int32 Index = 0; Index < 8; ++Index)
    {
        const bool bSwell = Index < 3;
        static const double SwellLength[3] = {1.0, 0.66, 0.43};
        static const double SwellDir[3] = {0.0, 6.0, -9.0};
        static const double WindLength[5] = {1.0, 0.68, 0.46, 0.30, 0.19};
        static const double WindDir[5] = {-12.0, 8.0, -24.0, 31.0, -43.0};
        const int32 W = Index - 3;
        const double Period = bSwell ? BaseState.DominantSwellPeriodSeconds : BaseState.WindSeaPeakPeriodSeconds;
        const double LengthScale = bSwell ? SwellLength[Index] : WindLength[W];
        const double Spread = bSwell ? BaseState.DirectionalSpread : BaseState.WindSeaDirectionalSpread;
        const double DirectionDeg = (bSwell ? BaseState.TravelDirectionDegrees : BaseState.WindDirectionDegrees)
            + (bSwell ? SwellDir[Index] : WindDir[W]) * FMath::Clamp(Spread, 0.0, 1.0);
        const double Wavelength = FMath::Max(0.05, FMath::Max(0.01, static_cast<double>(BaseState.GravityMS2)) * Period * Period / TwoPi * LengthScale);
        const double K = TwoPi / Wavelength;
        const double Radians = FMath::DegreesToRadians(DirectionDeg);
        const FVector2d D(FMath::Cos(Radians), FMath::Sin(Radians));
        const double Omega = FMath::Sqrt(FMath::Max(0.01, static_cast<double>(BaseState.GravityMS2)) * K);
        double Phase = FMath::Fmod(K * FVector2d::DotProduct(D, CameraMeters) - Omega * BaseState.OceanTimeSeconds, TwoPi);
        if (Phase < 0.0) Phase += TwoPi;
        Phases[Index] = static_cast<float>(Phase);
    }
    Out.AnchorPhase0123 = FVector4f(Phases[0], Phases[1], Phases[2], Phases[3]);
    Out.AnchorPhase4567 = FVector4f(Phases[4], Phases[5], Phases[6], Phases[7]);
    Out.FoamClipmapCenterOffsetMeters = FVector2f(
        static_cast<float>(BaseState.FoamClipmapCenterWorldMeters.X - CameraMeters.X),
        static_cast<float>(BaseState.FoamClipmapCenterWorldMeters.Y - CameraMeters.Y));

    // Per-view camera depth. A bounded inverse solve mirrors the material/CPU Eulerian surface convention.
    FVector2d ParameterRelativeMeters = FVector2d::ZeroVector;
    double HeightMeters = 0.0;
    for (int32 Iteration = 0; Iteration <= 4; ++Iteration)
    {
        FVector2d Horizontal = FVector2d::ZeroVector;
        HeightMeters = 0.0;
        for (int32 Index = 0; Index < 8; ++Index)
        {
            const bool bSwell = Index < 3;
            static const double SwellLength[3] = {1.0, 0.66, 0.43};
            static const double SwellAmp[3] = {0.500, 0.310, 0.190};
            static const double SwellDir[3] = {0.0, 6.0, -9.0};
            static const double SwellOffset[3] = {0.37, 1.14, 2.21};
            static const double WindLength[5] = {1.0, 0.68, 0.46, 0.30, 0.19};
            static const double WindAmp[5] = {0.340, 0.250, 0.180, 0.140, 0.090};
            static const double WindDir[5] = {-12.0, 8.0, -24.0, 31.0, -43.0};
            static const double WindOffset[5] = {0.79, 2.87, 1.73, 0.19, 2.48};
            const int32 W = Index - 3;
            const double Period = bSwell ? BaseState.DominantSwellPeriodSeconds : BaseState.WindSeaPeakPeriodSeconds;
            const double LengthScale = bSwell ? SwellLength[Index] : WindLength[W];
            const double Spread = bSwell ? BaseState.DirectionalSpread : BaseState.WindSeaDirectionalSpread;
            const double DirectionDeg = (bSwell ? BaseState.TravelDirectionDegrees : BaseState.WindDirectionDegrees)
                + (bSwell ? SwellDir[Index] : WindDir[W]) * FMath::Clamp(Spread, 0.0, 1.0);
            const double Wavelength = FMath::Max(0.05, FMath::Max(0.01, static_cast<double>(BaseState.GravityMS2)) * Period * Period / TwoPi * LengthScale);
            const double K = TwoPi / Wavelength;
            const double Radians = FMath::DegreesToRadians(DirectionDeg);
            const FVector2d D(FMath::Cos(Radians), FMath::Sin(Radians));
            const double Amp = bSwell
                ? BaseState.SwellSignificantWaveHeightMeters * 0.5718862635895968 * SwellAmp[Index]
                : BaseState.WindSeaSignificantWaveHeightMeters * 0.7244094847496808 * WindAmp[W];
            const double Q = FMath::Clamp(static_cast<double>(BaseState.Steepness) * (bSwell ? 1.0 : BaseState.WindSeaSteepnessScale), 0.0, 0.95);
            const double Anchor = Index < 4 ? Out.AnchorPhase0123[Index] : Out.AnchorPhase4567[Index - 4];
            const double Phase = Anchor + K * FVector2d::DotProduct(D, ParameterRelativeMeters) + (bSwell ? SwellOffset[Index] : WindOffset[W]);
            Horizontal += D * (Q * Amp * FMath::Cos(Phase));
            HeightMeters += Amp * FMath::Sin(Phase);
        }
        if (Iteration < 4)
        {
            ParameterRelativeMeters = -Horizontal;
        }
    }
    const double DynamicDepthMeters = BaseState.SeaLevelCm * 0.01 + HeightMeters - ViewOriginCm.Z * 0.01;
    Out.CameraDepthMeters = static_cast<float>(FMath::Clamp(DynamicDepthMeters, 0.0, 3000.0));
    return Out;
}
