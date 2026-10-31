#include "RealOceanController.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Net/UnrealNetwork.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/UnrealType.h"
#include "RealOceanSubsystem.h"
#include "RealOceanPostProcessComponent.h"
#include "RealOceanValidationLibrary.h"
#include "RealOceanReleaseConfig.h"
#include "RealOceanWaterProfile.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "WaterBodyOceanActor.h"
#include "WaterBodyOceanComponent.h"
#include "WaterBodyTypes.h"
#include "WaterZoneActor.h"
#include "WaterSubsystem.h"

namespace
{
#if !UE_BUILD_SHIPPING
    bool ParseProfilingVariantFromCommandLine(ERealOceanProfilingVariant& OutVariant)
    {
        FString Value;
        if (!FParse::Value(FCommandLine::Get(), TEXT("RealOceanProfileVariant="), Value))
        {
            return false;
        }

        if (Value.Equals(TEXT("Full"), ESearchCase::IgnoreCase)) OutVariant = ERealOceanProfilingVariant::Full;
        else if (Value.Equals(TEXT("NoCaustics"), ESearchCase::IgnoreCase)) OutVariant = ERealOceanProfilingVariant::NoCaustics;
        else if (Value.Equals(TEXT("NoMicroWaves"), ESearchCase::IgnoreCase)) OutVariant = ERealOceanProfilingVariant::NoMicroWaves;
        else if (Value.Equals(TEXT("SurfaceOnly"), ESearchCase::IgnoreCase)) OutVariant = ERealOceanProfilingVariant::SurfaceOnly;
        else if (Value.Equals(TEXT("MacroSurfaceOnly"), ESearchCase::IgnoreCase)) OutVariant = ERealOceanProfilingVariant::MacroSurfaceOnly;
        else if (Value.Equals(TEXT("RDGCausticsOnly"), ESearchCase::IgnoreCase)) OutVariant = ERealOceanProfilingVariant::RDGCausticsOnly;
        else if (Value.Equals(TEXT("NoGPUFoam"), ESearchCase::IgnoreCase)) OutVariant = ERealOceanProfilingVariant::NoGPUFoam;
        else return false;
        return true;
    }

    bool ParseFoamPresetFromCommandLine(ERealOceanFoamClipmapQualityPreset& OutPreset)
    {
        FString Value;
        if (!FParse::Value(FCommandLine::Get(), TEXT("RealOceanFoamPreset="), Value))
        {
            return false;
        }

        if (Value.Equals(TEXT("Low256"), ESearchCase::IgnoreCase)) OutPreset = ERealOceanFoamClipmapQualityPreset::Low256;
        else if (Value.Equals(TEXT("Medium384"), ESearchCase::IgnoreCase)) OutPreset = ERealOceanFoamClipmapQualityPreset::Medium384;
        else if (Value.Equals(TEXT("High512"), ESearchCase::IgnoreCase)) OutPreset = ERealOceanFoamClipmapQualityPreset::High512;
        else if (Value.Equals(TEXT("Custom"), ESearchCase::IgnoreCase)) OutPreset = ERealOceanFoamClipmapQualityPreset::Custom;
        else return false;
        return true;
    }
#endif

    bool TryReadConfiguredWaterMaterial(AWaterBody* Ocean, UMaterialInterface*& OutMaterial)
    {
        OutMaterial = nullptr;
        if (!Ocean)
        {
            return false;
        }
        UWaterBodyComponent* Component = Ocean->GetWaterBodyComponent();
        if (!Component)
        {
            return false;
        }

        // WaterMaterial is not exposed through a stable public getter in all Water plugin revisions.
        // Use reflected property access only for restoration; if Epic renames it, skip restoration
        // instead of mistaking "getter unavailable" for a legitimate null original material.
        if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Component->GetClass(), TEXT("WaterMaterial")))
        {
            OutMaterial = Cast<UMaterialInterface>(Property->GetObjectPropertyValue_InContainer(Component));
            return true;
        }
        return false;
    }
}

ARealOceanController::ARealOceanController()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    bAlwaysRelevant = true;
    SetReplicateMovement(false);
    UnderwaterPostProcess = CreateDefaultSubobject<URealOceanPostProcessComponent>(TEXT("UnderwaterPostProcess"));
    SetRootComponent(UnderwaterPostProcess);

#if UE_BUILD_SHIPPING
    // Validation is an integration aid; avoid startup log/asset inspection work in shipping builds.
    bValidateOnBeginPlay = false;
#else
    bValidateOnBeginPlay = true;
#endif
}

void ARealOceanController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARealOceanController, bIsTurnkeyAutoController);
    DOREPLIFETIME(ARealOceanController, NetworkClockSync);
}

