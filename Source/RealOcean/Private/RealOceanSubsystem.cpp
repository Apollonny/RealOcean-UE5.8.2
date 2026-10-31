#include "RealOceanSubsystem.h"

#include "Engine/World.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "WaterSubsystem.h"
#include "RealOceanOpticsLibrary.h"
#include "RealOceanWaveLibrary.h"
#include "RealOceanLightingLibrary.h"
#include "RealOceanRenderBridge.h"
#include "RealOceanReleaseConfig.h"
#include "RealOcean.h"
#include "RealOceanController.h"

namespace RealOceanMath
{
    static double Smooth01(double T)
    {
        T = FMath::Clamp(T, 0.0, 1.0);
        return T * T * (3.0 - 2.0 * T);
    }

    static FVector2D DirectionFromDegrees(double Degrees)
    {
        const double Radians = FMath::DegreesToRadians(Degrees);
        return FVector2D(FMath::Cos(Radians), FMath::Sin(Radians));
    }

    static FVector SafeNormalOrUp(const FVector& V)
    {
        const FVector N = V.GetSafeNormal();
        return N.IsNearlyZero() ? FVector::UpVector : N;
    }

    static double WrappedRadians(double Radians)
    {
        constexpr double TwoPi = 6.28318530717958647692;
        double Wrapped = FMath::Fmod(Radians, TwoPi);
        if (Wrapped < 0.0)
        {
            Wrapped += TwoPi;
        }
        return Wrapped;
    }
}

URealOceanSubsystem::URealOceanSubsystem()
{
}

void URealOceanSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    State.AbsolutePressureBar = Settings.SurfacePressureBar;
    WaveTimeSeconds = 0.0;
    CurrentTurbulenceTimeSeconds = 0.0;
    bUseSynchronizedClock = false;
    SynchronizedClockEpochServerSeconds = 0.0;
    SynchronizedWaveTimeAtEpochSeconds = 0.0;
    SynchronizedTurbulenceTimeAtEpochSeconds = 0.0;
    SynchronizedWaveTimeScale = 1.0;
    SynchronizedTurbulenceTimeScale = 1.0;
    PreviousSunElevationDegrees = 90.0;
    bSunElevationHistoryValid = false;
    bSunWasRising = true;
    bStaticParametersDirty = true;
}

void URealOceanSubsystem::Deinitialize()
{
    DeconfigureInternal();
    TrackedActor.Reset();
    FoamClipmapBoundMaterial.Reset();
    Super::Deinitialize();
}

void URealOceanSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);

    // Zero-placement bootstrap: authority creates one replicated automatic controller. Clients wait for
    // that replicated controller instead of creating an independent local ocean clock/owner. Dedicated
    // servers still create it so authoritative wave/current/pressure queries remain configured.
    if (InWorld.GetNetMode() == NM_Client)
    {
        return;
    }

    for (TActorIterator<ARealOceanController> It(&InWorld); It; ++It)
    {
        if (!It->IsActorBeingDestroyed())
        {
            return;
        }
    }

    SpawnAutomaticTurnkeyController();
}

ARealOceanController* URealOceanSubsystem::SpawnAutomaticTurnkeyController()
{
    UWorld* World = GetWorld();
    if (!World || World->GetNetMode() == NM_Client)
    {
        return nullptr;
    }

    ARealOceanController* Controller = World->SpawnActorDeferred<ARealOceanController>(
        ARealOceanController::StaticClass(),
        FTransform::Identity,
        nullptr,
        nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
        ESpawnActorScaleMethod::MultiplyWithRoot);
    if (Controller)
    {
        Controller->MarkAsTurnkeyAutoController();
        Controller->FinishSpawning(FTransform::Identity, true, nullptr, ESpawnActorScaleMethod::MultiplyWithRoot);
        UE_LOG(LogRealOcean, Display, TEXT("RealOcean turnkey bootstrap created an automatic replicated controller for %s."), *World->GetName());
    }
    else
    {
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean turnkey bootstrap failed to create its automatic controller for %s."), *World->GetName());
    }
    return Controller;
}

void URealOceanSubsystem::ScheduleTurnkeyControllerRecovery()
{
    UWorld* World = GetWorld();
    if (!World || World->GetNetMode() == NM_Client)
    {
        return;
    }

    TWeakObjectPtr<URealOceanSubsystem> WeakThis(this);
    World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakThis]()
    {
        if (URealOceanSubsystem* Subsystem = WeakThis.Get())
        {
            Subsystem->RecoverTurnkeyControllerOwnership();
        }
    }));
}

void URealOceanSubsystem::RecoverTurnkeyControllerOwnership()
{
    UWorld* World = GetWorld();
    if (!World || World->GetNetMode() == NM_Client || (bConfigured && ConfigurationOwner.IsValid()))
    {
        return;
    }

    ARealOceanController* BestExplicit = nullptr;
    ARealOceanController* BestAuto = nullptr;
    for (TActorIterator<ARealOceanController> It(World); It; ++It)
    {
        ARealOceanController* Candidate = *It;
        if (!Candidate || Candidate->IsActorBeingDestroyed())
        {
            continue;
        }

        ARealOceanController*& Best = Candidate->IsTurnkeyAutoController() ? BestAuto : BestExplicit;
        if (!Best || Candidate->GetPathName() < Best->GetPathName())
        {
            Best = Candidate;
        }
    }

    if (BestExplicit)
    {
        UE_LOG(LogRealOcean, Display, TEXT("RealOcean recovery promoting explicit controller %s after previous owner left."), *BestExplicit->GetName());
        BestExplicit->ApplySettings();
        return;
    }
    if (BestAuto)
    {
        BestAuto->ApplySettings();
        return;
    }
    SpawnAutomaticTurnkeyController();
}

void URealOceanSubsystem::Tick(float DeltaTime)
{
    if (bUseSynchronizedClock)
    {
        const double ServerNowSeconds = ResolveNetworkServerTimeSeconds();
        const double ElapsedSinceEpoch = FMath::Max(0.0, ServerNowSeconds - SynchronizedClockEpochServerSeconds);
        WaveTimeSeconds = SynchronizedWaveTimeAtEpochSeconds + ElapsedSinceEpoch * FMath::Max(0.0, SynchronizedWaveTimeScale);
        CurrentTurbulenceTimeSeconds = SynchronizedTurbulenceTimeAtEpochSeconds + ElapsedSinceEpoch * FMath::Max(0.0, SynchronizedTurbulenceTimeScale);
    }
    else
    {
        const double SafeDeltaSeconds = FMath::Max(0.0, static_cast<double>(DeltaTime));
        // Standalone/editor worlds integrate scaled frame deltas. Networked worlds use the replicated
        // server-time epoch above so frame rate and join time cannot change authoritative wave phase.
        WaveTimeSeconds += SafeDeltaSeconds * FMath::Max(0.0, Settings.Waves.TimeScale);
        CurrentTurbulenceTimeSeconds += SafeDeltaSeconds * FMath::Max(0.0, Settings.Current.TurbulenceTimeScale);
    }
    RecalculateState();
    PushMaterialParameters();
}

TStatId URealOceanSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(URealOceanSubsystem, STATGROUP_Tickables);
}

void URealOceanSubsystem::SetSynchronizedClock(
    double ServerEpochSeconds,
    double WaveTimeAtEpochSeconds,
    double TurbulenceTimeAtEpochSeconds,
    double WaveTimeScale,
    double TurbulenceTimeScale)
{
    bUseSynchronizedClock = true;
    SynchronizedClockEpochServerSeconds = ServerEpochSeconds;
    SynchronizedWaveTimeAtEpochSeconds = WaveTimeAtEpochSeconds;
    SynchronizedTurbulenceTimeAtEpochSeconds = TurbulenceTimeAtEpochSeconds;
    SynchronizedWaveTimeScale = FMath::Max(0.0, WaveTimeScale);
    SynchronizedTurbulenceTimeScale = FMath::Max(0.0, TurbulenceTimeScale);

    const double ServerNowSeconds = ResolveNetworkServerTimeSeconds();
    const double ElapsedSinceEpoch = FMath::Max(0.0, ServerNowSeconds - SynchronizedClockEpochServerSeconds);
    WaveTimeSeconds = SynchronizedWaveTimeAtEpochSeconds + ElapsedSinceEpoch * SynchronizedWaveTimeScale;
    CurrentTurbulenceTimeSeconds = SynchronizedTurbulenceTimeAtEpochSeconds + ElapsedSinceEpoch * SynchronizedTurbulenceTimeScale;
}

void URealOceanSubsystem::ClearSynchronizedClock()
{
    bUseSynchronizedClock = false;
}

void URealOceanSubsystem::Configure(
    const FRealOceanSettings& InSettings,
    UMaterialParameterCollection* InGlobalParameters,
    ADirectionalLight* InSunDirectionalLight)
{
    ConfigureInternal(nullptr, InSettings, InGlobalParameters, InSunDirectionalLight);
}

void URealOceanSubsystem::ConfigureForOwner(
    UObject* InConfigurationOwner,
    const FRealOceanSettings& InSettings,
    UMaterialParameterCollection* InGlobalParameters,
    ADirectionalLight* InSunDirectionalLight)
{
    ConfigureInternal(InConfigurationOwner, InSettings, InGlobalParameters, InSunDirectionalLight);
}

void URealOceanSubsystem::ConfigureInternal(
    UObject* InConfigurationOwner,
    const FRealOceanSettings& InSettings,
    UMaterialParameterCollection* InGlobalParameters,
    ADirectionalLight* InSunDirectionalLight)
{
    if (bConfigured && ConfigurationOwner.IsValid() && InConfigurationOwner && ConfigurationOwner.Get() != InConfigurationOwner)
    {
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean rejected a second configuration owner %s because %s already owns this world. Remove duplicate controllers or stream the current owner out first."),
            *GetNameSafe(InConfigurationOwner), *GetNameSafe(ConfigurationOwner.Get()));
        return;
    }

    Settings = InSettings;
    bMaterialCausticsSuppressed = false;

    // Project contract: exactly 3000 m. Prevent serialized older milestone values from leaking in.
    Settings.MaximumDepthMeters = 3000.0;

    ConfigurationOwner = InConfigurationOwner;
    GlobalParameters = InGlobalParameters;
    if (SunDirectionalLight.Get() != InSunDirectionalLight)
    {
        bSunElevationHistoryValid = false;
    }
    SunDirectionalLight = InSunDirectionalLight;
    bConfigured = true;
    EnsureFoamClipmapRenderTarget();
    RefreshFoamClipmapMaterialBinding();
    bStaticParametersDirty = true;
    RecalculateState();
    PushMaterialParameters();
}

