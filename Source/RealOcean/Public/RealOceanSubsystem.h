#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "RealOceanTypes.h"
#include "RealOceanSubsystem.generated.h"

class UMaterialParameterCollection;
class UMaterialParameterCollectionInstance;
class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;
class ADirectionalLight;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRealOceanDepthZoneChanged, ERealOceanDepthZone, PreviousZone, ERealOceanDepthZone, NewZone);

UCLASS(BlueprintType)
class REALOCEAN_API URealOceanSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    URealOceanSubsystem();

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override { return bConfigured; }
    virtual bool IsTickableInEditor() const override { return true; }

    UFUNCTION(BlueprintCallable, Category = "Real Ocean")
    void Configure(const FRealOceanSettings& InSettings, UMaterialParameterCollection* InGlobalParameters, ADirectionalLight* InSunDirectionalLight);

    // Controller-owned configuration keeps level-streaming teardown from leaving the world subsystem
    // ticking with stale MPC/render resources. A later controller may take ownership; only the current
    // owner is allowed to deconfigure the subsystem.
    void ConfigureForOwner(UObject* InConfigurationOwner, const FRealOceanSettings& InSettings, UMaterialParameterCollection* InGlobalParameters, ADirectionalLight* InSunDirectionalLight);
    void DeconfigureForOwner(const UObject* InConfigurationOwner);
    // World Partition handoff: release only ownership, preserving clocks/render state until the
    // explicit replacement controller configures in the same frame. Full teardown still uses DeconfigureForOwner.
    bool ReleaseOwnerForHandoff(const UObject* InConfigurationOwner);
    bool IsConfiguredByOwner(const UObject* InConfigurationOwner) const { return InConfigurationOwner && ConfigurationOwner.IsValid() && ConfigurationOwner.Get() == InConfigurationOwner; }
    bool CanConfigureForOwner(const UObject* InConfigurationOwner) const { return !bConfigured || !ConfigurationOwner.IsValid() || (InConfigurationOwner && ConfigurationOwner.Get() == InConfigurationOwner); }

    // Networked worlds can drive ocean phase from a replicated server-time epoch instead of local DeltaTime.
    // This keeps dedicated-server queries, clients and join-in-progress peers on the same deterministic phase.
    void SetSynchronizedClock(double ServerEpochSeconds, double WaveTimeAtEpochSeconds, double TurbulenceTimeAtEpochSeconds, double WaveTimeScale, double TurbulenceTimeScale);
    void ClearSynchronizedClock();
    // Re-establish zero-placement ownership after a streamed/destroyed explicit controller leaves.
    // Authority promotes a remaining explicit controller deterministically or creates a new auto fallback.
    void ScheduleTurnkeyControllerRecovery();
    double GetWaveClockSeconds() const { return WaveTimeSeconds; }
    double GetCurrentTurbulenceClockSeconds() const { return CurrentTurbulenceTimeSeconds; }

    UFUNCTION(BlueprintCallable, Category = "Real Ocean")
    void SetTrackedActor(AActor* InActor);

    UFUNCTION(BlueprintCallable, Category = "Real Ocean|Lighting")
    void SetSunDirectionalLight(ADirectionalLight* InSunDirectionalLight);

    // Development/profiling aid: suppresses material/MPC caustics while leaving the
    // physical render-thread caustics state enabled for the experimental RDG path.
    UFUNCTION(BlueprintCallable, Category = "Real Ocean|Profiling")
    void SetMaterialCausticsSuppressed(bool bSuppressed);

    // Non-shipping profiling is render-local by design. These overrides never mutate the
    // authoritative FRealOceanSettings used by wave/current/environment queries or replication.
    void SetLocalRenderProfilingOverrides(ERealOceanProfilingVariant Variant, bool bOverrideFoamPreset, ERealOceanFoamClipmapQualityPreset FoamPreset);

    // Binds the plugin-owned persistent foam texture to a project material instance. The material
    // must expose a Texture2D parameter named RO_FoamClipmapTexture. Safe to call repeatedly.
    UFUNCTION(BlueprintCallable, Category = "Real Ocean|Surface|GPU Foam")
    bool BindFoamClipmapToMaterial(UMaterialInstanceDynamic* MaterialInstance);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Surface|GPU Foam")
    UTextureRenderTarget2D* GetFoamClipmapRenderTarget() const { return FoamClipmapRenderTarget; }

    UFUNCTION(BlueprintPure, Category = "Real Ocean")
    FRealOceanState GetOceanState() const { return State; }

    UFUNCTION(BlueprintPure, Category = "Real Ocean")
    double GetDepthMeters() const { return State.DepthMeters; }

    UFUNCTION(BlueprintPure, Category = "Real Ocean")
    double GetAbsolutePressureBar() const { return State.AbsolutePressureBar; }

    UFUNCTION(BlueprintPure, Category = "Real Ocean")
    FRealOceanSettings GetOceanSettings() const { return Settings; }

    UFUNCTION(BlueprintPure, Category = "Real Ocean")
    ERealOceanDepthZone GetDepthZone() const { return State.DepthZone; }

    // Ocean-only environmental query. Returns data; it never applies forces or controls actors.
    UFUNCTION(BlueprintCallable, Category = "Real Ocean|Environment")
    FRealOceanEnvironmentSample QueryEnvironmentAtWorldPosition(FVector WorldPositionCm) const;

    UFUNCTION(BlueprintCallable, Category = "Real Ocean|Environment")
    FVector QueryCurrentVelocityMetersPerSecond(FVector WorldPositionCm) const;

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Environment")
    double QueryTemperatureC(double DepthMeters) const;

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Environment")
    double QuerySalinityPSU(double DepthMeters) const;

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Environment")
    double EstimateDensityKgM3(double DepthMeters) const;

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Integration")
    bool IsWaterIntegrationReady() const { return State.bWaterSubsystemAvailable && State.bWaterRenderingEnabled && State.bOceanBodyAvailable; }

    UFUNCTION(BlueprintPure, Category = "Real Ocean")
    static ERealOceanDepthZone ClassifyDepth(double DepthMeters);

    UFUNCTION(BlueprintPure, Category = "Real Ocean")
    static double CalculateGaugePressureBar(double DepthMeters, double DensityKgM3 = 1025.0, double GravityMS2 = 9.80665);

    UPROPERTY(BlueprintAssignable, Category = "Real Ocean")
    FRealOceanDepthZoneChanged OnDepthZoneChanged;

