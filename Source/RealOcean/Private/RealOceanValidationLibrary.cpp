#include "RealOceanValidationLibrary.h"

#include "RealOcean.h"
#include "RealOceanController.h"
#include "RealOceanPostProcessComponent.h"
#include "RealOceanWaterProfile.h"
#include "RealOceanReleaseConfig.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Engine/BlendableInterface.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "WaterSubsystem.h"
#include "WaterBodyActor.h"
#include "WaterBodyTypes.h"

namespace
{
    void AddMessage(FRealOceanValidationReport& Report, ERealOceanValidationSeverity Severity, const FString& Message)
    {
        FRealOceanValidationMessage& Entry = Report.Messages.AddDefaulted_GetRef();
        Entry.Severity = Severity;
        Entry.Message = Message;

        if (Severity == ERealOceanValidationSeverity::Error)
        {
            ++Report.ErrorCount;
        }
        else if (Severity == ERealOceanValidationSeverity::Warning)
        {
            ++Report.WarningCount;
        }
    }

    TSet<FName> ToNameSet(const TArray<FCollectionScalarParameter>& Parameters)
    {
        TSet<FName> Names;
        Names.Reserve(Parameters.Num());
        for (const FCollectionScalarParameter& Parameter : Parameters)
        {
            Names.Add(Parameter.ParameterName);
        }
        return Names;
    }

    TSet<FName> ToNameSet(const TArray<FCollectionVectorParameter>& Parameters)
    {
        TSet<FName> Names;
        Names.Reserve(Parameters.Num());
        for (const FCollectionVectorParameter& Parameter : Parameters)
        {
            Names.Add(Parameter.ParameterName);
        }
        return Names;
    }
}


FString URealOceanValidationLibrary::GetCompiledReleaseCausticsPath()
{
    return FString(RealOceanRelease::GetCompiledCausticsPathName());
}

bool URealOceanValidationLibrary::IsCompiledRDGCausticsReleasePath()
{
    return RealOceanRelease::UsesRDGCaustics();
}

FString URealOceanValidationLibrary::GetCompiledReleaseFoamPath()
{
    return FString(RealOceanRelease::GetCompiledFoamPathName());
}

bool URealOceanValidationLibrary::IsCompiledGPUFoamReleasePath()
{
    return RealOceanRelease::UsesGPUFoamClipmap();
}


bool URealOceanValidationLibrary::IsGPUFoamReleaseCertified()
{
    return RealOceanRelease::HasCertifiedGPUFoamRelease();
}

FString URealOceanValidationLibrary::GetCertifiedGPUFoamPreset()
{
    return FString(RealOceanRelease::GetCertifiedFoamPresetName());
}

int32 URealOceanValidationLibrary::GetCertifiedGPUFoamResolution()
{
    return RealOceanRelease::GetCertifiedFoamResolution();
}

int32 URealOceanValidationLibrary::GetCertifiedGPUFoamUpdateRateHz()
{
    return RealOceanRelease::GetCertifiedFoamUpdateRateHz();
}

FString URealOceanValidationLibrary::GetCompiledReleaseBundleId()
{
    return RealOceanRelease::GetReleaseBundleId();
}