void URealOceanSubsystem::DeconfigureForOwner(const UObject* InConfigurationOwner)
{
    if (!InConfigurationOwner || !ConfigurationOwner.IsValid() || ConfigurationOwner.Get() != InConfigurationOwner)
    {
        return;
    }
    DeconfigureInternal();
}

bool URealOceanSubsystem::ReleaseOwnerForHandoff(const UObject* InConfigurationOwner)
{
    if (!InConfigurationOwner || !ConfigurationOwner.IsValid() || ConfigurationOwner.Get() != InConfigurationOwner)
    {
        return false;
    }
    ConfigurationOwner.Reset();
    return true;
}

void URealOceanSubsystem::DeconfigureInternal()
{
    UWorld* World = GetWorld();
    FSceneInterface* Scene = World ? World->Scene : nullptr;

    // Explicitly neutralize runtime MPC values before dropping the collection reference. Otherwise a
    // streamed-out controller can leave frozen stateful foam/caustics/underwater state in materials.
    if (World && GlobalParameters)
    {
        if (UMaterialParameterCollectionInstance* MPC = World->GetParameterCollectionInstance(GlobalParameters))
        {
            MPC->SetScalarParameterValue(TEXT("RO_FoamClipmapValid"), 0.0f);
            MPC->SetScalarParameterValue(TEXT("RO_FoamClipmapBlend"), 0.0f);
            MPC->SetScalarParameterValue(TEXT("RO_CausticsEnabled"), 0.0f);
            MPC->SetScalarParameterValue(TEXT("RO_CausticsIntensity"), 0.0f);
            MPC->SetScalarParameterValue(TEXT("RO_CausticsStrength"), 0.0f);
            MPC->SetScalarParameterValue(TEXT("RO_UnderwaterBlend"), 0.0f);
            MPC->SetScalarParameterValue(TEXT("RO_DepthMeters"), 0.0f);
            MPC->SetScalarParameterValue(TEXT("RO_RawDepthMeters"), -1.0f);
        }
    }

    ReleaseFoamClipmapRenderTarget();
    RemoveRealOceanRenderBridge_GameThread(Scene);

    ConfigurationOwner.Reset();
    SunDirectionalLight.Reset();
    FoamClipmapBoundMaterial.Reset();
    GlobalParameters = nullptr;
    bConfigured = false;
    bStaticParametersDirty = true;
    bMaterialCausticsSuppressed = false;
    bFoamClipmapMaterialBound = false;
    LocalRenderProfilingVariant = ERealOceanProfilingVariant::Full;
    bLocalRenderFoamPresetOverride = false;
    LocalRenderFoamPreset = ERealOceanFoamClipmapQualityPreset::High512;
    State = FRealOceanState();
    State.AbsolutePressureBar = Settings.SurfacePressureBar;
    WaveTimeSeconds = 0.0;
    CurrentTurbulenceTimeSeconds = 0.0;
    bUseSynchronizedClock = false;
    SynchronizedClockEpochServerSeconds = 0.0;
    SynchronizedWaveTimeAtEpochSeconds = 0.0;
    SynchronizedTurbulenceTimeAtEpochSeconds = 0.0;
    PreviousSunElevationDegrees = 90.0;
    bSunElevationHistoryValid = false;
    bSunWasRising = true;
}

bool URealOceanSubsystem::BindFoamClipmapToMaterial(UMaterialInstanceDynamic* MaterialInstance)
{
    if (!MaterialInstance)
    {
        return false;
    }

    // Keep a weak binding target so quality/reconfigure changes that recreate the RT can rebind it
    // automatically instead of silently disabling stateful foam.
    FoamClipmapBoundMaterial = MaterialInstance;
    EnsureFoamClipmapRenderTarget();
    if (!FoamClipmapRenderTarget)
    {
        bFoamClipmapMaterialBound = false;
        bStaticParametersDirty = true;
        return false;
    }

#if UE_BUILD_SHIPPING
    if (RealOceanRelease::UsesGPUFoamClipmap())
    {
        const int32 ExpectedResolution = RealOceanRelease::GetCertifiedFoamResolution();
        if (FoamClipmapRenderTarget->SizeX != ExpectedResolution || FoamClipmapRenderTarget->SizeY != ExpectedResolution)
        {
            UE_LOG(LogRealOcean, Error, TEXT("RO_M32_GPUFOAM_BIND_FAIL expected_resolution=%d actual=%dx%d"),
                ExpectedResolution, FoamClipmapRenderTarget->SizeX, FoamClipmapRenderTarget->SizeY);
            ReleaseFoamClipmapRenderTarget();
            return false;
        }
    }
#endif

    const bool bWasBound = bFoamClipmapMaterialBound;
    RefreshFoamClipmapMaterialBinding();
    if (!bFoamClipmapMaterialBound)
    {
        return false;
    }

    bStaticParametersDirty = true;
    PushMaterialParameters();
#if UE_BUILD_SHIPPING
    if (!bWasBound && RealOceanRelease::UsesGPUFoamClipmap())
    {
        UE_LOG(LogRealOcean, Display, TEXT("RO_M32_GPUFOAM_READY bundle=%s preset=%s resolution=%d rate_hz=%d bound=1"),
            *RealOceanRelease::GetReleaseBundleId(), RealOceanRelease::GetCertifiedFoamPresetName(),
            ResolveFoamClipmapResolution(), FMath::RoundToInt(ResolveFoamClipmapUpdateRateHz()));
    }
#endif
    return true;
}

void URealOceanSubsystem::EnsureFoamClipmapRenderTarget()
{
    const bool bWantsClipmap = bConfigured
        && IsRenderGPUFoamEnabled()
        && Settings.QualityMode == ERealOceanQualityMode::Cinematic;
    if (!bWantsClipmap)
    {
        ReleaseFoamClipmapRenderTarget();
        return;
    }

    const int32 Resolution = ResolveFoamClipmapResolution();

    if (FoamClipmapRenderTarget && FoamClipmapRenderTarget->SizeX == Resolution && FoamClipmapRenderTarget->SizeY == Resolution)
    {
        RefreshFoamClipmapMaterialBinding();
        return;
    }

    ReleaseFoamClipmapRenderTarget();
    FoamClipmapRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("RealOceanFoamClipmapRT"));
    FoamClipmapRenderTarget->ClearColor = FLinearColor::Black;
    FoamClipmapRenderTarget->bAutoGenerateMips = false;
    FoamClipmapRenderTarget->InitCustomFormat(Resolution, Resolution, PF_R16F, false);
    FoamClipmapRenderTarget->UpdateResourceImmediate(true);

    UWorld* World = GetWorld();
    PushRealOceanFoamClipmapResource_GameThread(
        World ? World->Scene : nullptr,
        FoamClipmapRenderTarget->GameThread_GetRenderTargetResource());
    RefreshFoamClipmapMaterialBinding();
}

void URealOceanSubsystem::RefreshFoamClipmapMaterialBinding()
{
    UMaterialInstanceDynamic* MaterialInstance = FoamClipmapBoundMaterial.Get();
    if (!MaterialInstance || !FoamClipmapRenderTarget)
    {
        bFoamClipmapMaterialBound = false;
        return;
    }

    MaterialInstance->SetTextureParameterValue(TEXT("RO_FoamClipmapTexture"), FoamClipmapRenderTarget);
    // K2_GetTextureParameterValue returns null when the named parameter does not exist. Do not mark the
    // clipmap as ready (or spend its compute cost) unless the material actually accepted the binding.
    UTexture* BoundTexture = MaterialInstance->K2_GetTextureParameterValue(TEXT("RO_FoamClipmapTexture"));
    bFoamClipmapMaterialBound = (BoundTexture == FoamClipmapRenderTarget);
    if (!bFoamClipmapMaterialBound)
    {
        UE_LOG(LogRealOcean, Error, TEXT("RO_M32_GPUFOAM_BIND_FAIL missing_or_rejected_parameter=RO_FoamClipmapTexture material=%s"),
            *GetNameSafe(MaterialInstance));
    }
}

void URealOceanSubsystem::ReleaseFoamClipmapRenderTarget()
{
    if (FoamClipmapRenderTarget)
    {
        UWorld* World = GetWorld();
        PushRealOceanFoamClipmapResource_GameThread(World ? World->Scene : nullptr, nullptr);
        FoamClipmapRenderTarget = nullptr;
    }
    bFoamClipmapMaterialBound = false;
    bStaticParametersDirty = true;
}

int32 URealOceanSubsystem::ResolveFoamClipmapResolution() const
{
#if UE_BUILD_SHIPPING
    if (RealOceanRelease::UsesGPUFoamClipmap())
    {
        return RealOceanRelease::GetCertifiedFoamResolution();
    }
#endif
    switch (ResolveRenderFoamPreset())
    {
    case ERealOceanFoamClipmapQualityPreset::Low256:
        return 256;
    case ERealOceanFoamClipmapQualityPreset::Medium384:
        return 384;
    case ERealOceanFoamClipmapQualityPreset::High512:
        return 512;
    case ERealOceanFoamClipmapQualityPreset::Custom:
    default:
        return FMath::Clamp(FMath::RoundToInt(static_cast<double>(Settings.Surface.FoamClipmapResolution) / 64.0) * 64, 128, 1024);
    }
}