void ARealOceanController::BeginPlay()
{
    Super::BeginPlay();
    RegisterLocalPlayerLifecycleDelegates();
#if !UE_BUILD_SHIPPING
    ERealOceanProfilingVariant CommandLineVariant = ERealOceanProfilingVariant::Full;
    if (ParseProfilingVariantFromCommandLine(CommandLineVariant))
    {
        ProfilingVariant = CommandLineVariant;
    }
#endif
    if (!bIsTurnkeyAutoController)
    {
        RetireAutoControllerForExplicitHandoff();
    }

    // A network client never starts an independent local wave phase. Wait for the replicated
    // authoritative epoch; OnRep_NetworkClockSync completes turnkey configuration immediately.
    if (IsNetworkedWorld() && !HasAuthority())
    {
        if (NetworkClockSync.Revision > 0 && !NetworkClockSync.IsCompatible())
        {
            // A replicated but incompatible snapshot must fail closed immediately rather than waiting
            // forever for an OnRep that may already have happened before BeginPlay.
            ApplySettings();
            return;
        }
        if (NetworkClockSync.Revision == 0)
        {
            bDeferredBeginPlayForClockSync = true;
            UE_LOG(LogRealOcean, Verbose, TEXT("RealOceanController %s is waiting for authoritative ocean clock replication."), *GetName());
            return;
        }
    }

    ApplySettings();
    if (bValidateOnBeginPlay)
    {
        ValidateSetup();
    }
}

void ARealOceanController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnregisterLocalPlayerLifecycleDelegates();
    bool bWasConfigurationOwner = false;
    if (UWorld* World = GetWorld())
    {
        if (URealOceanSubsystem* OceanSubsystem = World->GetSubsystem<URealOceanSubsystem>())
        {
            bWasConfigurationOwner = OceanSubsystem->IsConfiguredByOwner(this);
            if (bWasConfigurationOwner)
            {
                // Only the active owner may restore/destroy shared turnkey water resources. A stale
                // controller must not clobber a newer controller that took over the same world.
                const bool bRecoverableRuntimeRemoval = !bIsTurnkeyAutoController
                    && (EndPlayReason == EEndPlayReason::RemovedFromWorld || EndPlayReason == EEndPlayReason::Destroyed);

                if (bRecoverableRuntimeRemoval)
                {
                    // Preserve wave/current clocks across a streaming ownership gap. The subsystem stays
                    // configured-but-ownerless for at most one tick while a replacement is promoted/spawned.
                    const bool bReleasedForRecovery = OceanSubsystem->ReleaseOwnerForHandoff(this);
                    CleanupTurnkeyRuntimeArtifacts();
                    if (bReleasedForRecovery)
                    {
                        OceanSubsystem->ScheduleTurnkeyControllerRecovery();
                    }
                    else
                    {
                        // Defensive fallback: never leave a stale owner if an unexpected lifecycle race
                        // invalidated the handoff precondition after bWasConfigurationOwner was sampled.
                        OceanSubsystem->DeconfigureForOwner(this);
                    }
                }
                else
                {
                    CleanupTurnkeyRuntimeArtifacts();
                    OceanSubsystem->DeconfigureForOwner(this);
                }
            }
        }
    }

    if (!bWasConfigurationOwner)
    {
        TurnkeyOceanMID = nullptr;
        TurnkeyBoundOcean.Reset();
        TurnkeyAutoCreatedOcean.Reset();
        TurnkeyAutoCreatedWaterZone.Reset();
        OriginalOceanSurfaceMaterial = nullptr;
        bCapturedOriginalOceanMaterial = false;
    }

    Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void ARealOceanController::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    // Avoid mutating a user's editor-world WaterBodyOcean with transient MIDs merely because a
    // controller property changed in Details. Runtime wiring is performed in BeginPlay.
    if (UWorld* World = GetWorld(); World && World->IsGameWorld())
    {
        ApplySettings();
    }
}
#endif

