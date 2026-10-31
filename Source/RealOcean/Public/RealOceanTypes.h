#pragma once

#include "CoreMinimal.h"
#include "RealOceanTypes.generated.h"

UENUM(BlueprintType)
enum class ERealOceanDepthZone : uint8
{
    AboveSurface UMETA(DisplayName = "Above Surface"),
    Epipelagic   UMETA(DisplayName = "Epipelagic 0-200 m"),
    Mesopelagic  UMETA(DisplayName = "Mesopelagic 200-1000 m"),
    Bathypelagic UMETA(DisplayName = "Bathypelagic 1000-3000 m")
};

UENUM(BlueprintType)
enum class ERealOceanQualityMode : uint8
{
    Performance UMETA(DisplayName = "Performance"),
    Balanced    UMETA(DisplayName = "Balanced"),
    Cinematic   UMETA(DisplayName = "Cinematic")
};

UENUM(BlueprintType)
enum class ERealOceanFoamClipmapQualityPreset : uint8
{
    Low256    UMETA(DisplayName = "Low — 256 @ 30 Hz"),
    Medium384 UMETA(DisplayName = "Medium — 384 @ 45 Hz"),
    High512   UMETA(DisplayName = "High — 512 @ 60 Hz"),
    Custom    UMETA(DisplayName = "Custom")
};

UENUM(BlueprintType)
enum class ERealOceanDebugView : uint8
{
    None           UMETA(DisplayName = "None"),
    Depth          UMETA(DisplayName = "Depth"),
    Transmission   UMETA(DisplayName = "RGB Transmission"),
    SunPath        UMETA(DisplayName = "Direct Sun Path"),
    WaveNormal     UMETA(DisplayName = "Wave Normal"),
    CausticFocus   UMETA(DisplayName = "Caustic Focus"),
    PhaseStability UMETA(DisplayName = "LWC Phase Stability")
};

UENUM(BlueprintType)
enum class ERealOceanDayPhase : uint8
{
    Night   UMETA(DisplayName = "Night"),
    Dawn    UMETA(DisplayName = "Dawn"),
    Morning UMETA(DisplayName = "Morning"),
    Midday  UMETA(DisplayName = "Midday"),
    Evening UMETA(DisplayName = "Evening"),
    Dusk    UMETA(DisplayName = "Dusk")
};

// Non-shipping profiling variants for same-scene A/B GPU captures. They do not change the
// physical 3000 m ocean contract and require no additional MPC parameters.
UENUM(BlueprintType)
enum class ERealOceanProfilingVariant : uint8
{
    Full             UMETA(DisplayName = "Full"),
    NoCaustics       UMETA(DisplayName = "No Caustics"),
    NoMicroWaves     UMETA(DisplayName = "No Micro Waves"),
    SurfaceOnly      UMETA(DisplayName = "Surface Only"),
    MacroSurfaceOnly UMETA(DisplayName = "Macro Surface Only"),
    RDGCausticsOnly  UMETA(DisplayName = "RDG Caustics Only (Material Caustics Suppressed)"),
    NoGPUFoam        UMETA(DisplayName = "No GPU Foam (Analytic M27 Fallback)")
};

USTRUCT(BlueprintType)
struct REALOCEAN_API FRealOceanWaveSettings
{
    GENERATED_BODY()

    // Direction the dominant wave field travels toward in Unreal XY space,
    // degrees counter-clockwise from +X.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Waves", meta=(ClampMin="-360.0", ClampMax="360.0", Units="deg"))
    double TravelDirectionDegrees = 225.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Waves", meta=(ClampMin="0.0", ClampMax="60.0", Units="m/s"))
    double WindSpeedMetersPerSecond = 8.0;

    // Direction the local wind-sea travels toward. Swell can arrive from TravelDirectionDegrees independently.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Waves|Wind Sea", meta=(ClampMin="-360.0", ClampMax="360.0", Units="deg"))
    double WindDirectionDegrees = 210.0;

    // Effective open-water fetch used by the fetch-limited JONSWAP-style wind-sea estimate.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Waves|Wind Sea", meta=(ClampMin="0.1", ClampMax="2000.0", Units="km"))
    double WindSeaFetchLengthKm = 120.0;