double URealOceanSubsystem::ResolveFoamClipmapUpdateRateHz() const
{
#if UE_BUILD_SHIPPING
    if (RealOceanRelease::UsesGPUFoamClipmap())
    {
        return static_cast<double>(RealOceanRelease::GetCertifiedFoamUpdateRateHz());
    }
#endif
    switch (ResolveRenderFoamPreset())
    {
    case ERealOceanFoamClipmapQualityPreset::Low256:
        return 30.0;
    case ERealOceanFoamClipmapQualityPreset::Medium384:
        return 45.0;
    case ERealOceanFoamClipmapQualityPreset::High512:
        return 60.0;
    case ERealOceanFoamClipmapQualityPreset::Custom:
    default:
        return FMath::Clamp(Settings.Surface.FoamClipmapUpdateRateHz, 10.0, 120.0);
    }
}

FVector2D URealOceanSubsystem::CalculateFoamClipmapCenterOffsetMeters() const
{
    const int32 Resolution = ResolveFoamClipmapResolution();
    const double CoverageMeters = FMath::Clamp(Settings.Surface.FoamClipmapCoverageMeters, 64.0, 1024.0);
    const double TexelMeters = CoverageMeters / static_cast<double>(Resolution);
    const FVector2D CameraMeters(State.RenderCameraWorldPositionCm.X * 0.01, State.RenderCameraWorldPositionCm.Y * 0.01);
    const FVector2D CenterMeters(
        FMath::RoundToDouble(CameraMeters.X / TexelMeters) * TexelMeters,
        FMath::RoundToDouble(CameraMeters.Y / TexelMeters) * TexelMeters);
    return CenterMeters - CameraMeters;
}

void URealOceanSubsystem::SetTrackedActor(AActor* InActor)
{
    TrackedActor = InActor;
}

void URealOceanSubsystem::SetSunDirectionalLight(ADirectionalLight* InSunDirectionalLight)
{
    if (SunDirectionalLight.Get() != InSunDirectionalLight)
    {
        bSunElevationHistoryValid = false;
    }
    SunDirectionalLight = InSunDirectionalLight;
    RecalculateState();
    PushMaterialParameters();
}

void URealOceanSubsystem::SetMaterialCausticsSuppressed(bool bSuppressed)
{
#if UE_BUILD_SHIPPING
    // Shipping release selection is authoritative. M31 accidentally forced this false, which
    // double-counted material caustics when a certified RDG caustics path was compiled.
    bSuppressed = RealOceanRelease::UsesRDGCaustics();
#endif
    if (bMaterialCausticsSuppressed == bSuppressed)
    {
        return;
    }

    bMaterialCausticsSuppressed = bSuppressed;
    bStaticParametersDirty = true;
    PushMaterialParameters();
}

void URealOceanSubsystem::SetLocalRenderProfilingOverrides(
    ERealOceanProfilingVariant Variant,
    bool bOverrideFoamPreset,
    ERealOceanFoamClipmapQualityPreset FoamPreset)
{
#if UE_BUILD_SHIPPING
    Variant = ERealOceanProfilingVariant::Full;
    bOverrideFoamPreset = false;
#endif

    const bool bChanged = LocalRenderProfilingVariant != Variant
        || bLocalRenderFoamPresetOverride != bOverrideFoamPreset
        || (bOverrideFoamPreset && LocalRenderFoamPreset != FoamPreset);
    if (!bChanged)
    {
        return;
    }

    LocalRenderProfilingVariant = Variant;
    bLocalRenderFoamPresetOverride = bOverrideFoamPreset;
    LocalRenderFoamPreset = FoamPreset;
    bStaticParametersDirty = true;

    // Foam preset/enablement is a local render concern. Recreate/release the transient RT as
    // needed without ever modifying the authoritative Settings used for simulation/query state.
    EnsureFoamClipmapRenderTarget();
    RefreshFoamClipmapMaterialBinding();
    RecalculateState();
    PushMaterialParameters();
}

ERealOceanFoamClipmapQualityPreset URealOceanSubsystem::ResolveRenderFoamPreset() const
{
#if !UE_BUILD_SHIPPING
    if (bLocalRenderFoamPresetOverride)
    {
        return LocalRenderFoamPreset;
    }
#endif
    return Settings.Surface.FoamClipmapQualityPreset;
}

bool URealOceanSubsystem::IsRenderCausticsEnabled() const
{
    if (!Settings.Caustics.bEnabled)
    {
        return false;
    }
#if !UE_BUILD_SHIPPING
    return LocalRenderProfilingVariant != ERealOceanProfilingVariant::NoCaustics
        && LocalRenderProfilingVariant != ERealOceanProfilingVariant::SurfaceOnly
        && LocalRenderProfilingVariant != ERealOceanProfilingVariant::MacroSurfaceOnly;
#else
    return true;
#endif
}

bool URealOceanSubsystem::IsRenderMicroWavesEnabled() const
{
#if !UE_BUILD_SHIPPING
    return LocalRenderProfilingVariant != ERealOceanProfilingVariant::NoMicroWaves
        && LocalRenderProfilingVariant != ERealOceanProfilingVariant::MacroSurfaceOnly;
#else
    return true;
#endif
}

bool URealOceanSubsystem::IsRenderGPUFoamEnabled() const
{
    if (!Settings.Surface.bEnableGPUFoamClipmap)
    {
        return false;
    }
#if !UE_BUILD_SHIPPING
    return LocalRenderProfilingVariant != ERealOceanProfilingVariant::NoGPUFoam;
#else
    return RealOceanRelease::UsesGPUFoamClipmap();
#endif
}

ERealOceanDepthZone URealOceanSubsystem::ClassifyDepth(double DepthMeters)
{
    if (DepthMeters <= 0.0) return ERealOceanDepthZone::AboveSurface;
    if (DepthMeters < 200.0) return ERealOceanDepthZone::Epipelagic;
    if (DepthMeters < 1000.0) return ERealOceanDepthZone::Mesopelagic;
    return ERealOceanDepthZone::Bathypelagic;
}

double URealOceanSubsystem::CalculateGaugePressureBar(double DepthMeters, double DensityKgM3, double GravityMS2)
{
    const double ClampedDepth = FMath::Max(0.0, DepthMeters);
    const double PressurePa = DensityKgM3 * GravityMS2 * ClampedDepth;
    return PressurePa / 100000.0;
}

double URealOceanSubsystem::QueryTemperatureC(double DepthMeters) const
{
    const double D = FMath::Clamp(DepthMeters, 0.0, Settings.MaximumDepthMeters);
    const double Width = FMath::Max(1.0, Settings.Hydrography.ThermoclineWidthMeters);
    const double Start = Settings.Hydrography.ThermoclineCenterMeters - 0.5 * Width;
    const double T = RealOceanMath::Smooth01((D - Start) / Width);
    return FMath::Lerp(Settings.Hydrography.SurfaceTemperatureC, Settings.Hydrography.DeepTemperatureC, T);
}

double URealOceanSubsystem::QuerySalinityPSU(double DepthMeters) const
{
    const double D = FMath::Clamp(DepthMeters, 0.0, Settings.MaximumDepthMeters);
    const double Width = FMath::Max(1.0, Settings.Hydrography.ThermoclineWidthMeters);
    const double Start = Settings.Hydrography.ThermoclineCenterMeters - 0.5 * Width;
    const double T = RealOceanMath::Smooth01((D - Start) / Width);
    return FMath::Lerp(Settings.Hydrography.SurfaceSalinityPSU, Settings.Hydrography.DeepSalinityPSU, T);
}

double URealOceanSubsystem::EstimateDensityKgM3(double DepthMeters) const
{
    // Lightweight gameplay/environment estimate, not an oceanographic equation of state.
    // Salinity and temperature coefficients are first-order approximations around the configured reference,
    // while the pressure term uses a representative seawater bulk modulus (~2.2 GPa).
    const double D = FMath::Clamp(DepthMeters, 0.0, Settings.MaximumDepthMeters);
    const double TemperatureC = QueryTemperatureC(D);
    const double SalinityPSU = QuerySalinityPSU(D);
    const double BaseDensity = FMath::Max(900.0, Settings.SeawaterDensityKgM3);
    const double ThermalDelta = -0.20 * (TemperatureC - Settings.Hydrography.ReferenceTemperatureC);
    const double SalinityDelta = 0.78 * (SalinityPSU - Settings.Hydrography.ReferenceSalinityPSU);
    const double GaugePressurePa = BaseDensity * FMath::Max(0.0, Settings.GravityMS2) * D;
    constexpr double RepresentativeBulkModulusPa = 2.2e9;
    const double CompressionDelta = BaseDensity * GaugePressurePa / RepresentativeBulkModulusPa;
    return FMath::Clamp(BaseDensity + ThermalDelta + SalinityDelta + CompressionDelta, 900.0, 1150.0);
}

