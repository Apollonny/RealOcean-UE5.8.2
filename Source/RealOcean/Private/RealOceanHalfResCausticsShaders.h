#pragma once

#include "CoreMinimal.h"
#include "RenderTargetPool.h"
#include "ScreenPass.h"

class FRDGBuilder;
class FSceneView;
struct FPostProcessMaterialInputs;
struct FRealOceanRenderState;

// M19 RDG half-resolution caustics path. Development builds expose A/B controls; Shipping
// compiles/uses it only when REALOCEAN_RELEASE_CAUSTICS_PATH is locked to 1 or 2.
// Mode: 1=composite, 2=visualize reconstructed / temporally resolved caustic buffer.
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
    TRefCountPtr<IPooledRenderTarget>* InOutHistoryTarget);