private:
    FVector ResolveTrackedPositionCm() const;
    FVector ResolveRenderCameraPositionCm() const;
    ARealOceanController* SpawnAutomaticTurnkeyController();
    void RecoverTurnkeyControllerOwnership();
    double ResolveNetworkServerTimeSeconds() const;
    double ResolveSeaLevelCm() const;
    void ResolveSunState();
    void ResolveQualityBudget();
    void ResolveWaterIntegrationState();
    void RecalculateState();
    void PushMaterialParameters();
    void EnsureFoamClipmapRenderTarget();
    void ReleaseFoamClipmapRenderTarget();
    void RefreshFoamClipmapMaterialBinding();
    void ConfigureInternal(UObject* InConfigurationOwner, const FRealOceanSettings& InSettings, UMaterialParameterCollection* InGlobalParameters, ADirectionalLight* InSunDirectionalLight);
    void DeconfigureInternal();
    int32 ResolveFoamClipmapResolution() const;
    double ResolveFoamClipmapUpdateRateHz() const;
    FVector2D CalculateFoamClipmapCenterOffsetMeters() const;
    ERealOceanFoamClipmapQualityPreset ResolveRenderFoamPreset() const;
    bool IsRenderCausticsEnabled() const;
    bool IsRenderMicroWavesEnabled() const;
    bool IsRenderGPUFoamEnabled() const;

    double WaveTimeSeconds = 0.0;
    double CurrentTurbulenceTimeSeconds = 0.0;
    bool bUseSynchronizedClock = false;
    double SynchronizedClockEpochServerSeconds = 0.0;
    double SynchronizedWaveTimeAtEpochSeconds = 0.0;
    double SynchronizedTurbulenceTimeAtEpochSeconds = 0.0;
    double SynchronizedWaveTimeScale = 1.0;
    double SynchronizedTurbulenceTimeScale = 1.0;
    double PreviousSunElevationDegrees = 90.0;
    bool bSunElevationHistoryValid = false;
    bool bSunWasRising = true;
    bool bConfigured = false;
    bool bStaticParametersDirty = true;
    bool bMaterialCausticsSuppressed = false;
    bool bFoamClipmapMaterialBound = false;
    ERealOceanProfilingVariant LocalRenderProfilingVariant = ERealOceanProfilingVariant::Full;
    bool bLocalRenderFoamPresetOverride = false;
    ERealOceanFoamClipmapQualityPreset LocalRenderFoamPreset = ERealOceanFoamClipmapQualityPreset::High512;

    FRealOceanSettings Settings;
    FRealOceanState State;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialParameterCollection> GlobalParameters = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> FoamClipmapRenderTarget = nullptr;

    TWeakObjectPtr<UMaterialInstanceDynamic> FoamClipmapBoundMaterial;
    TWeakObjectPtr<UObject> ConfigurationOwner;
    TWeakObjectPtr<AActor> TrackedActor;
    TWeakObjectPtr<ADirectionalLight> SunDirectionalLight;
};