FVector URealOceanSubsystem::QueryCurrentVelocityMetersPerSecond(FVector WorldPositionCm) const
{
    const double SeaLevelCm = ResolveSeaLevelCm();
    const FVector2D WorldXYMeters(WorldPositionCm.X * 0.01, WorldPositionCm.Y * 0.01);
    const FRealOceanWaveSample Surface = URealOceanWaveLibrary::EvaluateSurfaceAtWorldXY(
        WorldXYMeters,
        State.OceanTimeSeconds,
        Settings.Waves,
        Settings.GravityMS2,
        4);
    const double DynamicSurfaceHeightMeters = SeaLevelCm * 0.01 + Surface.HeightMeters;
    const double DistanceBelowSurfaceMeters = DynamicSurfaceHeightMeters - WorldPositionCm.Z * 0.01;
    if (DistanceBelowSurfaceMeters < 0.0)
    {
        return FVector::ZeroVector;
    }

    // Keep the configured large-scale current profile referenced to mean sea level so orbital wave
    // motion does not pump the current speed every crest/trough. Dynamic surface height is used only
    // for the physically important wet/dry classification above.
    const double RawMeanDepthMeters = (SeaLevelCm - WorldPositionCm.Z) * 0.01;
    const double MeanDepthMeters = FMath::Clamp(RawMeanDepthMeters, 0.0, Settings.MaximumDepthMeters);
    const double Width = FMath::Max(1.0, Settings.Current.TransitionWidthMeters);
    const double Start = Settings.Current.TransitionDepthMeters - 0.5 * Width;
    const double T = RealOceanMath::Smooth01((MeanDepthMeters - Start) / Width);

    const FVector2D SurfaceDir = RealOceanMath::DirectionFromDegrees(Settings.Current.SurfaceDirectionDegrees);
    const FVector2D DeepDir = RealOceanMath::DirectionFromDegrees(Settings.Current.DeepDirectionDegrees);
    const FVector SurfaceVelocity(SurfaceDir.X * Settings.Current.SurfaceSpeedMetersPerSecond, SurfaceDir.Y * Settings.Current.SurfaceSpeedMetersPerSecond, 0.0);
    const FVector DeepVelocity(DeepDir.X * Settings.Current.DeepSpeedMetersPerSecond, DeepDir.Y * Settings.Current.DeepSpeedMetersPerSecond, 0.0);
    FVector Velocity = FMath::Lerp(SurfaceVelocity, DeepVelocity, T);

    const double Strength = FMath::Max(0.0, Settings.Current.TurbulenceSpeedMetersPerSecond);
    if (Strength > 0.0)
    {
        constexpr double TwoPi = 6.28318530717958647692;
        const double Scale = FMath::Max(1.0, Settings.Current.TurbulenceScaleMeters);
        const FVector WorldMeters = WorldPositionCm * 0.01;
        const double CurrentTime = CurrentTurbulenceTimeSeconds;
        const double PX = RealOceanMath::WrappedRadians(TwoPi * WorldMeters.X / Scale + CurrentTime * 0.025);
        const double PY = RealOceanMath::WrappedRadians(TwoPi * WorldMeters.Y / (Scale * 1.37) - CurrentTime * 0.019);
        const double PZ = RealOceanMath::WrappedRadians(TwoPi * MeanDepthMeters / (Scale * 0.83) + CurrentTime * 0.011);

        const double NX = FMath::Sin(PX + 0.71 * FMath::Sin(PY));
        const double NY = FMath::Cos(PY - 0.53 * FMath::Sin(PX));
        const double NZ = 0.15 * FMath::Sin(PZ + 0.37 * FMath::Sin(PX + PY));
        Velocity += FVector(NX, NY, NZ) * Strength;
    }

    return Velocity;
}

FRealOceanEnvironmentSample URealOceanSubsystem::QueryEnvironmentAtWorldPosition(FVector WorldPositionCm) const
{
    FRealOceanEnvironmentSample Sample;
    Sample.WorldPositionCm = WorldPositionCm;

    const double SeaLevelCm = ResolveSeaLevelCm();
    const FVector2D WorldXYMeters(WorldPositionCm.X * 0.01, WorldPositionCm.Y * 0.01);
    const FRealOceanWaveSample Surface = URealOceanWaveLibrary::EvaluateSurfaceAtWorldXY(
        WorldXYMeters,
        State.OceanTimeSeconds,
        Settings.Waves,
        Settings.GravityMS2,
        4);

    Sample.SurfaceWorldHeightMeters = SeaLevelCm * 0.01 + Surface.HeightMeters;
    Sample.SurfaceNormal = Surface.Normal;
    Sample.DistanceToSurfaceMeters = Sample.SurfaceWorldHeightMeters - WorldPositionCm.Z * 0.01;
    Sample.MeanSeaDepthMeters = (SeaLevelCm - WorldPositionCm.Z) * 0.01;
    Sample.DynamicSurfaceDepthMeters = Sample.DistanceToSurfaceMeters;
    Sample.RawDepthMeters = Sample.DynamicSurfaceDepthMeters;
    Sample.bUnderwater = Sample.DynamicSurfaceDepthMeters > 0.0;
    Sample.bBelowConfiguredBottom = Sample.MeanSeaDepthMeters > Settings.MaximumDepthMeters;
    Sample.DepthMeters = FMath::Clamp(FMath::Max(0.0, Sample.DynamicSurfaceDepthMeters), 0.0, Settings.MaximumDepthMeters);
    Sample.DepthZone = ClassifyDepth(Sample.DepthMeters);
    Sample.TemperatureC = QueryTemperatureC(Sample.DepthMeters);
    Sample.SalinityPSU = QuerySalinityPSU(Sample.DepthMeters);
    Sample.EstimatedDensityKgM3 = EstimateDensityKgM3(Sample.DepthMeters);
    Sample.AbsolutePressureBar = Settings.SurfacePressureBar + CalculateGaugePressureBar(
        Sample.DepthMeters,
        Settings.SeawaterDensityKgM3,
        Settings.GravityMS2);
    Sample.CurrentVelocityMetersPerSecond = Sample.bUnderwater
        ? QueryCurrentVelocityMetersPerSecond(WorldPositionCm)
        : FVector::ZeroVector;
    return Sample;
}

FVector URealOceanSubsystem::ResolveTrackedPositionCm() const
{
    if (TrackedActor.IsValid())
    {
        return TrackedActor->GetActorLocation();
    }

    if (UWorld* World = GetWorld())
    {
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            if (const APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
            {
                return CameraManager->GetCameraLocation();
            }

            if (const APawn* Pawn = PC->GetPawn())
            {
                return Pawn->GetActorLocation();
            }
        }
    }

    return FVector::ZeroVector;
}

FVector URealOceanSubsystem::ResolveRenderCameraPositionCm() const
{
    // This position is no longer the visual phase anchor for generated materials: 0.33.4 derives wave
    // anchors/depth from native per-view CameraPositionWS. The primary camera remains useful for gameplay
    // diagnostics and for centering the one shared world-space persistent foam clipmap.
    if (UWorld* World = GetWorld())
    {
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            if (const APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
            {
                return CameraManager->GetCameraLocation();
            }
        }
    }

    return ResolveTrackedPositionCm();
}

double URealOceanSubsystem::ResolveNetworkServerTimeSeconds() const
{
    if (const UWorld* World = GetWorld())
    {
        if (const AGameStateBase* GameState = World->GetGameState())
        {
            return static_cast<double>(GameState->GetServerWorldTimeSeconds());
        }

        if (World->GetNetMode() == NM_Client)
        {
            // A join-in-progress client without GameState time synchronization must never substitute
            // its unrelated local World time. Hold the replicated epoch until GameState appears; the
            // next tick then resolves directly to authoritative server time instead of accumulating drift.
            return bUseSynchronizedClock ? SynchronizedClockEpochServerSeconds : 0.0;
        }
        return static_cast<double>(World->GetTimeSeconds());
    }
    return 0.0;
}

double URealOceanSubsystem::ResolveSeaLevelCm() const
{
    if (Settings.bUseWaterSubsystemSeaLevel)
    {
        if (UWorld* World = GetWorld())
        {
            if (UWaterSubsystem* WaterSubsystem = World->GetSubsystem<UWaterSubsystem>())
            {
                return static_cast<double>(WaterSubsystem->GetOceanBaseHeight());
            }
        }
    }

    return Settings.ManualSeaLevelCm;
}

void URealOceanSubsystem::ResolveQualityBudget()
{
    const double ConfiguredMaxDepth = FMath::Max(0.0, Settings.Caustics.MaxDepthMeters);

    switch (Settings.QualityMode)
    {
        case ERealOceanQualityMode::Performance:
            State.EffectiveCausticsMaxDepthMeters = FMath::Min(ConfiguredMaxDepth, 60.0);
            State.CausticsSolveIterations = 1;
            State.CausticsDerivativeScale = 1.75;
            break;

        case ERealOceanQualityMode::Balanced:
            State.EffectiveCausticsMaxDepthMeters = FMath::Min(ConfiguredMaxDepth, 90.0);
            State.CausticsSolveIterations = 2;
            State.CausticsDerivativeScale = 1.0;
            break;

        default:
            State.EffectiveCausticsMaxDepthMeters = ConfiguredMaxDepth;
            State.CausticsSolveIterations = 3;
            State.CausticsDerivativeScale = 0.75;
            break;
    }
}


void URealOceanSubsystem::ResolveWaterIntegrationState()
{
    State.bWaterSubsystemAvailable = false;
    State.bWaterRenderingEnabled = false;
    State.bOceanBodyAvailable = false;
    State.WaterSubsystemOceanBaseHeightCm = 0.0;

    if (UWorld* World = GetWorld())
    {
        if (UWaterSubsystem* WaterSubsystem = World->GetSubsystem<UWaterSubsystem>())
        {
            State.bWaterSubsystemAvailable = true;
            State.bWaterRenderingEnabled = WaterSubsystem->IsWaterRenderingEnabled();
            State.bOceanBodyAvailable = WaterSubsystem->GetOceanBodyComponent().IsValid();
            State.WaterSubsystemOceanBaseHeightCm = static_cast<double>(WaterSubsystem->GetOceanBaseHeight());
        }
    }
}