    // Scales the fetch-limited local wind-sea energy before it is capped by the requested total Hs.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Waves|Wind Sea", meta=(ClampMin="0.0", ClampMax="2.0"))
    double WindSeaEnergyScale = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Waves|Wind Sea", meta=(ClampMin="0.0", ClampMax="1.0"))
    double WindSeaDirectionalSpread = 0.72;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Waves|Wind Sea", meta=(ClampMin="0.1", ClampMax="2.0"))
    double WindSeaSteepnessScale = 0.88;

    // Hs is the combined sea-state target. M26 partitions its energy and variance-normalizes band amplitudes so Hs = 4*sigma.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Waves", meta=(ClampMin="0.0", ClampMax="20.0", Units="m"))
    double SignificantWaveHeightMeters = 2.0;

    // Dominant swell period. Deep-water wavelength is derived from L = g*T^2/(2*pi).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Waves", meta=(ClampMin="2.0", ClampMax="30.0", Units="s"))
    double DominantSwellPeriodSeconds = 10.5;

    // 0 = nearly one-directional swell, 1 = broad directional distribution.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Waves", meta=(ClampMin="0.0", ClampMax="1.0"))
    double DirectionalSpread = 0.35;

    // Horizontal Gerstner displacement factor. Kept below 1 to reduce self-intersection.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Waves", meta=(ClampMin="0.0", ClampMax="0.95"))
    double Steepness = 0.62;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Waves", meta=(ClampMin="0.0", ClampMax="4.0"))
    double TimeScale = 1.0;
};

USTRUCT(BlueprintType)
struct REALOCEAN_API FRealOceanSurfaceSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface", meta=(ClampMin="1.0", ClampMax="1.1"))
    double AirRefractiveIndex = 1.00029;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface", meta=(ClampMin="1.2", ClampMax="1.5"))
    double WaterRefractiveIndex = 1.333;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface", meta=(ClampMin="0.0", ClampMax="1.0"))
    double Roughness = 0.035;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface", meta=(ClampMin="0.0", ClampMax="4.0"))
    double MicroNormalStrength = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|Breaking", meta=(ClampMin="0.0", ClampMax="4.0"))
    double BreakingStrength = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|Breaking", meta=(ClampMin="0.0", ClampMax="4.0"))
    double WhitecapCoverageScale = 1.0;

    // Oldest reconstructed breaker state used by the Cinematic analytic foam-memory path.
    // Performance/Balanced remain on the instantaneous path.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|Foam Memory", meta=(ClampMin="0.0", ClampMax="8.0", Units="s"))
    double FoamMemorySeconds = 2.4;

    // Exponential half-life of retained foam density. Shorter values produce crisp rapidly dying foam;
    // longer values create broader streaks behind breaking crests.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|Foam Memory", meta=(ClampMin="0.1", ClampMax="8.0", Units="s"))
    double FoamDecayHalfLifeSeconds = 1.8;

    // Fraction of wind speed used as surface foam drift. A value of 0.03 means 8 m/s wind contributes
    // about 0.24 m/s before the current contribution is added.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|Foam Memory", meta=(ClampMin="0.0", ClampMax="0.10"))
    double FoamWindDriftFraction = 0.03;

    // Blend of the configured mean surface current into foam advection. 1 = foam follows the full mean current.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|Foam Memory", meta=(ClampMin="0.0", ClampMax="1.5"))
    double FoamSurfaceCurrentInfluence = 0.35;

    // M28 stateful GPU foam clipmap. It is quality-gated to Cinematic and remains optional;
    // the M27 analytic path is always available as the deterministic fallback.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|GPU Foam")
    bool bEnableGPUFoamClipmap = true;

    // M29 presets are intentionally not restricted to powers of two. Medium uses 384 so projects
    // can trade 43.75% fewer texels than 512 while retaining substantially more detail than 256.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|GPU Foam")
    ERealOceanFoamClipmapQualityPreset FoamClipmapQualityPreset = ERealOceanFoamClipmapQualityPreset::High512;

    // Used only when FoamClipmapQualityPreset=Custom. Runtime aligns to a 64-texel multiple.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|GPU Foam", meta=(ClampMin="128", ClampMax="1024"))
    int32 FoamClipmapResolution = 512;

    // Used only when FoamClipmapQualityPreset=Custom. Presets resolve to 30/45/60 Hz.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|GPU Foam", meta=(ClampMin="10.0", ClampMax="120.0", Units="Hz"))
    double FoamClipmapUpdateRateHz = 60.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|GPU Foam", meta=(ClampMin="64.0", ClampMax="1024.0", Units="m"))
    double FoamClipmapCoverageMeters = 256.0;

    // Stateful density fades to the deterministic analytic foam inside this border. This hides the
    // square clipmap boundary during camera travel without throwing away the M27 fallback.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|GPU Foam", meta=(ClampMin="0.0", ClampMax="256.0", Units="m"))
    double FoamClipmapEdgeFadeMeters = 24.0;

    // Density injected per second at a fully breaking texel. Frame-rate independent in the RDG update.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|GPU Foam", meta=(ClampMin="0.0", ClampMax="8.0"))
    double FoamClipmapInjectionPerSecond = 2.0;

    // Very small diffusion smooths texel-scale discontinuities after advection. Keep low to preserve streaks.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|GPU Foam", meta=(ClampMin="0.0", ClampMax="1.0"))
    double FoamClipmapDiffusionPerSecond = 0.08;

    // Blend of stateful density into the material's analytic whitecap result.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|GPU Foam", meta=(ClampMin="0.0", ClampMax="1.0"))
    double FoamClipmapBlend = 1.0;

    // Procedural micro gravity / wind-chop detail fades before the horizon to avoid sub-pixel shimmer.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|Horizon", meta=(ClampMin="0.0", Units="m"))
    double MicroDetailFadeStartMeters = 180.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|Horizon", meta=(ClampMin="1.0", Units="m"))
    double MicroDetailFadeEndMeters = 1400.0;

    // Restores aggregate highlight energy as explicit micro geometry is filtered at long distance.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface|Horizon", meta=(ClampMin="0.0", ClampMax="0.5"))
    double HorizonRoughnessBoost = 0.065;
};

