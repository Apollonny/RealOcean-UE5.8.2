#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RealOceanTypes.h"
#include "RealOceanWaveLibrary.generated.h"

USTRUCT(BlueprintType)
struct REALOCEAN_API FRealOceanWaveSample
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Real Ocean|Waves", meta=(Units="m"))
    double HeightMeters = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Real Ocean|Waves")
    FVector Normal = FVector::UpVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Real Ocean|Waves", meta=(Units="m"))
    FVector DisplacementMeters = FVector::ZeroVector;

    // Horizontal Gerstner mapping determinant. Values below 1 indicate local compression;
    // very small values are useful as an instantaneous breaking/whitecap cue.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Real Ocean|Waves")
    double HorizontalJacobian = 1.0;
};

USTRUCT(BlueprintType)
struct REALOCEAN_API FRealOceanSpectrumPartition
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Real Ocean|Waves", meta=(Units="m"))
    double SwellSignificantWaveHeightMeters = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Real Ocean|Waves", meta=(Units="m"))
    double WindSeaSignificantWaveHeightMeters = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Real Ocean|Waves", meta=(Units="s"))
    double WindSeaPeakPeriodSeconds = 2.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Real Ocean|Waves")
    double WindSeaEnergyFraction = 0.0;
};

UCLASS()
class REALOCEAN_API URealOceanWaveLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category = "Real Ocean|Waves")
    static FRealOceanWaveSample EvaluateWaveField(
        FVector2D WorldXYMeters,
        double TimeSeconds,
        const FRealOceanWaveSettings& Settings,
        double GravityMS2 = 9.80665);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Waves")
    static FRealOceanWaveSample EvaluateSurfaceAtWorldXY(
        FVector2D WorldXYMeters,
        double TimeSeconds,
        const FRealOceanWaveSettings& Settings,
        double GravityMS2 = 9.80665,
        int32 SolveIterations = 4);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Waves")
    static double DeepWaterWavelengthMeters(double PeriodSeconds, double GravityMS2 = 9.80665);

    // M26: derive the local wind-sea peak from U10 + effective fetch, then partition the requested
    // combined significant wave height by spectral energy (Hs^2). Swell retains the residual energy.
    UFUNCTION(BlueprintPure, Category = "Real Ocean|Waves")
    static FRealOceanSpectrumPartition CalculateSpectrumPartition(
        const FRealOceanWaveSettings& Settings,
        double GravityMS2 = 9.80665);

    static void CalculateSpatiotemporalPhaseAnchors(
        FVector2D AnchorWorldXYMeters,
        const FRealOceanWaveSettings& Settings,
        double GravityMS2,
        double TimeSeconds,
        FLinearColor& OutPhase0123,
        FLinearColor& OutPhase4567);

    static void CalculateMicroSpatiotemporalPhaseAnchors(
        FVector2D AnchorWorldXYMeters,
        const FRealOceanWaveSettings& Settings,
        double TimeSeconds,
        FLinearColor& OutPhase0123);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Surface")
    static double CalculateCriticalAngleDegrees(double IncidentIOR = 1.333, double TransmittedIOR = 1.00029);
};