void URealOceanSubsystem::ResolveSunState()
{
    FVector DirectionToSource = Settings.Lighting.ManualSunDirectionToSourceWS;
    FLinearColor RawSunColor = Settings.Lighting.ManualSunColor;
    double RawSunIntensity = Settings.Lighting.ManualSunIntensity;

    if (SunDirectionalLight.IsValid())
    {
        // Epic's Directional Light arrow/forward direction is the direction light is emitted.
        // RealOcean stores the opposite: the direction from a water point toward the sun/source.
        DirectionToSource = -SunDirectionalLight->GetActorForwardVector();

        if (UDirectionalLightComponent* LightComponent = SunDirectionalLight->FindComponentByClass<UDirectionalLightComponent>())
        {
            DirectionToSource = -LightComponent->GetForwardVector();
            RawSunColor = LightComponent->GetLightColor();
            RawSunIntensity = LightComponent->IsVisible() ? static_cast<double>(LightComponent->Intensity) : 0.0;
        }
    }

    State.SunDirectionToSourceWS = RealOceanMath::SafeNormalOrUp(DirectionToSource);
    const double SignedSunCosZenithAir = FMath::Clamp(
        FVector::DotProduct(State.SunDirectionToSourceWS, FVector::UpVector), -1.0, 1.0);
    State.SunElevationDegrees = FMath::RadiansToDegrees(FMath::Asin(SignedSunCosZenithAir));

    if (bSunElevationHistoryValid)
    {
        const double ElevationDelta = State.SunElevationDegrees - PreviousSunElevationDegrees;
        if (FMath::Abs(ElevationDelta) > 1.0e-5)
        {
            bSunWasRising = ElevationDelta > 0.0;
        }
    }
    PreviousSunElevationDegrees = State.SunElevationDegrees;
    bSunElevationHistoryValid = true;
    State.bSunRising = bSunWasRising;

    const double TwilightStart = FMath::Clamp(Settings.Lighting.TwilightStartElevationDegrees, -18.0, 0.0);
    const double FullDaylight = FMath::Max(1.0, Settings.Lighting.FullDaylightElevationDegrees);
    const double GoldenHourEnd = FMath::Max(5.0, Settings.Lighting.GoldenHourEndElevationDegrees);
    State.DaylightFactor = RealOceanMath::Smooth01(
        (State.SunElevationDegrees - TwilightStart) / FMath::Max(FullDaylight - TwilightStart, 0.01));

    if (State.SunElevationDegrees > 0.0)
    {
        const double GoldenEntry = RealOceanMath::Smooth01(State.SunElevationDegrees / 2.0);
        const double GoldenFadeStart = GoldenHourEnd * 0.55;
        const double GoldenExit = 1.0 - RealOceanMath::Smooth01(
            (State.SunElevationDegrees - GoldenFadeStart) / FMath::Max(GoldenHourEnd - GoldenFadeStart, 0.01));
        State.GoldenHourFactor = FMath::Clamp(GoldenEntry * GoldenExit, 0.0, 1.0);
    }
    else
    {
        State.GoldenHourFactor = 0.0;
    }

    if (State.SunElevationDegrees <= TwilightStart)
    {
        State.DayPhase = ERealOceanDayPhase::Night;
    }
    else if (State.SunElevationDegrees < 4.0)
    {
        State.DayPhase = bSunWasRising ? ERealOceanDayPhase::Dawn : ERealOceanDayPhase::Dusk;
    }
    else if (State.SunElevationDegrees < 35.0)
    {
        State.DayPhase = bSunWasRising ? ERealOceanDayPhase::Morning : ERealOceanDayPhase::Evening;
    }
    else
    {
        State.DayPhase = ERealOceanDayPhase::Midday;
    }

    // M35 day-cycle ambient response. Twilight retains a reduced amount of underwater inscatter,
    // then smoothly reaches the configured daytime value as the sun rises. This prevents the ocean
    // volume from glowing with noon-level ambient energy after sunset without inventing moonlight.
    double AmbientScale = 1.0;
    if (Settings.Lighting.bEnableElevationDrivenDayCycle)
    {
        const double NightScale = FMath::Clamp(Settings.Lighting.NightInscatterScale, 0.0, 1.0);
        const double TwilightScale = FMath::Clamp(Settings.Lighting.TwilightInscatterScale, NightScale, 1.0);
        if (State.SunElevationDegrees <= TwilightStart)
        {
            AmbientScale = NightScale;
        }
        else if (State.SunElevationDegrees < 0.0)
        {
            const double TwilightT = RealOceanMath::Smooth01(
                (State.SunElevationDegrees - TwilightStart) / FMath::Max(-TwilightStart, 0.01));
            AmbientScale = FMath::Lerp(NightScale, TwilightScale, TwilightT);
        }
        else
        {
            const double DayT = RealOceanMath::Smooth01(State.SunElevationDegrees / FullDaylight);
            AmbientScale = FMath::Lerp(TwilightScale, 1.0, DayT);
        }
    }
    State.EffectiveInscatterIntensity = FMath::Max(0.0, Settings.InscatterIntensity) * AmbientScale;

    // Preserve the product (solar color * relative intensity) while using air-mass attenuation to
    // warm and dim only RealOcean's custom sun paths at low elevation. This avoids altering the
    // project's actual Directional Light or SkyAtmosphere state.
    FLinearColor EffectiveSunColor = RawSunColor;
    double EffectiveSunIntensity = FMath::Max(0.0, RawSunIntensity * Settings.Lighting.SunIntensityScale);
    State.DayCycleAtmosphereTransmissionRGB = FVector::OneVector;
    if (Settings.Lighting.bEnableElevationDrivenDayCycle)
    {
        State.DayCycleAtmosphereTransmissionRGB = URealOceanLightingLibrary::CalculateDayCycleAtmosphereTransmissionRGB(
            State.SunElevationDegrees,
            Settings.Lighting.DayCycleAtmosphericOpticalDepthRGB,
            Settings.Lighting.DayCycleAtmosphereStrength);

        if (State.SunElevationDegrees <= 0.0)
        {
            EffectiveSunIntensity = 0.0;
        }
        else
        {
            const double AtmosphereLuminance = FMath::Max(
                URealOceanLightingLibrary::CalculateLinearLuminance(State.DayCycleAtmosphereTransmissionRGB), 1.0e-4);
            const FVector Chroma = State.DayCycleAtmosphereTransmissionRGB / AtmosphereLuminance;
            EffectiveSunColor.R *= static_cast<float>(FMath::Clamp(Chroma.X, 0.0, 4.0));
            EffectiveSunColor.G *= static_cast<float>(FMath::Clamp(Chroma.Y, 0.0, 4.0));
            EffectiveSunColor.B *= static_cast<float>(FMath::Clamp(Chroma.Z, 0.0, 4.0));
            EffectiveSunIntensity *= AtmosphereLuminance;
        }
    }

    State.SunColor = EffectiveSunColor;
    State.SunIntensity = EffectiveSunIntensity;
    const double ReferenceIntensity = FMath::Max(1.0, Settings.Lighting.ReferenceSunIntensity);
    State.SunRelativeIntensity = FMath::Clamp(State.SunIntensity / ReferenceIntensity, 0.0, 16.0);
    State.SunCosZenithAir = FMath::Clamp(SignedSunCosZenithAir, 0.0, 1.0);

    State.SunCosZenithWater = URealOceanLightingLibrary::CalculateRefractedSunCosineWater(
        State.SunCosZenithAir,
        Settings.Surface.AirRefractiveIndex,
        Settings.Surface.WaterRefractiveIndex);

    if (State.SunCosZenithAir <= 0.0 || State.SunIntensity <= 0.0)
    {
        State.DirectSunPathMeters = 0.0;
        State.SunTransmissionRGB = FVector::ZeroVector;
        State.SunlightTransmission = 0.0;
        return;
    }

    State.DirectSunPathMeters = URealOceanLightingLibrary::CalculateDirectSunPathMeters(
        State.DepthMeters,
        State.SunCosZenithAir,
        Settings.Surface.AirRefractiveIndex,
        Settings.Surface.WaterRefractiveIndex);

    State.SunTransmissionRGB = URealOceanLightingLibrary::CalculateSpectralSunTransmission(
        State.EffectiveAbsorptionRGBPerMeter,
        State.EffectiveScatteringRGBPerMeter,
        State.DirectSunPathMeters);
    const double InterfaceTransmission = URealOceanLightingLibrary::CalculateSunInterfaceTransmissionAirToWater(
        State.SunCosZenithAir,
        Settings.Surface.AirRefractiveIndex,
        Settings.Surface.WaterRefractiveIndex);
    State.SunTransmissionRGB *= InterfaceTransmission;

    State.SunlightTransmission = FMath::Clamp(
        URealOceanLightingLibrary::CalculateLinearLuminance(State.SunTransmissionRGB),
        0.0,
        1.0);
}