USTRUCT(BlueprintType)
struct REALOCEAN_API FRealOceanLightingSettings
{
    GENERATED_BODY()

    // Used only when no Directional Light is bound on RealOceanController.
    // This vector points FROM the water TOWARD the sun/source.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Lighting")
    FVector ManualSunDirectionToSourceWS = FVector(0.0, 0.0, 1.0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Lighting")
    FLinearColor ManualSunColor = FLinearColor::White;

    // Passed through as a relative/direct-light scale when a real Directional Light is not bound.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Lighting", meta=(ClampMin="0.0"))
    double ManualSunIntensity = 100000.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Lighting", meta=(ClampMin="0.0", ClampMax="10.0"))
    double SunIntensityScale = 1.0;

    // RealOcean converts the bound Directional Light intensity to a relative 0..N factor for custom scattering.
    // For physically-scaled daylight, 100000 lux is a practical clear-sun reference.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Lighting", meta=(ClampMin="1.0"))
    double ReferenceSunIntensity = 100000.0;

    // M35: elevation-driven daylight response for RealOcean's custom sun/underwater paths.
    // SingleLayerWater/SkyAtmosphere remain the scene-lighting authority; this compensates custom
    // ocean HLSL that otherwise sees the raw Directional Light equally at noon and golden hour.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Lighting|Day Cycle")
    bool bEnableElevationDrivenDayCycle = true;

    // Strength of the extra-atmospheric air-mass attenuation applied to RealOcean custom solar light.
    // Set to 0 when a project already supplies fully atmosphere-attenuated sun color/intensity to RealOcean.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Lighting|Day Cycle", meta=(ClampMin="0.0", ClampMax="2.0"))
    double DayCycleAtmosphereStrength = 0.65;

    // Approximate RGB optical depth used only for the day-cycle compensation. Blue is intentionally
    // attenuated more strongly at long grazing paths, producing a warm dawn/dusk solar source.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Lighting|Day Cycle", meta=(ClampMin="0.0"))
    FVector DayCycleAtmosphericOpticalDepthRGB = FVector(0.04, 0.08, 0.16);

    // Civil-twilight lower boundary and the elevation where underwater ambient/inscatter reaches
    // its configured daytime strength. Direct sun remains physically disabled below the horizon.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Lighting|Day Cycle", meta=(ClampMin="-18.0", ClampMax="0.0", Units="deg"))
    double TwilightStartElevationDegrees = -6.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Lighting|Day Cycle", meta=(ClampMin="1.0", ClampMax="30.0", Units="deg"))
    double FullDaylightElevationDegrees = 12.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Lighting|Day Cycle", meta=(ClampMin="5.0", ClampMax="35.0", Units="deg"))
    double GoldenHourEndElevationDegrees = 18.0;

    // Ambient underwater scattering must not glow with full daytime energy at night.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Lighting|Day Cycle", meta=(ClampMin="0.0", ClampMax="1.0"))
    double NightInscatterScale = 0.045;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Lighting|Day Cycle", meta=(ClampMin="0.0", ClampMax="1.0"))
    double TwilightInscatterScale = 0.28;

    // HG anisotropy. Positive values favor forward scattering.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Lighting", meta=(ClampMin="-0.95", ClampMax="0.95"))
    double ForwardScatteringG = 0.82;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Lighting", meta=(ClampMin="100.0", ClampMax="3000.0", Units="m"))
    double AphoticDepthMeters = 1000.0;
};

USTRUCT(BlueprintType)
struct REALOCEAN_API FRealOceanCausticsSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Caustics")
    bool bEnabled = true;

