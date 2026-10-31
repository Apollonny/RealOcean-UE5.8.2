#pragma once

#include "CoreMinimal.h"
#include "RHIResources.h"

// Render-thread snapshot of the physical ocean state used by the experimental RDG path.
// The game thread pushes a complete immutable copy; the render thread never dereferences UObjects.
struct FRealOceanRenderState
{
    bool bConfigured = false;
    bool bCausticsEnabled = true;

    float SeaLevelCm = 0.0f;
    float CameraDepthMeters = 0.0f;
    float SunRelativeIntensity = 0.0f;
    float SunCosZenithAir = 1.0f;
    FVector3f SunDirectionToSourceWS = FVector3f(0.0f, 0.0f, 1.0f);
    FVector3f SunColor = FVector3f(1.0f, 1.0f, 1.0f);
    FVector3f AbsorptionPerMeter = FVector3f::ZeroVector;
    FVector3f ScatteringPerMeter = FVector3f::ZeroVector;

    float AirIOR = 1.00029f;
    float WaterIOR = 1.333f;
    double OceanTimeSeconds = 0.0;
    float TravelDirectionDegrees = 0.0f;
    float WindDirectionDegrees = 0.0f;
    float WindSpeedMps = 8.0f;
    float SignificantWaveHeightMeters = 2.0f;
    float SwellSignificantWaveHeightMeters = 2.0f;
    float WindSeaSignificantWaveHeightMeters = 0.0f;
    float DominantSwellPeriodSeconds = 10.5f;
    float WindSeaPeakPeriodSeconds = 5.5f;
    float DirectionalSpread = 0.35f;
    float WindSeaDirectionalSpread = 0.72f;
    float Steepness = 0.72f;
    float WindSeaSteepnessScale = 0.88f;
    float GravityMS2 = 9.80665f;
    FVector4f AnchorPhase0123 = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
    FVector4f AnchorPhase4567 = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);

    float EffectiveMaxDepthMeters = 120.0f;
    float DerivativeStepMeters = 0.45f;
    float ScreenSpaceStability = 1.25f;
    float Contrast = 1.35f;
    float FocusClamp = 8.0f;
    float CausticsIntensity = 1.0f;
    float SolveIterations = 3.0f;
    float DerivativeScale = 0.75f;
    float QualityMode = 2.0f;

    // M30-profiled stateful GPU foam clipmap. Center offset is relative to the render camera so shader
    // coordinates remain LWC-safe even when the absolute world origin is very large.
    bool bFoamClipmapEnabled = false;
    int32 FoamClipmapResolution = 512;
    float FoamClipmapCoverageMeters = 256.0f;
    float FoamClipmapUpdateRateHz = 60.0f;
    float FoamDecayHalfLifeSeconds = 1.8f;
    float FoamClipmapInjectionPerSecond = 2.0f;
    float FoamClipmapDiffusionPerSecond = 0.08f;
    float BreakingStrength = 1.0f;
    float WhitecapCoverageScale = 1.0f;
    FVector2f FoamAdvectionVelocityMps = FVector2f::ZeroVector;
    FVector2f FoamClipmapCenterOffsetMeters = FVector2f::ZeroVector;
    FVector2d FoamClipmapCenterWorldMeters = FVector2d::ZeroVector;
};

class FTextureRenderTargetResource;
class FSceneInterface;

// Render bridge entries are scoped by FSceneInterface so PIE/editor preview/multi-world rendering cannot
// consume another world's ocean state or persistent foam texture. Scene pointers are opaque keys only.
void PushRealOceanRenderState_GameThread(const FSceneInterface* Scene, const FRealOceanRenderState& State);
const FRealOceanRenderState& GetRealOceanRenderState_RenderThread(const FSceneInterface* Scene);
FRealOceanRenderState BuildRealOceanViewRenderState_RenderThread(const FRealOceanRenderState& BaseState, const FVector& ViewOriginCm);
void PushRealOceanFoamClipmapResource_GameThread(const FSceneInterface* Scene, FTextureRenderTargetResource* Resource);
FTextureRHIRef GetRealOceanFoamClipmapTexture_RenderThread(const FSceneInterface* Scene);
void RemoveRealOceanRenderBridge_GameThread(const FSceneInterface* Scene);