void ARealOceanController::ApplySettings()
{
    // Cheap when already resolved and makes runtime recovery deterministic if generated assets become
    // available after BeginPlay (source-control checkout / manual turnkey rebuild during PIE).
    bTurnkeyAssetsReady = ResolveTurnkeyDefaults();

    FRealOceanSettings EffectiveSettings;
    if (IsNetworkedWorld() && !HasAuthority() && NetworkClockSync.Revision > 0)
    {
        if (!NetworkClockSync.IsCompatible())
        {
            if (!bWarnedNetworkProtocolMismatch)
            {
                UE_LOG(LogRealOcean, Error, TEXT("RealOcean network snapshot rejected: protocol/schema mismatch (remote protocol=%u schema=%u, local protocol=%u schema=%u). Update all peers to the same RealOcean build."),
                    NetworkClockSync.ProtocolVersion, NetworkClockSync.SettingsSchemaVersion,
                    RealOceanNetworkProtocol::SnapshotProtocolVersion, RealOceanNetworkProtocol::SettingsSchemaVersion);
                bWarnedNetworkProtocolMismatch = true;
            }
            if (UnderwaterPostProcess)
            {
                UnderwaterPostProcess->SetBlendWeightOverride(true, 0.0f);
            }
            RestoreTurnkeyOceanSurface();
            if (UWorld* MismatchWorld = GetWorld())
            {
                if (URealOceanSubsystem* MismatchSubsystem = MismatchWorld->GetSubsystem<URealOceanSubsystem>())
                {
                    MismatchSubsystem->DeconfigureForOwner(this);
                }
            }
            return;
        }
        bWarnedNetworkProtocolMismatch = false;
        // Consume the exact settings snapshot that produced the authoritative epoch. Local serialized
        // defaults/profiles must not produce a different wave spectrum on a join-in-progress client.
        EffectiveSettings = NetworkClockSync.EffectiveSettings;
    }
    else
    {
        EffectiveSettings = OceanSettings;
        if (WaterProfile)
        {
            WaterProfile->ApplyToSettings(EffectiveSettings);
        }
    }
    EffectiveSettings.MaximumDepthMeters = 3000.0;

#if UE_BUILD_SHIPPING
    // M32 fail-closed + shipping-attested release lock. Apply before Configure() so an analytic Shipping build never
    // allocates the plugin-owned R16F clipmap, and a certified GPU build cannot drift from the
    // exact preset that passed target-hardware capture.
    EffectiveSettings.Surface.bEnableGPUFoamClipmap = RealOceanRelease::UsesGPUFoamClipmap();
    if (RealOceanRelease::UsesGPUFoamClipmap())
    {
        switch (RealOceanRelease::GetCertifiedFoamPresetCode())
        {
        case 1: EffectiveSettings.Surface.FoamClipmapQualityPreset = ERealOceanFoamClipmapQualityPreset::Low256; break;
        case 2: EffectiveSettings.Surface.FoamClipmapQualityPreset = ERealOceanFoamClipmapQualityPreset::Medium384; break;
        case 3: EffectiveSettings.Surface.FoamClipmapQualityPreset = ERealOceanFoamClipmapQualityPreset::High512; break;
        default: EffectiveSettings.Surface.bEnableGPUFoamClipmap = false; break;
        }
    }
#endif

    if (UWorld* World = GetWorld())
    {
        if (URealOceanSubsystem* OceanSubsystem = World->GetSubsystem<URealOceanSubsystem>())
        {
            if (!OceanSubsystem->CanConfigureForOwner(this))
            {
                // Do not let a rejected duplicate controller mutate the shared network clock before
                // ownership validation. ConfigureForOwner emits the canonical duplicate-owner error.
                OceanSubsystem->ConfigureForOwner(this, EffectiveSettings, GlobalParameterCollection, SunDirectionalLight);
            }
            else
            {
                if (IsNetworkedWorld())
                {
                    if (HasAuthority())
                    {
                        UpdateAuthoritativeNetworkClock(EffectiveSettings, OceanSubsystem);
                    }
                    ApplyNetworkClockToSubsystem(OceanSubsystem);
                }
                else
                {
                    OceanSubsystem->ClearSynchronizedClock();
                }
                OceanSubsystem->ConfigureForOwner(this, EffectiveSettings, GlobalParameterCollection, SunDirectionalLight);
            }
            if (!OceanSubsystem->IsConfiguredByOwner(this))
            {
                if (UnderwaterPostProcess)
                {
                    UnderwaterPostProcess->SetBlendWeightOverride(true, 0.0f);
                }
                UE_LOG(LogRealOcean, Error, TEXT("RealOceanController %s did not acquire world ownership; disabling its post-process and skipping material runtime wiring."), *GetName());
                return;
            }

        }
    }

    RefreshLocalRenderStateOnly();
}

bool ARealOceanController::IsNetworkedWorld() const
{
    const UWorld* World = GetWorld();
    return World && World->GetNetMode() != NM_Standalone;
}

double ARealOceanController::ResolveServerWorldTimeSeconds() const
{
    if (const UWorld* World = GetWorld())
    {
        if (const AGameStateBase* GameState = World->GetGameState())
        {
            return static_cast<double>(GameState->GetServerWorldTimeSeconds());
        }
        return static_cast<double>(World->GetTimeSeconds());
    }
    return 0.0;
}