    // Hard geometric envelope; spectral sun transmission usually removes caustics earlier.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Caustics", meta=(ClampMin="1.0", ClampMax="300.0", Units="m"))
    double MaxDepthMeters = 120.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Caustics", meta=(ClampMin="0.0", ClampMax="8.0"))
    double Intensity = 1.0;

    // Controls how strongly Jacobian compression/expansion is shown around neutral focus=1.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Caustics", meta=(ClampMin="0.0", ClampMax="4.0"))
    double Contrast = 1.35;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Caustics", meta=(ClampMin="1.0", ClampMax="32.0"))
    double FocusClamp = 8.0;

    // Finite-difference spacing used by the wave-derived ray Jacobian.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Caustics", meta=(ClampMin="0.05", ClampMax="5.0", Units="m"))
    double DerivativeStepMeters = 0.45;

    // Screen-space footprint expands the finite-difference kernel when a caustic is sub-pixel.
    // This is intentionally spatial rather than history-based so TSR/TAA can accumulate a stable signal.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Caustics", meta=(ClampMin="0.0", ClampMax="4.0"))
    double ScreenSpaceStability = 1.25;
};


USTRUCT(BlueprintType)
struct REALOCEAN_API FRealOceanCurrentSettings
{
    GENERATED_BODY()

    // Horizontal current direction the water travels toward, degrees counter-clockwise from +X.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Current", meta=(ClampMin="-360.0", ClampMax="360.0", Units="deg"))
    double SurfaceDirectionDegrees = 40.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Current", meta=(ClampMin="0.0", ClampMax="5.0", Units="m/s"))
    double SurfaceSpeedMetersPerSecond = 0.35;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Current", meta=(ClampMin="-360.0", ClampMax="360.0", Units="deg"))
    double DeepDirectionDegrees = 15.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Current", meta=(ClampMin="0.0", ClampMax="5.0", Units="m/s"))
    double DeepSpeedMetersPerSecond = 0.10;

    // Center of the smooth surface-to-deep current transition.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Current", meta=(ClampMin="1.0", ClampMax="3000.0", Units="m"))
    double TransitionDepthMeters = 600.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Current", meta=(ClampMin="10.0", ClampMax="2000.0", Units="m"))
    double TransitionWidthMeters = 350.0;