const TArray<FName>& URealOceanValidationLibrary::GetRequiredScalarParameters()
{
    static const TArray<FName> Names = {
        TEXT("RO_ContractVersion"),
        TEXT("RO_SeaLevelCm"), TEXT("RO_DepthMeters"), TEXT("RO_RawDepthMeters"), TEXT("RO_BelowConfiguredBottom"),
        TEXT("RO_NormalizedDepth"), TEXT("RO_GaugePressureBar"), TEXT("RO_AbsolutePressureBar"), TEXT("RO_UnderwaterBlend"),
        TEXT("RO_VisibilityMeters"), TEXT("RO_DepthZone"), TEXT("RO_MaxDepthMeters"), TEXT("RO_UnderwaterTransitionMeters"), TEXT("RO_GravityMS2"),
        TEXT("RO_Turbidity"), TEXT("RO_InscatterIntensity"), TEXT("RO_MaxOpticalPathMeters"), TEXT("RO_AphoticDepthMeters"),
        TEXT("RO_QualityMode"), TEXT("RO_DebugView"), TEXT("RO_DebugOpacity"), TEXT("RO_SunlightTransmission"),
        TEXT("RO_SunIntensity"), TEXT("RO_SunRelativeIntensity"), TEXT("RO_SunCosZenithAir"), TEXT("RO_SunCosZenithWater"),
        TEXT("RO_DirectSunPathMeters"), TEXT("RO_ForwardScatteringG"), TEXT("RO_OceanTimeSeconds"),
        TEXT("RO_WaveTravelDirectionDegrees"), TEXT("RO_WindDirectionDegrees"), TEXT("RO_WindSpeedMps"), TEXT("RO_SignificantWaveHeightMeters"),
        TEXT("RO_SwellSignificantWaveHeightMeters"), TEXT("RO_WindSeaSignificantWaveHeightMeters"),
        TEXT("RO_DominantSwellPeriodSeconds"), TEXT("RO_WindSeaPeakPeriodSeconds"), TEXT("RO_WindSeaFetchKm"),
        TEXT("RO_WindSeaEnergyScale"), TEXT("RO_DirectionalSpread"), TEXT("RO_WindSeaDirectionalSpread"),
        TEXT("RO_WaveSteepness"), TEXT("RO_WindSeaSteepnessScale"),
        TEXT("RO_AirIOR"), TEXT("RO_WaterIOR"), TEXT("RO_CriticalAngleDegrees"), TEXT("RO_SurfaceRoughness"),
        TEXT("RO_MicroNormalStrength"), TEXT("RO_BreakingStrength"), TEXT("RO_WhitecapCoverageScale"),
        TEXT("RO_FoamMemorySeconds"), TEXT("RO_FoamDecayHalfLifeSeconds"), TEXT("RO_FoamWindDriftFraction"),
        TEXT("RO_FoamSurfaceCurrentInfluence"), TEXT("RO_FoamClipmapCoverageMeters"), TEXT("RO_FoamClipmapValid"), TEXT("RO_FoamClipmapBlend"),
        TEXT("RO_FoamClipmapEdgeFadeMeters"), TEXT("RO_FoamClipmapResolution"), TEXT("RO_FoamClipmapTexelMeters"), TEXT("RO_FoamClipmapUpdateRateHz"),
        TEXT("RO_MicroDetailFadeStartMeters"), TEXT("RO_MicroDetailFadeEndMeters"), TEXT("RO_HorizonRoughnessBoost"),
        TEXT("RO_CausticsStrength"), TEXT("RO_CausticsEnabled"),
        TEXT("RO_CausticsMaxDepthMeters"), TEXT("RO_CausticsIntensity"), TEXT("RO_CausticsContrast"),
        TEXT("RO_CausticsFocusClamp"), TEXT("RO_CausticsDerivativeStepMeters"), TEXT("RO_CausticsScreenSpaceStability"),
        TEXT("RO_EffectiveCausticsMaxDepthMeters"), TEXT("RO_CausticsSolveIterations"), TEXT("RO_CausticsDerivativeScale")
    };
    return Names;
}

const TArray<FName>& URealOceanValidationLibrary::GetRequiredVectorParameters()
{
    static const TArray<FName> Names = {
        TEXT("RO_WaveDirectionXY"), TEXT("RO_SurfaceCurrentVelocityXYMps"), TEXT("RO_FoamClipmapCenterOffsetMeters"), TEXT("RO_FoamClipmapCenterWorldXYMeters"),
        TEXT("RO_WaveAnchorPhase0123"), TEXT("RO_WaveAnchorPhase4567"), TEXT("RO_MicroWaveAnchorPhase0123"),
        TEXT("RO_AbsorptionRGB"), TEXT("RO_ScatteringRGB"), TEXT("RO_ExtinctionRGB"), TEXT("RO_InscatterColor"),
        TEXT("RO_SunDirectionToSourceWS"), TEXT("RO_SunColor"), TEXT("RO_SunTransmissionRGB")
    };
    return Names;
}