void ARealOceanController::UpdateAuthoritativeNetworkClock(const FRealOceanSettings& EffectiveSettings, URealOceanSubsystem* OceanSubsystem)
{
    if (!HasAuthority() || !IsNetworkedWorld() || !OceanSubsystem)
    {
        return;
    }

    const double ServerNow = ResolveServerWorldTimeSeconds();
    double CurrentWaveTime = 0.0;
    double CurrentTurbulenceTime = 0.0;
    if (NetworkClockSync.Revision > 0)
    {
        const double Elapsed = FMath::Max(0.0, ServerNow - NetworkClockSync.ServerEpochSeconds);
        CurrentWaveTime = NetworkClockSync.WaveTimeAtEpochSeconds + Elapsed * FMath::Max(0.0, NetworkClockSync.WaveTimeScale);
        CurrentTurbulenceTime = NetworkClockSync.TurbulenceTimeAtEpochSeconds + Elapsed * FMath::Max(0.0, NetworkClockSync.TurbulenceTimeScale);
    }
    else
    {
        // Initial worlds read as zero here; ownership-recovery worlds retain the previous subsystem
        // clocks so a newly promoted/spawned replicated controller does not restart the ocean phase.
        CurrentWaveTime = OceanSubsystem->GetWaveClockSeconds();
        CurrentTurbulenceTime = OceanSubsystem->GetCurrentTurbulenceClockSeconds();
    }

    NetworkClockSync.ProtocolVersion = RealOceanNetworkProtocol::SnapshotProtocolVersion;
    NetworkClockSync.SettingsSchemaVersion = RealOceanNetworkProtocol::SettingsSchemaVersion;
    NetworkClockSync.ServerEpochSeconds = ServerNow;
    NetworkClockSync.WaveTimeAtEpochSeconds = CurrentWaveTime;
    NetworkClockSync.TurbulenceTimeAtEpochSeconds = CurrentTurbulenceTime;
    NetworkClockSync.WaveTimeScale = FMath::Max(0.0, EffectiveSettings.Waves.TimeScale);
    NetworkClockSync.TurbulenceTimeScale = FMath::Max(0.0, EffectiveSettings.Current.TurbulenceTimeScale);
    NetworkClockSync.EffectiveSettings = EffectiveSettings;
    NetworkClockSync.EffectiveSettings.MaximumDepthMeters = 3000.0;
    ++NetworkClockSync.Revision;
    ForceNetUpdate();
}

void ARealOceanController::ApplyNetworkClockToSubsystem(URealOceanSubsystem* OceanSubsystem) const
{
    if (!OceanSubsystem || NetworkClockSync.Revision == 0)
    {
        return;
    }
    OceanSubsystem->SetSynchronizedClock(
        NetworkClockSync.ServerEpochSeconds,
        NetworkClockSync.WaveTimeAtEpochSeconds,
        NetworkClockSync.TurbulenceTimeAtEpochSeconds,
        NetworkClockSync.WaveTimeScale,
        NetworkClockSync.TurbulenceTimeScale);
}

void ARealOceanController::OnRep_NetworkClockSync()
{
    if (!NetworkClockSync.IsCompatible())
    {
        ApplySettings();
        return;
    }
    if (NetworkClockSync.Revision == 0 || !HasActorBegunPlay())
    {
        return;
    }

    const bool bWasDeferredInitialSetup = bDeferredBeginPlayForClockSync;
    bDeferredBeginPlayForClockSync = false;

    // Revision changes are intentionally infrequent (ApplySettings / ownership handoff), so reapply
    // the whole authoritative settings snapshot as well as the phase epoch. This keeps runtime server
    // Hs/period/direction/current changes deterministic on every client, not just join-in-progress.
    ApplySettings();

    if (bWasDeferredInitialSetup && bValidateOnBeginPlay)
    {
        ValidateSetup();
    }
}