    // Low-frequency deterministic 3D variation added to the mean current. This is environmental data only;
    // RealOcean does not apply forces to actors.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Current", meta=(ClampMin="0.0", ClampMax="2.0", Units="m/s"))
    double TurbulenceSpeedMetersPerSecond = 0.08;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Current", meta=(ClampMin="10.0", ClampMax="5000.0", Units="m"))
    double TurbulenceScaleMeters = 280.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Current", meta=(ClampMin="0.0", ClampMax="10.0"))
    double TurbulenceTimeScale = 1.0;
};

USTRUCT(BlueprintType)
struct REALOCEAN_API FRealOceanHydrographySettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Hydrography", meta=(Units="Celsius"))
    double SurfaceTemperatureC = 18.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Hydrography", meta=(Units="Celsius"))
    double DeepTemperatureC = 2.5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Hydrography", meta=(ClampMin="1.0", ClampMax="2500.0", Units="m"))
    double ThermoclineCenterMeters = 450.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Hydrography", meta=(ClampMin="10.0", ClampMax="1500.0", Units="m"))
    double ThermoclineWidthMeters = 260.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Hydrography", meta=(ClampMin="0.0", ClampMax="50.0"))
    double SurfaceSalinityPSU = 35.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Hydrography", meta=(ClampMin="0.0", ClampMax="50.0"))
    double DeepSalinityPSU = 34.7;

    // Reference point for the lightweight density estimate returned by environment queries.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Hydrography", meta=(Units="Celsius"))
    double ReferenceTemperatureC = 10.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment|Hydrography")
    double ReferenceSalinityPSU = 35.0;
};

USTRUCT(BlueprintType)
struct REALOCEAN_API FRealOceanSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Scale")
    bool bUseWaterSubsystemSeaLevel = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Scale", meta=(Units="cm"))
    double ManualSeaLevelCm = 0.0;

    // Project contract: this ocean is exactly 3000 m deep. Runtime depth/pressure state clamps here.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Scale", meta=(ClampMin="3000.0", ClampMax="3000.0", Units="m"))
    double MaximumDepthMeters = 3000.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Quality")
    ERealOceanQualityMode QualityMode = ERealOceanQualityMode::Cinematic;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Debug")
    ERealOceanDebugView DebugView = ERealOceanDebugView::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Debug", meta=(ClampMin="0.0", ClampMax="1.0"))
    double DebugOpacity = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Physics", meta=(ClampMin="900.0", ClampMax="1100.0", Units="kg/m^3"))
    double SeawaterDensityKgM3 = 1025.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Physics", meta=(ClampMin="0.0", Units="m/s^2"))
    double GravityMS2 = 9.80665;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Physics", meta=(ClampMin="0.0", Units="bar"))
    double SurfacePressureBar = 1.01325;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Waves")
    FRealOceanWaveSettings Waves;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Surface")
    FRealOceanSurfaceSettings Surface;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Lighting")
    FRealOceanLightingSettings Lighting;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Caustics")
    FRealOceanCausticsSettings Caustics;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment")
    FRealOceanCurrentSettings Current;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Environment")
    FRealOceanHydrographySettings Hydrography;

    // Spectral starter coefficients in inverse meters, RGB order. These remain calibratable because
    // inherent optical properties vary with particulates, dissolved matter, plankton and wavelength.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Optics", meta=(ClampMin="0.0"))
    FVector AbsorptionRGBPerMeter = FVector(0.15, 0.03, 0.012);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Optics", meta=(ClampMin="0.0"))
    FVector ScatteringRGBPerMeter = FVector(0.020, 0.012, 0.008);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Optics")
    FLinearColor InscatterColor = FLinearColor(0.018f, 0.115f, 0.165f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Optics", meta=(ClampMin="0.0", ClampMax="8.0"))
    double InscatterIntensity = 1.0;

    // Dimensionless particulate optical control. 0.10 preserves the authored base coefficients;
    // lower values are clearer and higher values add bounded spectral absorption/scattering.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Optics", meta=(ClampMin="0.0", ClampMax="5.0"))
    double Turbidity = 0.10;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Optics", meta=(ClampMin="0.01", ClampMax="5.0", Units="m"))
    double UnderwaterTransitionMeters = 0.35;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Optics", meta=(ClampMin="0.0001", ClampMax="0.5"))
    double VisibilityTransmissionThreshold = 0.02;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Optics", meta=(ClampMin="10.0", ClampMax="3000.0", Units="m"))
    double MaxOpticalPathMeters = 750.0;
};