void URealOceanSubsystem::RecalculateState()
{
    // WaveTimeSeconds is integrated from scaled frame deltas in Tick(). Assign it before any CPU
    // wave/current query so every runtime path samples one continuous wave clock.
    State.OceanTimeSeconds = WaveTimeSeconds;

    const FVector CameraPositionCm = ResolveTrackedPositionCm();
    const FVector RenderCameraPositionCm = ResolveRenderCameraPositionCm();
    const double SeaLevelCm = ResolveSeaLevelCm();
    const FRealOceanWaveSample CameraSurface = URealOceanWaveLibrary::EvaluateSurfaceAtWorldXY(
        FVector2D(CameraPositionCm.X, CameraPositionCm.Y) * 0.01,
        State.OceanTimeSeconds,
        Settings.Waves,
        Settings.GravityMS2,
        4);
    const double DynamicSurfaceHeightMeters = SeaLevelCm * 0.01 + CameraSurface.HeightMeters;
    const double DynamicSurfaceDepthMeters = DynamicSurfaceHeightMeters - CameraPositionCm.Z * 0.01;
    const double MeanSeaDepthMeters = (SeaLevelCm - CameraPositionCm.Z) * 0.01;
    const double WaterColumnDepthMeters = FMath::Clamp(DynamicSurfaceDepthMeters, 0.0, Settings.MaximumDepthMeters);

    const ERealOceanDepthZone PreviousZone = State.DepthZone;

    State.CameraWorldPositionCm = CameraPositionCm;
    State.RenderCameraWorldPositionCm = RenderCameraPositionCm;
    State.SeaLevelCm = SeaLevelCm;
    State.MeanSeaDepthMeters = MeanSeaDepthMeters;
    State.DynamicSurfaceDepthMeters = DynamicSurfaceDepthMeters;
    State.RawDepthMeters = DynamicSurfaceDepthMeters;
    State.DepthMeters = WaterColumnDepthMeters;
    // The configured 3000 m bottom is referenced to mean sea level; wave elevation only changes
    // the instantaneous water column above the camera, not the geological bottom plane.
    State.bBelowConfiguredBottom = MeanSeaDepthMeters > Settings.MaximumDepthMeters;
    State.NormalizedDepth = FMath::Clamp(WaterColumnDepthMeters / Settings.MaximumDepthMeters, 0.0, 1.0);
    State.GaugePressureBar = CalculateGaugePressureBar(WaterColumnDepthMeters, Settings.SeawaterDensityKgM3, Settings.GravityMS2);
    State.AbsolutePressureBar = Settings.SurfacePressureBar + State.GaugePressureBar;
    State.WaterTemperatureC = QueryTemperatureC(WaterColumnDepthMeters);
    State.SalinityPSU = QuerySalinityPSU(WaterColumnDepthMeters);
    State.EstimatedDensityKgM3 = EstimateDensityKgM3(WaterColumnDepthMeters);
    State.CurrentVelocityMetersPerSecond = QueryCurrentVelocityMetersPerSecond(CameraPositionCm);
    State.QualityMode = Settings.QualityMode;
    State.DebugView = Settings.DebugView;

    ResolveWaterIntegrationState();

    const double TransitionMeters = FMath::Max(0.01, Settings.UnderwaterTransitionMeters);
    State.UnderwaterBlend = RealOceanMath::Smooth01(FMath::Max(0.0, DynamicSurfaceDepthMeters) / TransitionMeters);

    URealOceanOpticsLibrary::CalculateTurbidityAdjustedCoefficients(
        Settings.AbsorptionRGBPerMeter,
        Settings.ScatteringRGBPerMeter,
        Settings.Turbidity,
        State.EffectiveAbsorptionRGBPerMeter,
        State.EffectiveScatteringRGBPerMeter);
    State.ExtinctionRGBPerMeter = FVector(
        FMath::Max(0.0, State.EffectiveAbsorptionRGBPerMeter.X + State.EffectiveScatteringRGBPerMeter.X),
        FMath::Max(0.0, State.EffectiveAbsorptionRGBPerMeter.Y + State.EffectiveScatteringRGBPerMeter.Y),
        FMath::Max(0.0, State.EffectiveAbsorptionRGBPerMeter.Z + State.EffectiveScatteringRGBPerMeter.Z));

    State.ReferenceVisibilityMeters = URealOceanOpticsLibrary::CalculateReferenceVisibilityMeters(
        State.EffectiveAbsorptionRGBPerMeter,
        State.EffectiveScatteringRGBPerMeter,
        Settings.VisibilityTransmissionThreshold);

    ResolveSunState();
    ResolveQualityBudget();

    if (Settings.Caustics.bEnabled && WaterColumnDepthMeters > 0.0 && State.EffectiveCausticsMaxDepthMeters > 0.0)
    {
        const double Envelope = 1.0 - RealOceanMath::Smooth01(WaterColumnDepthMeters / State.EffectiveCausticsMaxDepthMeters);
        State.CausticsStrength = State.UnderwaterBlend
            * Envelope
            * State.SunlightTransmission
            * FMath::Max(0.0, Settings.Caustics.Intensity);
    }
    else
    {
        State.CausticsStrength = 0.0;
    }

    State.WaveTravelDirectionXY = RealOceanMath::DirectionFromDegrees(Settings.Waves.TravelDirectionDegrees);
    State.WindDirectionXY = RealOceanMath::DirectionFromDegrees(Settings.Waves.WindDirectionDegrees);
    const FRealOceanSpectrumPartition SpectrumPartitionState = URealOceanWaveLibrary::CalculateSpectrumPartition(Settings.Waves, Settings.GravityMS2);
    State.SwellSignificantWaveHeightMeters = SpectrumPartitionState.SwellSignificantWaveHeightMeters;
    State.WindSeaSignificantWaveHeightMeters = SpectrumPartitionState.WindSeaSignificantWaveHeightMeters;
    State.WindSeaPeakPeriodSeconds = SpectrumPartitionState.WindSeaPeakPeriodSeconds;
    URealOceanWaveLibrary::CalculateSpatiotemporalPhaseAnchors(
        FVector2D(RenderCameraPositionCm.X, RenderCameraPositionCm.Y) * 0.01,
        Settings.Waves,
        Settings.GravityMS2,
        State.OceanTimeSeconds,
        State.WaveAnchorPhase0123,
        State.WaveAnchorPhase4567);
    URealOceanWaveLibrary::CalculateMicroSpatiotemporalPhaseAnchors(
        FVector2D(RenderCameraPositionCm.X, RenderCameraPositionCm.Y) * 0.01,
        Settings.Waves,
        State.OceanTimeSeconds,
        State.MicroWaveAnchorPhase0123);
    State.UnderwaterCriticalAngleDegrees = URealOceanWaveLibrary::CalculateCriticalAngleDegrees(
        Settings.Surface.WaterRefractiveIndex,
        Settings.Surface.AirRefractiveIndex);

    State.DepthZone = ClassifyDepth(DynamicSurfaceDepthMeters);

    // Milestone 16: push a UObject-free immutable snapshot to the render thread so the
    // experimental half-resolution RDG path evaluates the exact same ocean state as the
    // material/MPC path. Absolute world coordinates are intentionally not transferred.
    const FRealOceanWaveSample RenderCameraSurface = URealOceanWaveLibrary::EvaluateSurfaceAtWorldXY(
        FVector2D(RenderCameraPositionCm.X, RenderCameraPositionCm.Y) * 0.01,
        State.OceanTimeSeconds,
        Settings.Waves,
        Settings.GravityMS2,
        4);
    const double RenderCameraDynamicSurfaceDepthMeters = SeaLevelCm * 0.01 + RenderCameraSurface.HeightMeters - RenderCameraPositionCm.Z * 0.01;

    FRealOceanRenderState RenderState;
    RenderState.bConfigured = bConfigured;
    RenderState.bCausticsEnabled = IsRenderCausticsEnabled();
    RenderState.SeaLevelCm = static_cast<float>(SeaLevelCm);
    RenderState.CameraDepthMeters = static_cast<float>(FMath::Clamp(RenderCameraDynamicSurfaceDepthMeters, 0.0, Settings.MaximumDepthMeters));
    RenderState.SunRelativeIntensity = static_cast<float>(State.SunRelativeIntensity);
    RenderState.SunCosZenithAir = static_cast<float>(State.SunCosZenithAir);
    RenderState.SunDirectionToSourceWS = FVector3f(State.SunDirectionToSourceWS);
    RenderState.SunColor = FVector3f(State.SunColor.R, State.SunColor.G, State.SunColor.B);
    RenderState.AbsorptionPerMeter = FVector3f(State.EffectiveAbsorptionRGBPerMeter);
    RenderState.ScatteringPerMeter = FVector3f(State.EffectiveScatteringRGBPerMeter);
    RenderState.AirIOR = static_cast<float>(Settings.Surface.AirRefractiveIndex);
    RenderState.WaterIOR = static_cast<float>(Settings.Surface.WaterRefractiveIndex);
    RenderState.OceanTimeSeconds = State.OceanTimeSeconds;
    const FRealOceanSpectrumPartition SpectrumPartition = URealOceanWaveLibrary::CalculateSpectrumPartition(Settings.Waves, Settings.GravityMS2);
    RenderState.TravelDirectionDegrees = static_cast<float>(Settings.Waves.TravelDirectionDegrees);
    RenderState.WindDirectionDegrees = static_cast<float>(Settings.Waves.WindDirectionDegrees);
    RenderState.WindSpeedMps = static_cast<float>(Settings.Waves.WindSpeedMetersPerSecond);
    RenderState.SignificantWaveHeightMeters = static_cast<float>(Settings.Waves.SignificantWaveHeightMeters);
    RenderState.SwellSignificantWaveHeightMeters = static_cast<float>(SpectrumPartition.SwellSignificantWaveHeightMeters);
    RenderState.WindSeaSignificantWaveHeightMeters = static_cast<float>(SpectrumPartition.WindSeaSignificantWaveHeightMeters);
    RenderState.DominantSwellPeriodSeconds = static_cast<float>(Settings.Waves.DominantSwellPeriodSeconds);
    RenderState.WindSeaPeakPeriodSeconds = static_cast<float>(SpectrumPartition.WindSeaPeakPeriodSeconds);
    RenderState.DirectionalSpread = static_cast<float>(Settings.Waves.DirectionalSpread);
    RenderState.WindSeaDirectionalSpread = static_cast<float>(Settings.Waves.WindSeaDirectionalSpread);
    RenderState.Steepness = static_cast<float>(Settings.Waves.Steepness);
    RenderState.WindSeaSteepnessScale = static_cast<float>(Settings.Waves.WindSeaSteepnessScale);
    RenderState.GravityMS2 = static_cast<float>(Settings.GravityMS2);
    RenderState.AnchorPhase0123 = FVector4f(State.WaveAnchorPhase0123.R, State.WaveAnchorPhase0123.G, State.WaveAnchorPhase0123.B, State.WaveAnchorPhase0123.A);
    RenderState.AnchorPhase4567 = FVector4f(State.WaveAnchorPhase4567.R, State.WaveAnchorPhase4567.G, State.WaveAnchorPhase4567.B, State.WaveAnchorPhase4567.A);
    RenderState.EffectiveMaxDepthMeters = static_cast<float>(State.EffectiveCausticsMaxDepthMeters);
    RenderState.DerivativeStepMeters = static_cast<float>(Settings.Caustics.DerivativeStepMeters);
    RenderState.ScreenSpaceStability = static_cast<float>(Settings.Caustics.ScreenSpaceStability);
    RenderState.Contrast = static_cast<float>(Settings.Caustics.Contrast);
    RenderState.FocusClamp = static_cast<float>(Settings.Caustics.FocusClamp);
    RenderState.CausticsIntensity = static_cast<float>(Settings.Caustics.Intensity);
    RenderState.SolveIterations = static_cast<float>(State.CausticsSolveIterations);
    RenderState.DerivativeScale = static_cast<float>(State.CausticsDerivativeScale);
    RenderState.QualityMode = static_cast<float>(static_cast<uint8>(State.QualityMode));

    const FVector2D WindDirection = RealOceanMath::DirectionFromDegrees(Settings.Waves.WindDirectionDegrees);
    const FVector2D SurfaceCurrentDirection = RealOceanMath::DirectionFromDegrees(Settings.Current.SurfaceDirectionDegrees);
    const double DriftSpeedMps = FMath::Min(FMath::Max(0.0, Settings.Waves.WindSpeedMetersPerSecond) * FMath::Max(0.0, Settings.Surface.FoamWindDriftFraction), 1.25);
    FVector2D FoamAdvection = WindDirection * DriftSpeedMps
        + SurfaceCurrentDirection * Settings.Current.SurfaceSpeedMetersPerSecond * FMath::Max(0.0, Settings.Surface.FoamSurfaceCurrentInfluence);
    const double FoamSpeed = FoamAdvection.Size();
    if (FoamSpeed > 3.0)
    {
        FoamAdvection *= 3.0 / FoamSpeed;
    }

    RenderState.bFoamClipmapEnabled = IsRenderGPUFoamEnabled()
        && Settings.QualityMode == ERealOceanQualityMode::Cinematic
        && FoamClipmapRenderTarget != nullptr
        && bFoamClipmapMaterialBound;
#if UE_BUILD_SHIPPING
    RenderState.bFoamClipmapEnabled = RenderState.bFoamClipmapEnabled && RealOceanRelease::UsesGPUFoamClipmap();
#endif
    RenderState.FoamClipmapResolution = FoamClipmapRenderTarget ? FoamClipmapRenderTarget->SizeX : 0;
    RenderState.FoamClipmapCoverageMeters = static_cast<float>(FMath::Clamp(Settings.Surface.FoamClipmapCoverageMeters, 64.0, 1024.0));
    RenderState.FoamClipmapUpdateRateHz = static_cast<float>(ResolveFoamClipmapUpdateRateHz());
    RenderState.FoamDecayHalfLifeSeconds = static_cast<float>(FMath::Max(0.1, Settings.Surface.FoamDecayHalfLifeSeconds));
    RenderState.FoamClipmapInjectionPerSecond = static_cast<float>(FMath::Max(0.0, Settings.Surface.FoamClipmapInjectionPerSecond));
    RenderState.FoamClipmapDiffusionPerSecond = static_cast<float>(FMath::Clamp(Settings.Surface.FoamClipmapDiffusionPerSecond, 0.0, 1.0));
    RenderState.BreakingStrength = static_cast<float>(FMath::Max(0.0, Settings.Surface.BreakingStrength));
    RenderState.WhitecapCoverageScale = static_cast<float>(FMath::Max(0.0, Settings.Surface.WhitecapCoverageScale));
    RenderState.FoamAdvectionVelocityMps = FVector2f(FoamAdvection);
    const FVector2D FoamCenterOffsetMeters = CalculateFoamClipmapCenterOffsetMeters();
    RenderState.FoamClipmapCenterOffsetMeters = FVector2f(FoamCenterOffsetMeters);
    RenderState.FoamClipmapCenterWorldMeters = FVector2d(
        State.RenderCameraWorldPositionCm.X * 0.01 + FoamCenterOffsetMeters.X,
        State.RenderCameraWorldPositionCm.Y * 0.01 + FoamCenterOffsetMeters.Y);
    if (UWorld* World = GetWorld())
    {
        PushRealOceanRenderState_GameThread(World->Scene, RenderState);
    }

    if (PreviousZone != State.DepthZone)
    {
        OnDepthZoneChanged.Broadcast(PreviousZone, State.DepthZone);
    }
}