void ARealOceanController::RetireAutoControllerForExplicitHandoff()
{
    UWorld* World = GetWorld();
    if (!World || bIsTurnkeyAutoController)
    {
        return;
    }

    URealOceanSubsystem* OceanSubsystem = World->GetSubsystem<URealOceanSubsystem>();
    if (!OceanSubsystem)
    {
        return;
    }

    for (TActorIterator<ARealOceanController> It(World); It; ++It)
    {
        ARealOceanController* Candidate = *It;
        if (!Candidate || Candidate == this || !Candidate->bIsTurnkeyAutoController || Candidate->IsActorBeingDestroyed())
        {
            continue;
        }
        if (!OceanSubsystem->IsConfiguredByOwner(Candidate))
        {
            continue;
        }

        UE_LOG(LogRealOcean, Display, TEXT("RealOcean explicit controller %s is taking ownership from automatic controller %s."), *GetName(), *Candidate->GetName());
        if (Candidate->NetworkClockSync.Revision > 0)
        {
            // Preserve the authoritative phase epoch across World Partition/streaming handoff.
            NetworkClockSync = Candidate->NetworkClockSync;
        }
        // Do not fully deconfigure here: that would reset standalone wave/current clocks and create
        // a visible phase jump. Release only ownership; this controller reconfigures immediately.
        if (!OceanSubsystem->ReleaseOwnerForHandoff(Candidate))
        {
            UE_LOG(LogRealOcean, Error, TEXT("RealOcean explicit handoff from %s failed ownership release; keeping the fallback controller."), *Candidate->GetName());
            return;
        }
        Candidate->CleanupTurnkeyRuntimeArtifacts();
        if (HasAuthority())
        {
            Candidate->Destroy();
        }
        break;
    }
}



void ARealOceanController::RegisterLocalPlayerLifecycleDelegates()
{
    UWorld* World = GetWorld();
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    if (!GameInstance)
    {
        return;
    }

    if (!LocalPlayerAddedHandle.IsValid())
    {
        LocalPlayerAddedHandle = GameInstance->OnLocalPlayerAddedEvent.AddUObject(
            this, &ARealOceanController::HandleLocalPlayerTopologyChanged);
    }
    if (!LocalPlayerRemovedHandle.IsValid())
    {
        LocalPlayerRemovedHandle = GameInstance->OnLocalPlayerRemovedEvent.AddUObject(
            this, &ARealOceanController::HandleLocalPlayerTopologyChanged);
    }
}

void ARealOceanController::UnregisterLocalPlayerLifecycleDelegates()
{
    UWorld* World = GetWorld();
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    if (GameInstance)
    {
        if (LocalPlayerAddedHandle.IsValid())
        {
            GameInstance->OnLocalPlayerAddedEvent.Remove(LocalPlayerAddedHandle);
        }
        if (LocalPlayerRemovedHandle.IsValid())
        {
            GameInstance->OnLocalPlayerRemovedEvent.Remove(LocalPlayerRemovedHandle);
        }
    }
    LocalPlayerAddedHandle.Reset();
    LocalPlayerRemovedHandle.Reset();
}

void ARealOceanController::HandleLocalPlayerTopologyChanged(ULocalPlayer* LocalPlayer)
{
    (void)LocalPlayer;
    if (!HasActorBegunPlay() || IsActorBeingDestroyed())
    {
        return;
    }

    UE_LOG(LogRealOcean, Verbose, TEXT("RealOcean detected a local-player topology change; refreshing per-view render bindings."));
    RefreshLocalRenderStateOnly();
}

void ARealOceanController::RefreshLocalRenderStateOnly()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    ERealOceanProfilingVariant EffectiveProfilingVariant = ProfilingVariant;
#if UE_BUILD_SHIPPING
    EffectiveProfilingVariant = ERealOceanProfilingVariant::Full;
#endif

    ERealOceanFoamClipmapQualityPreset LocalFoamPreset = OceanSettings.Surface.FoamClipmapQualityPreset;
    bool bHasLocalFoamPresetOverride = false;
#if !UE_BUILD_SHIPPING
    bHasLocalFoamPresetOverride = ParseFoamPresetFromCommandLine(LocalFoamPreset);
#endif

    bool bForceUnderwaterPostProcessOff = false;
    bool bSuppressMaterialCaustics =
#if UE_BUILD_SHIPPING
        RealOceanRelease::UsesRDGCaustics();
#else
        false;
#endif

    switch (EffectiveProfilingVariant)
    {
    case ERealOceanProfilingVariant::SurfaceOnly:
    case ERealOceanProfilingVariant::MacroSurfaceOnly:
        bForceUnderwaterPostProcessOff = true;
        break;
    case ERealOceanProfilingVariant::RDGCausticsOnly:
        bSuppressMaterialCaustics = true;
        break;
    default:
        break;
    }

    // 0.33.4 generated materials derive camera phase/depth from native per-view material inputs.
    // Multi-view is therefore supported instead of disabling surface/post-process rendering.
    // The single world-space stateful foam clipmap remains shared; views outside it transparently use analytic foam.

    if (UnderwaterPostProcess)
    {
        UnderwaterPostProcess->SetBlendWeightOverride(bForceUnderwaterPostProcessOff, 0.0f);
    }

    if (URealOceanSubsystem* OceanSubsystem = World->GetSubsystem<URealOceanSubsystem>())
    {
        if (OceanSubsystem->IsConfiguredByOwner(this))
        {
            OceanSubsystem->SetLocalRenderProfilingOverrides(
                EffectiveProfilingVariant,
                bHasLocalFoamPresetOverride,
                LocalFoamPreset);
            OceanSubsystem->SetMaterialCausticsSuppressed(bSuppressMaterialCaustics);
        }
    }

    if (World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer)
    {
        BindTurnkeyOceanSurface();
    }
}