USTRUCT(BlueprintType)
struct REALOCEAN_API FRealOceanEnvironmentSample
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment")
    FVector WorldPositionCm = FVector::ZeroVector;

    // Signed depth below mean sea level; negative values are above the mean plane.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment", meta=(Units="m"))
    double MeanSeaDepthMeters = 0.0;

    // Signed depth below the instantaneous dynamic wave surface; negative values are above water.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment", meta=(Units="m"))
    double DynamicSurfaceDepthMeters = 0.0;

    // Deprecated compatibility alias. As of 0.33.4 this consistently means DynamicSurfaceDepthMeters.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment", meta=(Units="m", DeprecatedProperty, DeprecationMessage="Use DynamicSurfaceDepthMeters or MeanSeaDepthMeters explicitly."))
    double RawDepthMeters = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment", meta=(Units="m"))
    double DepthMeters = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment")
    bool bUnderwater = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment")
    bool bBelowConfiguredBottom = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment")
    ERealOceanDepthZone DepthZone = ERealOceanDepthZone::AboveSurface;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment", meta=(Units="bar"))
    double AbsolutePressureBar = 1.01325;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment", meta=(Units="Celsius"))
    double TemperatureC = 18.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment")
    double SalinityPSU = 35.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment", meta=(Units="kg/m^3"))
    double EstimatedDensityKgM3 = 1025.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment", meta=(Units="m/s"))
    FVector CurrentVelocityMetersPerSecond = FVector::ZeroVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment", meta=(Units="m"))
    double SurfaceWorldHeightMeters = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment")
    FVector SurfaceNormal = FVector::UpVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment", meta=(Units="m"))
    double DistanceToSurfaceMeters = 0.0;
};

