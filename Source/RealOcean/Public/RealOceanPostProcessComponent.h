#pragma once

#include "CoreMinimal.h"
#include "Components/PostProcessComponent.h"
#include "RealOceanPostProcessComponent.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;

UCLASS(ClassGroup=(Rendering), meta=(BlueprintSpawnableComponent))
class REALOCEAN_API URealOceanPostProcessComponent : public UPostProcessComponent
{
    GENERATED_BODY()

public:
    URealOceanPostProcessComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UFUNCTION(BlueprintCallable, Category = "Real Ocean|Post Process")
    void RebuildUnderwaterMaterial();

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Post Process")
    UMaterialInstanceDynamic* GetUnderwaterMID() const { return UnderwaterMID; }

    // Profiling-only force override used to isolate underwater post-process cost in A/B captures.
    // Shipping builds can leave this disabled; normal depth-driven blending is unchanged.
    UFUNCTION(BlueprintCallable, Category = "Real Ocean|Post Process")
    void SetBlendWeightOverride(bool bEnabled, float Weight = 0.0f);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Post Process")
    bool IsBlendWeightOverrideEnabled() const { return bBlendWeightOverrideEnabled; }

    // Must be a Post Process domain material. The supplied Milestone-4 guide documents the exact graph.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Real Ocean|Post Process")
    TObjectPtr<UMaterialInterface> UnderwaterPostProcessMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Real Ocean|Post Process")
    bool bDriveBlendWeightFromOceanDepth = true;

    // Generated 0.33.4 material computes underwater depth/blend from native per-view camera inputs.
    // Keep the unbound material active for every view; manual legacy materials may disable this.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Real Ocean|Post Process")
    bool bPerViewMaterialSelfBlend = true;

private:
    void UpdateUnderwaterBlendable(float Weight);

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> UnderwaterMID = nullptr;

    float LastAppliedBlendWeight = -1.0f;
    bool bBlendWeightOverrideEnabled = false;
    float BlendWeightOverride = 0.0f;
};
