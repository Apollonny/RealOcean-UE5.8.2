#pragma once

#include "CoreMinimal.h"

class FRDGBuilder;
class FSceneView;
struct FRealOceanRenderState;
struct IPooledRenderTarget;

struct FRealOceanFoamClipmapHistory
{
    bool bValid = false;
    double LastOceanTimeSeconds = -1.0;
    FVector2d PreviousCenterWorldMeters = FVector2d::ZeroVector;
};

// Updates the plugin-owned persistent R16F foam texture after the base pass has sampled it.
// The result is visible to the material on the next frame, avoiding read/write hazards while
// preserving true frame-to-frame state. Returns true when an update pass was scheduled.
bool AddRealOceanFoamClipmapPass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FRealOceanRenderState& OceanState,
    FRealOceanFoamClipmapHistory& InOutHistory,
    bool bForceReset);