USTRUCT(BlueprintType)
struct REALOCEAN_API FRealOceanState
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean")
    double SeaLevelCm = 0.0;

    // Physical water-column depth, clamped to the configured 3000 m project bottom.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean")
    double DepthMeters = 0.0;

    // Signed depth below mean sea level. This is the geological/reference depth used for bottom tests.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean", meta=(Units="m"))
    double MeanSeaDepthMeters = 0.0;

    // Unclamped signed distance below the instantaneous wave surface. Negative values are above water.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean", meta=(Units="m"))
    double DynamicSurfaceDepthMeters = 0.0;

    // Deprecated compatibility alias. As of 0.33.4 this consistently means DynamicSurfaceDepthMeters.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean", meta=(Units="m", DeprecatedProperty, DeprecationMessage="Use DynamicSurfaceDepthMeters or MeanSeaDepthMeters explicitly."))
    double RawDepthMeters = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean")
    bool bBelowConfiguredBottom = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean")
    double NormalizedDepth = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean")
    double GaugePressureBar = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean")
    double AbsolutePressureBar = 1.01325;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment", meta=(Units="Celsius"))
    double WaterTemperatureC = 18.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment")
    double SalinityPSU = 35.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment", meta=(Units="kg/m^3"))
    double EstimatedDensityKgM3 = 1025.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Environment", meta=(Units="m/s"))
    FVector CurrentVelocityMetersPerSecond = FVector::ZeroVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Optics")
    double UnderwaterBlend = 0.0;

    // Luminance summary of SunTransmissionRGB; 0 below horizon.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Lighting")
    double SunlightTransmission = 1.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Lighting")
    FVector SunTransmissionRGB = FVector(1.0, 1.0, 1.0);

    // Points from the water toward the sun/source. The light travels in the opposite direction.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Lighting")
    FVector SunDirectionToSourceWS = FVector(0.0, 0.0, 1.0);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Lighting")
    FLinearColor SunColor = FLinearColor::White;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Lighting")
    double SunIntensity = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Lighting")
    double SunRelativeIntensity = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Lighting")
    double SunCosZenithAir = 1.0;

    // Signed astronomical elevation from the local horizon. Unlike SunCosZenithAir this preserves
    // negative twilight/night values so gameplay and material response can follow a full day cycle.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Lighting|Day Cycle", meta=(Units="deg"))
    double SunElevationDegrees = 90.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Lighting|Day Cycle")
    ERealOceanDayPhase DayPhase = ERealOceanDayPhase::Midday;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Lighting|Day Cycle")
    bool bSunRising = true;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Lighting|Day Cycle")
    double DaylightFactor = 1.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Lighting|Day Cycle")
    double GoldenHourFactor = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Lighting|Day Cycle")
    FVector DayCycleAtmosphereTransmissionRGB = FVector(1.0, 1.0, 1.0);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Lighting|Day Cycle")
    double EffectiveInscatterIntensity = 1.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Lighting")
    double SunCosZenithWater = 1.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Lighting", meta=(Units="m"))
    double DirectSunPathMeters = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Optics", meta=(Units="m"))
    double ReferenceVisibilityMeters = 0.0;

    // Turbidity-adjusted coefficients actually used by CPU diagnostics and render paths.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Optics")
    FVector EffectiveAbsorptionRGBPerMeter = FVector::ZeroVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Optics")
    FVector EffectiveScatteringRGBPerMeter = FVector::ZeroVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Optics")
    FVector ExtinctionRGBPerMeter = FVector::ZeroVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Caustics")
    double CausticsStrength = 0.0;

    // Effective production budget resolved from QualityMode. These are pushed to the MPC so shader
    // cost/behavior is explicit rather than duplicated as magic thresholds in Custom HLSL.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Caustics", meta=(Units="m"))
    double EffectiveCausticsMaxDepthMeters = 120.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Caustics")
    int32 CausticsSolveIterations = 3;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Caustics")
    double CausticsDerivativeScale = 0.75;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Waves")
    double OceanTimeSeconds = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Waves")
    FVector2D WaveTravelDirectionXY = FVector2D(1.0, 0.0);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Waves")
    FVector2D WindDirectionXY = FVector2D(1.0, 0.0);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Waves", meta=(Units="m"))
    double SwellSignificantWaveHeightMeters = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Waves", meta=(Units="m"))
    double WindSeaSignificantWaveHeightMeters = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Waves", meta=(Units="s"))
    double WindSeaPeakPeriodSeconds = 2.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Surface", meta=(Units="deg"))
    double UnderwaterCriticalAngleDegrees = 48.6;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Quality")
    ERealOceanQualityMode QualityMode = ERealOceanQualityMode::Cinematic;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Debug")
    ERealOceanDebugView DebugView = ERealOceanDebugView::None;

    // Bounded spatiotemporal wave phases at the render camera. The shader combines these with camera-relative XY,
    // avoiding float phase loss when the world is many kilometers from the origin.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Waves")
    FLinearColor WaveAnchorPhase0123 = FLinearColor::Black;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Waves")
    FLinearColor WaveAnchorPhase4567 = FLinearColor::Black;

    // Bounded phases for four procedural shading-only micro gravity / wind-chop bands.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Waves")
    FLinearColor MicroWaveAnchorPhase0123 = FLinearColor::Black;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Waves")
    FVector RenderCameraWorldPositionCm = FVector::ZeroVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean")
    ERealOceanDepthZone DepthZone = ERealOceanDepthZone::AboveSurface;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean")
    FVector CameraWorldPositionCm = FVector::ZeroVector;

    // UE Water plugin integration diagnostics. These do not change the RealOcean optical model;
    // they make missing WaterBodyOcean/Water rendering configuration visible at runtime.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Integration")
    bool bWaterSubsystemAvailable = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Integration")
    bool bWaterRenderingEnabled = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Integration")
    bool bOceanBodyAvailable = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Integration")
    double WaterSubsystemOceanBaseHeightCm = 0.0;
};