bool ARealOceanController::ResolveTurnkeyDefaults()
{
    UWorld* World = GetWorld();
    const bool bDedicatedServer = World && World->GetNetMode() == NM_DedicatedServer;

    if (bAutoResolveTurnkeyAssets && !bDedicatedServer)
    {
        if (!GlobalParameterCollection)
        {
            GlobalParameterCollection = LoadObject<UMaterialParameterCollection>(nullptr,
                TEXT("/Game/RealOceanGenerated/MPC_RealOceanGlobal.MPC_RealOceanGlobal"));
        }

        if (!OceanSurfaceMaterial)
        {
            OceanSurfaceMaterial = LoadObject<UMaterialInterface>(nullptr,
                TEXT("/Game/RealOceanGenerated/MI_RealOceanSurface.MI_RealOceanSurface"));
        }

        if (UnderwaterPostProcess && !UnderwaterPostProcess->UnderwaterPostProcessMaterial)
        {
            UnderwaterPostProcess->UnderwaterPostProcessMaterial = LoadObject<UMaterialInterface>(nullptr,
                TEXT("/Game/RealOceanGenerated/M_PP_RealOceanUnderwater.M_PP_RealOceanUnderwater"));
            if (UnderwaterPostProcess->UnderwaterPostProcessMaterial)
            {
                // Actor BeginPlay occurs after component BeginPlay. Rebuild explicitly when turnkey
                // resolution supplies the material at actor startup.
                UnderwaterPostProcess->RebuildUnderwaterMaterial();
            }
        }
    }

    if ((!IsValid(SunDirectionalLight) || SunDirectionalLight->IsActorBeingDestroyed()) && !bDedicatedServer)
    {
        SunDirectionalLight = DiscoverPreferredSunLight();
    }

    if (bDedicatedServer || !bAutoResolveTurnkeyAssets)
    {
        return true;
    }

    const bool bSurfaceReady = GlobalParameterCollection && OceanSurfaceMaterial;
    const bool bUnderwaterReady = !UnderwaterPostProcess || UnderwaterPostProcess->UnderwaterPostProcessMaterial != nullptr;
    const bool bReady = bSurfaceReady && bUnderwaterReady;
    if (!bReady)
    {
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean turnkey assets are missing or failed to load from /Game/RealOceanGenerated. Reopen the editor and fix any RealOceanEditor compile/save errors before packaging or Play."));
    }
    return bReady;
}

ADirectionalLight* ARealOceanController::DiscoverPreferredSunLight() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    ADirectionalLight* PrimaryAtmosphereSun = nullptr;
    ADirectionalLight* AnyAtmosphereSun = nullptr;
    ADirectionalLight* BrightestFallback = nullptr;
    float PrimaryIntensity = -1.0f;
    float AtmosphereIntensity = -1.0f;
    float FallbackIntensity = -1.0f;

    for (TActorIterator<ADirectionalLight> It(World); It; ++It)
    {
        ADirectionalLight* Candidate = *It;
        if (!Candidate || Candidate->IsActorBeingDestroyed() || Candidate->IsHidden())
        {
            continue;
        }

        UDirectionalLightComponent* Component = Cast<UDirectionalLightComponent>(Candidate->GetLightComponent());
        if (!Component || !Component->IsVisible())
        {
            continue;
        }

        const float Intensity = Component->Intensity;
        if (Component->IsUsedAsAtmosphereSunLight())
        {
            if (Component->GetAtmosphereSunLightIndex() == 0 && Intensity > PrimaryIntensity)
            {
                PrimaryAtmosphereSun = Candidate;
                PrimaryIntensity = Intensity;
            }
            if (Intensity > AtmosphereIntensity)
            {
                AnyAtmosphereSun = Candidate;
                AtmosphereIntensity = Intensity;
            }
        }

        if (Intensity > FallbackIntensity)
        {
            BrightestFallback = Candidate;
            FallbackIntensity = Intensity;
        }
    }

    return PrimaryAtmosphereSun ? PrimaryAtmosphereSun : (AnyAtmosphereSun ? AnyAtmosphereSun : BrightestFallback);
}