void URealOceanSubsystem::PushMaterialParameters()
{
    UWorld* World = GetWorld();
    if (!World || !GlobalParameters)
    {
        return;
    }

    UMaterialParameterCollectionInstance* MPC = World->GetParameterCollectionInstance(GlobalParameters);
    if (!MPC)
    {
        return;
    }

    // Dynamic state: camera, water column, sun and phase anchors can change every frame.
    MPC->SetScalarParameterValue(TEXT("RO_SeaLevelCm"), static_cast<float>(State.SeaLevelCm));
    MPC->SetScalarParameterValue(TEXT("RO_DepthMeters"), static_cast<float>(State.DepthMeters));
    MPC->SetScalarParameterValue(TEXT("RO_RawDepthMeters"), static_cast<float>(State.RawDepthMeters));
    MPC->SetScalarParameterValue(TEXT("RO_BelowConfiguredBottom"), State.bBelowConfiguredBottom ? 1.0f : 0.0f);
    MPC->SetScalarParameterValue(TEXT("RO_NormalizedDepth"), static_cast<float>(State.NormalizedDepth));
    MPC->SetScalarParameterValue(TEXT("RO_GaugePressureBar"), static_cast<float>(State.GaugePressureBar));
    MPC->SetScalarParameterValue(TEXT("RO_AbsolutePressureBar"), static_cast<float>(State.AbsolutePressureBar));
    MPC->SetScalarParameterValue(TEXT("RO_UnderwaterBlend"), static_cast<float>(State.UnderwaterBlend));
    MPC->SetScalarParameterValue(TEXT("RO_DepthZone"), static_cast<float>(static_cast<uint8>(State.DepthZone)));
    MPC->SetScalarParameterValue(TEXT("RO_SunlightTransmission"), static_cast<float>(State.SunlightTransmission));
    MPC->SetScalarParameterValue(TEXT("RO_SunIntensity"), static_cast<float>(State.SunIntensity));
    MPC->SetScalarParameterValue(TEXT("RO_SunRelativeIntensity"), static_cast<float>(State.SunRelativeIntensity));
    MPC->SetScalarParameterValue(TEXT("RO_InscatterIntensity"), static_cast<float>(State.EffectiveInscatterIntensity));
    MPC->SetScalarParameterValue(TEXT("RO_SunCosZenithAir"), static_cast<float>(State.SunCosZenithAir));
    MPC->SetScalarParameterValue(TEXT("RO_SunCosZenithWater"), static_cast<float>(State.SunCosZenithWater));
    MPC->SetScalarParameterValue(TEXT("RO_DirectSunPathMeters"), static_cast<float>(FMath::Min(State.DirectSunPathMeters, 1000000.0)));
    MPC->SetScalarParameterValue(TEXT("RO_OceanTimeSeconds"), static_cast<float>(State.OceanTimeSeconds));
    MPC->SetScalarParameterValue(TEXT("RO_CausticsStrength"),
        bMaterialCausticsSuppressed ? 0.0f : static_cast<float>(State.CausticsStrength));

    MPC->SetVectorParameterValue(TEXT("RO_WaveAnchorPhase0123"), State.WaveAnchorPhase0123);
    MPC->SetVectorParameterValue(TEXT("RO_WaveAnchorPhase4567"), State.WaveAnchorPhase4567);
    MPC->SetVectorParameterValue(TEXT("RO_MicroWaveAnchorPhase0123"), State.MicroWaveAnchorPhase0123);
    MPC->SetVectorParameterValue(TEXT("RO_SunDirectionToSourceWS"), FLinearColor(
        static_cast<float>(State.SunDirectionToSourceWS.X),
        static_cast<float>(State.SunDirectionToSourceWS.Y),
        static_cast<float>(State.SunDirectionToSourceWS.Z),
        0.0f));
    MPC->SetVectorParameterValue(TEXT("RO_SunColor"), State.SunColor);
    MPC->SetVectorParameterValue(TEXT("RO_SunTransmissionRGB"), FLinearColor(
        static_cast<float>(State.SunTransmissionRGB.X),
        static_cast<float>(State.SunTransmissionRGB.Y),
        static_cast<float>(State.SunTransmissionRGB.Z),
        0.0f));
    const FVector2D DynamicFoamCenterOffsetMeters = CalculateFoamClipmapCenterOffsetMeters();
    MPC->SetVectorParameterValue(TEXT("RO_FoamClipmapCenterOffsetMeters"), FLinearColor(
        static_cast<float>(DynamicFoamCenterOffsetMeters.X),
        static_cast<float>(DynamicFoamCenterOffsetMeters.Y),
        0.0f,
        0.0f));
    MPC->SetVectorParameterValue(TEXT("RO_FoamClipmapCenterWorldXYMeters"), FLinearColor(
        static_cast<float>(State.RenderCameraWorldPositionCm.X * 0.01 + DynamicFoamCenterOffsetMeters.X),
        static_cast<float>(State.RenderCameraWorldPositionCm.Y * 0.01 + DynamicFoamCenterOffsetMeters.Y),
        0.0f,
        0.0f));

    if (!bStaticParametersDirty)
    {
        return;
    }

    // Static/config state: only rewrite when Configure() changes the controller/profile/MPC contract.
    MPC->SetScalarParameterValue(TEXT("RO_ContractVersion"), 12.0f);
    MPC->SetScalarParameterValue(TEXT("RO_UnderwaterTransitionMeters"), static_cast<float>(FMath::Max(0.01, Settings.UnderwaterTransitionMeters)));
    MPC->SetScalarParameterValue(TEXT("RO_VisibilityMeters"), static_cast<float>(FMath::Min(State.ReferenceVisibilityMeters, 1000000.0)));
    MPC->SetScalarParameterValue(TEXT("RO_MaxDepthMeters"), 3000.0f);
    MPC->SetScalarParameterValue(TEXT("RO_GravityMS2"), static_cast<float>(Settings.GravityMS2));
    MPC->SetScalarParameterValue(TEXT("RO_Turbidity"), static_cast<float>(Settings.Turbidity));
    MPC->SetScalarParameterValue(TEXT("RO_MaxOpticalPathMeters"), static_cast<float>(Settings.MaxOpticalPathMeters));
    MPC->SetScalarParameterValue(TEXT("RO_AphoticDepthMeters"), static_cast<float>(Settings.Lighting.AphoticDepthMeters));
    MPC->SetScalarParameterValue(TEXT("RO_QualityMode"), static_cast<float>(static_cast<uint8>(State.QualityMode)));
    MPC->SetScalarParameterValue(TEXT("RO_DebugView"), static_cast<float>(static_cast<uint8>(State.DebugView)));
    MPC->SetScalarParameterValue(TEXT("RO_DebugOpacity"), static_cast<float>(FMath::Clamp(Settings.DebugOpacity, 0.0, 1.0)));
    MPC->SetScalarParameterValue(TEXT("RO_ForwardScatteringG"), static_cast<float>(Settings.Lighting.ForwardScatteringG));
    const FRealOceanSpectrumPartition SpectrumPartitionStatic = URealOceanWaveLibrary::CalculateSpectrumPartition(Settings.Waves, Settings.GravityMS2);
    MPC->SetScalarParameterValue(TEXT("RO_WaveTravelDirectionDegrees"), static_cast<float>(Settings.Waves.TravelDirectionDegrees));
    MPC->SetScalarParameterValue(TEXT("RO_WindDirectionDegrees"), static_cast<float>(Settings.Waves.WindDirectionDegrees));
    MPC->SetScalarParameterValue(TEXT("RO_WindSpeedMps"), static_cast<float>(Settings.Waves.WindSpeedMetersPerSecond));
    MPC->SetScalarParameterValue(TEXT("RO_SignificantWaveHeightMeters"), static_cast<float>(Settings.Waves.SignificantWaveHeightMeters));
    MPC->SetScalarParameterValue(TEXT("RO_SwellSignificantWaveHeightMeters"), static_cast<float>(SpectrumPartitionStatic.SwellSignificantWaveHeightMeters));
    MPC->SetScalarParameterValue(TEXT("RO_WindSeaSignificantWaveHeightMeters"), static_cast<float>(SpectrumPartitionStatic.WindSeaSignificantWaveHeightMeters));
    MPC->SetScalarParameterValue(TEXT("RO_DominantSwellPeriodSeconds"), static_cast<float>(Settings.Waves.DominantSwellPeriodSeconds));
    MPC->SetScalarParameterValue(TEXT("RO_WindSeaPeakPeriodSeconds"), static_cast<float>(SpectrumPartitionStatic.WindSeaPeakPeriodSeconds));
    MPC->SetScalarParameterValue(TEXT("RO_WindSeaFetchKm"), static_cast<float>(Settings.Waves.WindSeaFetchLengthKm));
    MPC->SetScalarParameterValue(TEXT("RO_WindSeaEnergyScale"), static_cast<float>(Settings.Waves.WindSeaEnergyScale));
    MPC->SetScalarParameterValue(TEXT("RO_DirectionalSpread"), static_cast<float>(Settings.Waves.DirectionalSpread));
    MPC->SetScalarParameterValue(TEXT("RO_WindSeaDirectionalSpread"), static_cast<float>(Settings.Waves.WindSeaDirectionalSpread));
    MPC->SetScalarParameterValue(TEXT("RO_WaveSteepness"), static_cast<float>(Settings.Waves.Steepness));
    MPC->SetScalarParameterValue(TEXT("RO_WindSeaSteepnessScale"), static_cast<float>(Settings.Waves.WindSeaSteepnessScale));
    MPC->SetScalarParameterValue(TEXT("RO_AirIOR"), static_cast<float>(Settings.Surface.AirRefractiveIndex));
    MPC->SetScalarParameterValue(TEXT("RO_WaterIOR"), static_cast<float>(Settings.Surface.WaterRefractiveIndex));
    MPC->SetScalarParameterValue(TEXT("RO_CriticalAngleDegrees"), static_cast<float>(State.UnderwaterCriticalAngleDegrees));
    MPC->SetScalarParameterValue(TEXT("RO_SurfaceRoughness"), static_cast<float>(Settings.Surface.Roughness));
    MPC->SetScalarParameterValue(TEXT("RO_MicroNormalStrength"),
        IsRenderMicroWavesEnabled() ? static_cast<float>(Settings.Surface.MicroNormalStrength) : 0.0f);
    MPC->SetScalarParameterValue(TEXT("RO_BreakingStrength"), static_cast<float>(Settings.Surface.BreakingStrength));
    MPC->SetScalarParameterValue(TEXT("RO_WhitecapCoverageScale"), static_cast<float>(Settings.Surface.WhitecapCoverageScale));
    MPC->SetScalarParameterValue(TEXT("RO_FoamMemorySeconds"), static_cast<float>(Settings.Surface.FoamMemorySeconds));
    MPC->SetScalarParameterValue(TEXT("RO_FoamDecayHalfLifeSeconds"), static_cast<float>(Settings.Surface.FoamDecayHalfLifeSeconds));
    MPC->SetScalarParameterValue(TEXT("RO_FoamWindDriftFraction"), static_cast<float>(Settings.Surface.FoamWindDriftFraction));
    MPC->SetScalarParameterValue(TEXT("RO_FoamSurfaceCurrentInfluence"), static_cast<float>(Settings.Surface.FoamSurfaceCurrentInfluence));
    bool bGPUFoamActive = IsRenderGPUFoamEnabled()
        && Settings.QualityMode == ERealOceanQualityMode::Cinematic
        && FoamClipmapRenderTarget != nullptr
        && bFoamClipmapMaterialBound;
#if UE_BUILD_SHIPPING
    bGPUFoamActive = bGPUFoamActive && RealOceanRelease::UsesGPUFoamClipmap();
#endif
    const double FoamCoverageMeters = FMath::Clamp(Settings.Surface.FoamClipmapCoverageMeters, 64.0, 1024.0);
    const int32 FoamResolution = ResolveFoamClipmapResolution();
    MPC->SetScalarParameterValue(TEXT("RO_FoamClipmapCoverageMeters"), static_cast<float>(FoamCoverageMeters));
    MPC->SetScalarParameterValue(TEXT("RO_FoamClipmapValid"), bGPUFoamActive ? 1.0f : 0.0f);
    MPC->SetScalarParameterValue(TEXT("RO_FoamClipmapBlend"), static_cast<float>(FMath::Clamp(Settings.Surface.FoamClipmapBlend, 0.0, 1.0)));
    MPC->SetScalarParameterValue(TEXT("RO_FoamClipmapEdgeFadeMeters"), static_cast<float>(FMath::Clamp(Settings.Surface.FoamClipmapEdgeFadeMeters, 0.0, FoamCoverageMeters * 0.45)));
    MPC->SetScalarParameterValue(TEXT("RO_FoamClipmapResolution"), static_cast<float>(FoamResolution));
    MPC->SetScalarParameterValue(TEXT("RO_FoamClipmapTexelMeters"), static_cast<float>(FoamCoverageMeters / static_cast<double>(FoamResolution)));
    MPC->SetScalarParameterValue(TEXT("RO_FoamClipmapUpdateRateHz"), static_cast<float>(ResolveFoamClipmapUpdateRateHz()));
    MPC->SetScalarParameterValue(TEXT("RO_MicroDetailFadeStartMeters"), static_cast<float>(Settings.Surface.MicroDetailFadeStartMeters));
    MPC->SetScalarParameterValue(TEXT("RO_MicroDetailFadeEndMeters"), static_cast<float>(Settings.Surface.MicroDetailFadeEndMeters));
    MPC->SetScalarParameterValue(TEXT("RO_HorizonRoughnessBoost"), static_cast<float>(Settings.Surface.HorizonRoughnessBoost));
    MPC->SetScalarParameterValue(TEXT("RO_CausticsEnabled"),
        (IsRenderCausticsEnabled() && !bMaterialCausticsSuppressed) ? 1.0f : 0.0f);
    MPC->SetScalarParameterValue(TEXT("RO_CausticsMaxDepthMeters"), static_cast<float>(Settings.Caustics.MaxDepthMeters));
    MPC->SetScalarParameterValue(TEXT("RO_CausticsIntensity"),
        (bMaterialCausticsSuppressed || !IsRenderCausticsEnabled()) ? 0.0f : static_cast<float>(Settings.Caustics.Intensity));
    MPC->SetScalarParameterValue(TEXT("RO_CausticsContrast"), static_cast<float>(Settings.Caustics.Contrast));
    MPC->SetScalarParameterValue(TEXT("RO_CausticsFocusClamp"), static_cast<float>(Settings.Caustics.FocusClamp));
    MPC->SetScalarParameterValue(TEXT("RO_CausticsDerivativeStepMeters"), static_cast<float>(Settings.Caustics.DerivativeStepMeters));
    MPC->SetScalarParameterValue(TEXT("RO_CausticsScreenSpaceStability"), static_cast<float>(Settings.Caustics.ScreenSpaceStability));
    MPC->SetScalarParameterValue(TEXT("RO_EffectiveCausticsMaxDepthMeters"), static_cast<float>(State.EffectiveCausticsMaxDepthMeters));
    MPC->SetScalarParameterValue(TEXT("RO_CausticsSolveIterations"), static_cast<float>(State.CausticsSolveIterations));
    MPC->SetScalarParameterValue(TEXT("RO_CausticsDerivativeScale"), static_cast<float>(State.CausticsDerivativeScale));

    MPC->SetVectorParameterValue(TEXT("RO_WaveDirectionXY"), FLinearColor(
        static_cast<float>(State.WaveTravelDirectionXY.X),
        static_cast<float>(State.WaveTravelDirectionXY.Y),
        0.0f,
        0.0f));
    const FVector2D SurfaceCurrentDirection = RealOceanMath::DirectionFromDegrees(Settings.Current.SurfaceDirectionDegrees);
    MPC->SetVectorParameterValue(TEXT("RO_SurfaceCurrentVelocityXYMps"), FLinearColor(
        static_cast<float>(SurfaceCurrentDirection.X * Settings.Current.SurfaceSpeedMetersPerSecond),
        static_cast<float>(SurfaceCurrentDirection.Y * Settings.Current.SurfaceSpeedMetersPerSecond),
        0.0f,
        0.0f));
    MPC->SetVectorParameterValue(TEXT("RO_AbsorptionRGB"), FLinearColor(
        static_cast<float>(Settings.AbsorptionRGBPerMeter.X),
        static_cast<float>(Settings.AbsorptionRGBPerMeter.Y),
        static_cast<float>(Settings.AbsorptionRGBPerMeter.Z),
        0.0f));
    MPC->SetVectorParameterValue(TEXT("RO_ScatteringRGB"), FLinearColor(
        static_cast<float>(Settings.ScatteringRGBPerMeter.X),
        static_cast<float>(Settings.ScatteringRGBPerMeter.Y),
        static_cast<float>(Settings.ScatteringRGBPerMeter.Z),
        0.0f));
    MPC->SetVectorParameterValue(TEXT("RO_ExtinctionRGB"), FLinearColor(
        static_cast<float>(State.ExtinctionRGBPerMeter.X),
        static_cast<float>(State.ExtinctionRGBPerMeter.Y),
        static_cast<float>(State.ExtinctionRGBPerMeter.Z),
        0.0f));
    MPC->SetVectorParameterValue(TEXT("RO_InscatterColor"), Settings.InscatterColor);

    bStaticParametersDirty = false;
}