FRealOceanValidationReport URealOceanValidationLibrary::ValidateController(const ARealOceanController* Controller, bool bLogResults)
{
    FRealOceanValidationReport Report;

    if (!Controller)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Error, TEXT("RealOceanController is null."));
        Report.bReadyForRuntime = false;
        return Report;
    }

    if (!FMath::IsNearlyEqual(Controller->OceanSettings.MaximumDepthMeters, 3000.0, 0.001))
    {
        AddMessage(Report, ERealOceanValidationSeverity::Warning,
            TEXT("MaximumDepthMeters is serialized with a non-3000 value. Runtime clamps the contract to exactly 3000 m."));
    }

    if (Controller->OceanSettings.Surface.WaterRefractiveIndex <= Controller->OceanSettings.Surface.AirRefractiveIndex)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Error,
            TEXT("WaterRefractiveIndex must be greater than AirRefractiveIndex for the configured underwater Fresnel/TIR model."));
    }

    if (Controller->OceanSettings.Waves.WindSeaFetchLengthKm <= 0.0)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Error,
            TEXT("WindSeaFetchLengthKm must be positive for the M26 fetch-limited wind-sea spectrum."));
    }

    if (Controller->OceanSettings.Surface.MicroDetailFadeEndMeters <= Controller->OceanSettings.Surface.MicroDetailFadeStartMeters)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Error,
            TEXT("MicroDetailFadeEndMeters must be greater than MicroDetailFadeStartMeters."));
    }

    if (Controller->OceanSettings.Lighting.FullDaylightElevationDegrees <= 0.0
        || Controller->OceanSettings.Lighting.FullDaylightElevationDegrees <= Controller->OceanSettings.Lighting.TwilightStartElevationDegrees)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Error,
            TEXT("Day-cycle FullDaylightElevationDegrees must be positive and greater than TwilightStartElevationDegrees."));
    }

    if (Controller->OceanSettings.Lighting.GoldenHourEndElevationDegrees <= 0.0)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Error,
            TEXT("GoldenHourEndElevationDegrees must be positive."));
    }

    if (Controller->OceanSettings.Lighting.NightInscatterScale > Controller->OceanSettings.Lighting.TwilightInscatterScale)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Warning,
            TEXT("NightInscatterScale is greater than TwilightInscatterScale. Runtime clamps twilight ambient above the night level."));
    }

    const FVector DayCycleTau = Controller->OceanSettings.Lighting.DayCycleAtmosphericOpticalDepthRGB;
    if (DayCycleTau.X < 0.0 || DayCycleTau.Y < 0.0 || DayCycleTau.Z < 0.0)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Error,
            TEXT("DayCycleAtmosphericOpticalDepthRGB must be non-negative in all channels."));
    }

    if (Controller->OceanSettings.Surface.FoamDecayHalfLifeSeconds <= 0.0)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Error,
            TEXT("FoamDecayHalfLifeSeconds must be positive when foam persistence is enabled."));
    }

    if (Controller->OceanSettings.Surface.bEnableGPUFoamClipmap)
    {
        int32 Resolution = 512;
        double UpdateRateHz = 60.0;
        switch (Controller->OceanSettings.Surface.FoamClipmapQualityPreset)
        {
        case ERealOceanFoamClipmapQualityPreset::Low256:    Resolution = 256; UpdateRateHz = 30.0; break;
        case ERealOceanFoamClipmapQualityPreset::Medium384: Resolution = 384; UpdateRateHz = 45.0; break;
        case ERealOceanFoamClipmapQualityPreset::High512:   Resolution = 512; UpdateRateHz = 60.0; break;
        case ERealOceanFoamClipmapQualityPreset::Custom:
        default:
            Resolution = FMath::Clamp(FMath::RoundToInt(static_cast<double>(Controller->OceanSettings.Surface.FoamClipmapResolution) / 64.0) * 64, 128, 1024);
            UpdateRateHz = FMath::Clamp(Controller->OceanSettings.Surface.FoamClipmapUpdateRateHz, 10.0, 120.0);
            if (Controller->OceanSettings.Surface.FoamClipmapResolution < 128 || Controller->OceanSettings.Surface.FoamClipmapResolution > 1024
                || Controller->OceanSettings.Surface.FoamClipmapResolution % 64 != 0)
            {
                AddMessage(Report, ERealOceanValidationSeverity::Warning,
                    TEXT("Custom FoamClipmapResolution should be a 64-texel multiple in [128, 1024]. Runtime aligns and clamps it."));
            }
            break;
        }
        if (Controller->OceanSettings.Surface.FoamClipmapCoverageMeters < 64.0)
        {
            AddMessage(Report, ERealOceanValidationSeverity::Error,
                TEXT("FoamClipmapCoverageMeters must be at least 64 m for the M29 clipmap path."));
        }
        const double CoverageMeters = FMath::Clamp(Controller->OceanSettings.Surface.FoamClipmapCoverageMeters, 64.0, 1024.0);
        const double TexelMeters = CoverageMeters / static_cast<double>(Resolution);
        if (TexelMeters > 1.5)
        {
            AddMessage(Report, ERealOceanValidationSeverity::Warning,
                TEXT("GPU foam clipmap texel size exceeds 1.5 m; narrow breaker streaks may alias before TSR."));
        }
        if (Controller->OceanSettings.Surface.FoamClipmapEdgeFadeMeters * 2.0 >= CoverageMeters)
        {
            AddMessage(Report, ERealOceanValidationSeverity::Warning,
                TEXT("FoamClipmapEdgeFadeMeters consumes at least half the clipmap width. Reduce it to preserve a fully stateful center region."));
        }
        if (UpdateRateHz < 30.0)
        {
            AddMessage(Report, ERealOceanValidationSeverity::Info,
                TEXT("GPU foam clipmap updates below 30 Hz can show stepping during rapid foam advection; verify with TSR in motion."));
        }
    }

    const double MeanFoamAdvectionMps =
        Controller->OceanSettings.Waves.WindSpeedMetersPerSecond * Controller->OceanSettings.Surface.FoamWindDriftFraction
        + Controller->OceanSettings.Current.SurfaceSpeedMetersPerSecond * Controller->OceanSettings.Surface.FoamSurfaceCurrentInfluence;
    if (Controller->OceanSettings.Surface.FoamMemorySeconds > 4.0 && MeanFoamAdvectionMps > 1.0)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Warning,
            TEXT("Long foam memory combined with fast wind/current advection can create overlong streaks. Review Cinematic motion and reduce FoamMemorySeconds or advection influence if needed."));
    }

    if (FMath::Abs(Controller->OceanSettings.Waves.WindDirectionDegrees - Controller->OceanSettings.Waves.TravelDirectionDegrees) > 170.0
        && Controller->OceanSettings.Waves.WindSpeedMetersPerSecond > 12.0)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Info,
            TEXT("Strong wind opposes the swell direction; crossed-sea breaking/whitecaps may become visually aggressive. This is valid but should be reviewed in motion."));
    }

    const UWorld* ValidationWorld = Controller->GetWorld();
    const bool bDedicatedServer = ValidationWorld && ValidationWorld->GetNetMode() == NM_DedicatedServer;

    if (!bDedicatedServer && !Controller->SunDirectionalLight)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Warning,
            TEXT("No Directional Light is assigned. RealOcean will use the manual sun direction/color/intensity fallback."));
    }

    if (UWorld* World = Controller->GetWorld())
    {
        int32 ControllerCount = 0;
        for (TActorIterator<ARealOceanController> It(World); It; ++It)
        {
            if (!It->IsActorBeingDestroyed())
            {
                ++ControllerCount;
            }
        }
        if (ControllerCount > 1)
        {
            AddMessage(Report, ERealOceanValidationSeverity::Error,
                FString::Printf(TEXT("%d RealOceanController actors are active in one world. RealOcean now locks the first live configuration owner and rejects later controllers; remove duplicates for deterministic turnkey behavior."), ControllerCount));
        }

        if (!bDedicatedServer)
        {
            UWaterSubsystem* WaterSubsystem = World->GetSubsystem<UWaterSubsystem>();
            if (!WaterSubsystem)
            {
                AddMessage(Report, ERealOceanValidationSeverity::Error,
                    TEXT("UE Water subsystem is unavailable in this world. Verify the Water plugin is enabled and this is a supported runtime/editor world."));
            }
            else
            {
                if (!WaterSubsystem->IsWaterRenderingEnabled())
                {
                    AddMessage(Report, ERealOceanValidationSeverity::Warning,
                        TEXT("UE Water rendering is currently disabled. RealOcean optics can still run, but the WaterBodyOcean surface mesh will not render."));
                }

                if (!WaterSubsystem->GetOceanBodyComponent().IsValid())
                {
                    bool bOceanActorExists = false;
                    for (TActorIterator<AWaterBody> It(World); It; ++It)
                    {
                        if (!It->IsActorBeingDestroyed() && It->GetWaterBodyType() == EWaterBodyType::Ocean)
                        {
                            bOceanActorExists = true;
                            break;
                        }
                    }

                    const bool bExpectedTurnkeyRegistrationDelay = Controller->bAutoCreateWaterWorldWhenMissing && bOceanActorExists;
                    const ERealOceanValidationSeverity Severity = bExpectedTurnkeyRegistrationDelay
                        ? ERealOceanValidationSeverity::Warning
                        : (Controller->OceanSettings.bUseWaterSubsystemSeaLevel ? ERealOceanValidationSeverity::Error : ERealOceanValidationSeverity::Warning);
                    AddMessage(Report, Severity, bExpectedTurnkeyRegistrationDelay
                        ? TEXT("A WaterBodyOcean actor exists but WaterSubsystem registration is not visible yet. Turnkey runtime will re-evaluate on the next subsystem tick; verify only if this persists.")
                        : TEXT("No UE Water ocean body is registered. Add a WaterBodyOcean, or disable bUseWaterSubsystemSeaLevel and use ManualSeaLevelCm."));
                }
            }
        }
        else
        {
            AddMessage(Report, ERealOceanValidationSeverity::Info,
                TEXT("Dedicated server: visual Water/MPC/post-process validation is intentionally skipped; authoritative RealOcean environment queries remain configured."));
        }
    }
    else
    {
        AddMessage(Report, ERealOceanValidationSeverity::Warning,
            TEXT("Controller has no world yet; Water subsystem readiness could not be checked."));
    }

    if (Controller->WaterProfile && Controller->WaterProfile->ProfileVersion != 1)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Warning,
            FString::Printf(TEXT("Water profile version %d is not the release-candidate profile schema version 1. Verify optical fields before shipping."), Controller->WaterProfile->ProfileVersion));
    }

    if (!bDedicatedServer)
    {
    if (Controller->bAutoBindOceanSurfaceMaterial && !Controller->OceanSurfaceMaterial)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Error,
            TEXT("OceanSurfaceMaterial is not assigned. The turnkey installer normally generates and auto-resolves /Game/RealOceanGenerated/MI_RealOceanSurface."));
    }

    UMaterialParameterCollection* MPC = Controller->GlobalParameterCollection;
    if (!MPC)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Error,
            TEXT("GlobalParameterCollection is not assigned. Reopen the Unreal Editor so the RealOcean turnkey installer can generate /Game/RealOceanGenerated/MPC_RealOceanGlobal, or assign a compatible MPC manually."));
    }
    else
    {
        const TSet<FName> ScalarNames = ToNameSet(MPC->ScalarParameters);
        const TSet<FName> VectorNames = ToNameSet(MPC->VectorParameters);

        if (MPC->ScalarParameters.Num() != GetRequiredScalarParameters().Num()
            || MPC->VectorParameters.Num() != GetRequiredVectorParameters().Num())
        {
            AddMessage(Report, ERealOceanValidationSeverity::Error,
                FString::Printf(TEXT("MPC schema drift: expected %d scalar / %d vector parameters, found %d / %d. Run RealOcean.RebuildTurnkeyContent."),
                    GetRequiredScalarParameters().Num(), GetRequiredVectorParameters().Num(), MPC->ScalarParameters.Num(), MPC->VectorParameters.Num()));
        }

        TArray<FString> MissingScalars;
        for (const FName& Required : GetRequiredScalarParameters())
        {
            if (!ScalarNames.Contains(Required))
            {
                MissingScalars.Add(Required.ToString());
            }
        }

        TArray<FString> MissingVectors;
        for (const FName& Required : GetRequiredVectorParameters())
        {
            if (!VectorNames.Contains(Required))
            {
                MissingVectors.Add(Required.ToString());
            }
        }

        if (MissingScalars.Num() > 0)
        {
            AddMessage(Report, ERealOceanValidationSeverity::Error,
                FString::Printf(TEXT("MPC is missing scalar parameters: %s"), *FString::Join(MissingScalars, TEXT(", "))));
        }

        if (MissingVectors.Num() > 0)
        {
            AddMessage(Report, ERealOceanValidationSeverity::Error,
                FString::Printf(TEXT("MPC is missing vector parameters: %s"), *FString::Join(MissingVectors, TEXT(", "))));
        }

        if (!MPC->HasValidUniformBufferStruct())
        {
            AddMessage(Report, ERealOceanValidationSeverity::Warning,
                TEXT("MPC uniform-buffer structure is not currently valid. Save/recompile dependent materials after updating the collection."));
        }
    }

    if (!Controller->UnderwaterPostProcess)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Error, TEXT("UnderwaterPostProcess component is missing."));
    }
    else if (!Controller->UnderwaterPostProcess->UnderwaterPostProcessMaterial)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Error,
            TEXT("UnderwaterPostProcessMaterial is not assigned. The turnkey installer normally generates and auto-resolves /Game/RealOceanGenerated/M_PP_RealOceanUnderwater."));
    }
    else
    {
        const UMaterialInterface* MaterialInterface = Controller->UnderwaterPostProcess->UnderwaterPostProcessMaterial;
        const UMaterial* BaseMaterial = MaterialInterface->GetMaterial();
        if (!BaseMaterial)
        {
            AddMessage(Report, ERealOceanValidationSeverity::Error, TEXT("Unable to resolve the base underwater post-process material."));
        }
        else
        {
            if (BaseMaterial->MaterialDomain != MD_PostProcess)
            {
                AddMessage(Report, ERealOceanValidationSeverity::Error,
                    TEXT("Underwater material must use Material Domain = Post Process."));
            }

            if (!BaseMaterial->bIsBlendable)
            {
                AddMessage(Report, ERealOceanValidationSeverity::Error,
                    TEXT("Underwater post-process material has Is Blendable disabled."));
            }

            const EBlendableLocation Location = MaterialInterface->GetBlendableLocation(BaseMaterial);
            if (Location != BL_SceneColorBeforeDOF)
            {
                AddMessage(Report, ERealOceanValidationSeverity::Error,
                    TEXT("Underwater post-process Blendable Location must be Scene Color Before DOF so RealOcean executes before temporal upscaling in UE 5.8."));
            }
        }
    }

    } // !bDedicatedServer visual validation

    if (Report.ErrorCount == 0)
    {
        AddMessage(Report, ERealOceanValidationSeverity::Info,
            FString::Printf(TEXT("RealOcean release core is ready with MPC contract v%d / shader contract v%d; physical ocean depth is fixed at 3000 m; compiled Shipping caustics path = %s."),
                GetMPCContractVersion(), GetShaderContractVersion(), RealOceanRelease::GetCompiledCausticsPathName()));
    }

    Report.bReadyForRuntime = (Report.ErrorCount == 0);

    if (bLogResults)
    {
        for (const FRealOceanValidationMessage& Message : Report.Messages)
        {
            switch (Message.Severity)
            {
                case ERealOceanValidationSeverity::Error:
                    UE_LOG(LogRealOcean, Error, TEXT("%s"), *Message.Message);
                    break;
                case ERealOceanValidationSeverity::Warning:
                    UE_LOG(LogRealOcean, Warning, TEXT("%s"), *Message.Message);
                    break;
                default:
                    UE_LOG(LogRealOcean, Display, TEXT("%s"), *Message.Message);
                    break;
            }
        }
    }

    return Report;
}