AWaterBody* ARealOceanController::EnsureTurnkeyOceanBody()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    if (UWaterSubsystem* WaterSubsystem = World->GetSubsystem<UWaterSubsystem>())
    {
        if (UWaterBodyComponent* RegisteredOcean = WaterSubsystem->GetOceanBodyComponent().Get())
        {
            if (AWaterBody* RegisteredActor = Cast<AWaterBody>(RegisteredOcean->GetOwner()))
            {
                if (!RegisteredActor->IsActorBeingDestroyed())
                {
                    return RegisteredActor;
                }
            }
        }
    }

    for (TActorIterator<AWaterBody> It(World); It; ++It)
    {
        if (!It->IsActorBeingDestroyed() && It->GetWaterBodyType() == EWaterBodyType::Ocean)
        {
            return *It;
        }
    }

    if (!bAutoCreateWaterWorldWhenMissing || !World->IsGameWorld())
    {
        return nullptr;
    }

    AWaterZone* WaterZone = nullptr;
    for (TActorIterator<AWaterZone> It(World); It; ++It)
    {
        if (!It->IsActorBeingDestroyed())
        {
            WaterZone = *It;
            break;
        }
    }

    const double ExtentCm = FMath::Clamp(TurnkeyOceanExtentMeters, 1000.0, 100000.0) * 100.0;
    const double SeaLevelCm = OceanSettings.ManualSeaLevelCm;

    if (!WaterZone)
    {
        FActorSpawnParameters ZoneSpawn;
        ZoneSpawn.Name = MakeUniqueObjectName(World, AWaterZone::StaticClass(), TEXT("RealOcean_AutoWaterZone"));
        ZoneSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        WaterZone = World->SpawnActor<AWaterZone>(AWaterZone::StaticClass(), FVector(0.0, 0.0, SeaLevelCm), FRotator::ZeroRotator, ZoneSpawn);
        if (WaterZone)
        {
            WaterZone->SetZoneExtent(FVector2D(ExtentCm, ExtentCm));
            TurnkeyAutoCreatedWaterZone = WaterZone;
        }
    }

    if (!WaterZone)
    {
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean turnkey bootstrap could not create or find a WaterZone; refusing to create an orphan WaterBodyOcean."));
        return nullptr;
    }

    FActorSpawnParameters OceanSpawn;
    OceanSpawn.Name = MakeUniqueObjectName(World, AWaterBodyOcean::StaticClass(), TEXT("RealOcean_AutoWaterBodyOcean"));
    OceanSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AWaterBodyOcean* Ocean = World->SpawnActor<AWaterBodyOcean>(AWaterBodyOcean::StaticClass(), FVector(0.0, 0.0, SeaLevelCm), FRotator::ZeroRotator, OceanSpawn);
    if (!Ocean)
    {
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean turnkey bootstrap could not create WaterBodyOcean."));
        if (TurnkeyAutoCreatedWaterZone.IsValid())
        {
            TurnkeyAutoCreatedWaterZone->Destroy();
            TurnkeyAutoCreatedWaterZone.Reset();
        }
        return nullptr;
    }
    TurnkeyAutoCreatedOcean = Ocean;

    if (UWaterBodyOceanComponent* OceanComponent = Cast<UWaterBodyOceanComponent>(Ocean->GetWaterBodyComponent()))
    {
        OceanComponent->SetOceanExtent(FVector2D(ExtentCm, ExtentCm));
        if (WaterZone)
        {
            OceanComponent->SetWaterZoneOverride(TSoftObjectPtr<AWaterZone>(WaterZone));
            OceanComponent->FillWaterZoneWithOcean();
        }
    }

PRAGMA_DISABLE_DEPRECATION_WARNINGS
    Ocean->OnWaterBodyChanged(true, false);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
    UE_LOG(LogRealOcean, Display, TEXT("RealOcean turnkey bootstrap created a %.0f m WaterBodyOcean fallback."), TurnkeyOceanExtentMeters);
    return Ocean;
}

