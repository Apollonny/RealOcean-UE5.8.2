#include "RealOceanPostProcessComponent.h"

#include "Engine/BlendableInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "RealOceanSubsystem.h"

URealOceanPostProcessComponent::URealOceanPostProcessComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;

    bUnbound = true;
    BlendRadius = 0.0f;
    BlendWeight = 1.0f;
    Priority = 100.0f;
}

void URealOceanPostProcessComponent::BeginPlay()
{
    Super::BeginPlay();
    RebuildUnderwaterMaterial();
}

void URealOceanPostProcessComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!UnderwaterMID)
    {
        return;
    }

    if (bBlendWeightOverrideEnabled)
    {
        UpdateUnderwaterBlendable(BlendWeightOverride);
        return;
    }

    if (bPerViewMaterialSelfBlend)
    {
        // Generated material performs the waterline test independently for each FSceneView.
        UpdateUnderwaterBlendable(1.0f);
        return;
    }

    if (!bDriveBlendWeightFromOceanDepth)
    {
        // Disabling automatic depth drive must not leave the last underwater weight latched forever.
        UpdateUnderwaterBlendable(0.0f);
        return;
    }

    float Weight = 0.0f;
    if (UWorld* World = GetWorld())
    {
        if (const URealOceanSubsystem* Ocean = World->GetSubsystem<URealOceanSubsystem>())
        {
            Weight = static_cast<float>(FMath::Clamp(Ocean->GetOceanState().UnderwaterBlend, 0.0, 1.0));
        }
    }

    UpdateUnderwaterBlendable(Weight);
}

void URealOceanPostProcessComponent::RebuildUnderwaterMaterial()
{
    // Keep old blendable references harmless at weight zero. Epic's blendable container is designed
    // to retain zero-weight entries, so no removal API is required here.
    if (UnderwaterMID)
    {
        TScriptInterface<IBlendableInterface> OldBlendable;
        OldBlendable.SetObject(UnderwaterMID);
        OldBlendable.SetInterface(Cast<IBlendableInterface>(UnderwaterMID));
        AddOrUpdateBlendable(OldBlendable, 0.0f);
    }

    UnderwaterMID = nullptr;
    LastAppliedBlendWeight = -1.0f;

    if (!UnderwaterPostProcessMaterial)
    {
        return;
    }

    UnderwaterMID = UMaterialInstanceDynamic::Create(UnderwaterPostProcessMaterial, this);
    if (UnderwaterMID)
    {
        UpdateUnderwaterBlendable(bBlendWeightOverrideEnabled ? BlendWeightOverride : (bPerViewMaterialSelfBlend ? 1.0f : 0.0f));
    }
}

void URealOceanPostProcessComponent::SetBlendWeightOverride(bool bEnabled, float Weight)
{
    bBlendWeightOverrideEnabled = bEnabled;
    BlendWeightOverride = FMath::Clamp(Weight, 0.0f, 1.0f);
    if (UnderwaterMID)
    {
        LastAppliedBlendWeight = -1.0f;
        if (bBlendWeightOverrideEnabled)
        {
            UpdateUnderwaterBlendable(BlendWeightOverride);
        }
        else if (bPerViewMaterialSelfBlend)
        {
            UpdateUnderwaterBlendable(1.0f);
        }
    }
}

void URealOceanPostProcessComponent::UpdateUnderwaterBlendable(float Weight)
{
    if (!UnderwaterMID)
    {
        return;
    }

    const float ClampedWeight = FMath::Clamp(Weight, 0.0f, 1.0f);
    if (LastAppliedBlendWeight >= 0.0f && FMath::IsNearlyEqual(LastAppliedBlendWeight, ClampedWeight, 1.0e-4f))
    {
        return;
    }

    TScriptInterface<IBlendableInterface> Blendable;
    Blendable.SetObject(UnderwaterMID);
    Blendable.SetInterface(Cast<IBlendableInterface>(UnderwaterMID));
    AddOrUpdateBlendable(Blendable, ClampedWeight);
    LastAppliedBlendWeight = ClampedWeight;
}