void ARealOceanController::BindTurnkeyOceanSurface()
{
    UWorld* World = GetWorld();
    if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    if (!bAutoBindOceanSurfaceMaterial || !OceanSurfaceMaterial)
    {
        return;
    }
    if (bAutoResolveTurnkeyAssets && !bTurnkeyAssetsReady)
    {
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean skipped automatic water material binding because generated turnkey assets are not ready."));
        return;
    }

    AWaterBody* Ocean = EnsureTurnkeyOceanBody();
    if (!Ocean)
    {
        return;
    }

    if (TurnkeyBoundOcean.Get() != Ocean)
    {
        RestoreTurnkeyOceanSurface();
        TurnkeyBoundOcean = Ocean;
        UMaterialInterface* CapturedMaterial = nullptr;
        bCapturedOriginalOceanMaterial = TryReadConfiguredWaterMaterial(Ocean, CapturedMaterial);
        OriginalOceanSurfaceMaterial = CapturedMaterial;
        if (!bCapturedOriginalOceanMaterial && !TurnkeyAutoCreatedOcean.IsValid())
        {
            UE_LOG(LogRealOcean, Warning, TEXT("RealOcean could not read the existing WaterBodyOcean material for later restoration; it will leave the assigned RealOcean material in place on controller teardown."));
        }
        TurnkeyOceanMID = nullptr;
    }

    // Reuse the MID while parent/ocean remain stable. ApplySettings can run repeatedly at runtime;
    // creating a new MID every call caused unnecessary GC pressure and briefly broke foam rebinds.
    if (!TurnkeyOceanMID || TurnkeyOceanMID->Parent != OceanSurfaceMaterial)
    {
        TurnkeyOceanMID = UMaterialInstanceDynamic::Create(OceanSurfaceMaterial, Ocean);
    }

    UMaterialInterface* MaterialToAssign = TurnkeyOceanMID ? static_cast<UMaterialInterface*>(TurnkeyOceanMID.Get()) : OceanSurfaceMaterial.Get();
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    Ocean->SetWaterMaterial(MaterialToAssign);
    Ocean->OnWaterBodyChanged(false, false);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

    if (TurnkeyOceanMID)
    {
        if (URealOceanSubsystem* OceanSubsystem = World->GetSubsystem<URealOceanSubsystem>())
        {
            OceanSubsystem->BindFoamClipmapToMaterial(TurnkeyOceanMID);
        }
    }
}

void ARealOceanController::RestoreTurnkeyOceanSurface()
{
    AWaterBody* Ocean = TurnkeyBoundOcean.Get();
    if (Ocean && bCapturedOriginalOceanMaterial && !TurnkeyAutoCreatedOcean.IsValid())
    {
PRAGMA_DISABLE_DEPRECATION_WARNINGS
        Ocean->SetWaterMaterial(OriginalOceanSurfaceMaterial);
        Ocean->OnWaterBodyChanged(false, false);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }

    TurnkeyOceanMID = nullptr;
    TurnkeyBoundOcean.Reset();
    OriginalOceanSurfaceMaterial = nullptr;
    bCapturedOriginalOceanMaterial = false;
}

void ARealOceanController::CleanupTurnkeyRuntimeArtifacts()
{
    RestoreTurnkeyOceanSurface();

    if (TurnkeyAutoCreatedOcean.IsValid())
    {
        TurnkeyAutoCreatedOcean->Destroy();
        TurnkeyAutoCreatedOcean.Reset();
    }
    if (TurnkeyAutoCreatedWaterZone.IsValid())
    {
        TurnkeyAutoCreatedWaterZone->Destroy();
        TurnkeyAutoCreatedWaterZone.Reset();
    }
}


void ARealOceanController::SetProfilingVariant(ERealOceanProfilingVariant Variant)
{
#if UE_BUILD_SHIPPING
    ProfilingVariant = ERealOceanProfilingVariant::Full;
#else
    ProfilingVariant = Variant;
#endif
    RefreshLocalRenderStateOnly();
}

void ARealOceanController::ClearProfilingVariant()
{
    ProfilingVariant = ERealOceanProfilingVariant::Full;
    RefreshLocalRenderStateOnly();
}

void ARealOceanController::ValidateSetup()
{
    URealOceanValidationLibrary::ValidateController(this, true);
}

void ARealOceanController::TrackActor(AActor* ActorToTrack)
{
    if (UWorld* World = GetWorld())
    {
        if (URealOceanSubsystem* OceanSubsystem = World->GetSubsystem<URealOceanSubsystem>())
        {
            OceanSubsystem->SetTrackedActor(ActorToTrack);
        }
    }
}

FRealOceanState ARealOceanController::GetOceanState() const
{
    if (UWorld* World = GetWorld())
    {
        if (URealOceanSubsystem* OceanSubsystem = World->GetSubsystem<URealOceanSubsystem>())
        {
            return OceanSubsystem->GetOceanState();
        }
    }

    return FRealOceanState();
}

FRealOceanEnvironmentSample ARealOceanController::QueryEnvironmentAtWorldPosition(FVector WorldPositionCm) const
{
    if (UWorld* World = GetWorld())
    {
        if (URealOceanSubsystem* OceanSubsystem = World->GetSubsystem<URealOceanSubsystem>())
        {
            return OceanSubsystem->QueryEnvironmentAtWorldPosition(WorldPositionCm);
        }
    }

    FRealOceanEnvironmentSample Empty;
    Empty.WorldPositionCm = WorldPositionCm;
    return Empty;
}
