#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RealOceanOpticsLibrary.generated.h"

UCLASS()
class REALOCEAN_API URealOceanOpticsLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Mirrors RO_ApplyTurbidityToOpticalCoefficientsM35. Turbidity=0.10 is neutral.
    static void CalculateTurbidityAdjustedCoefficients(
        const FVector& BaseAbsorptionPerMeter,
        const FVector& BaseScatteringPerMeter,
        double Turbidity,
        FVector& OutEffectiveAbsorptionPerMeter,
        FVector& OutEffectiveScatteringPerMeter);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Optics")
    static FVector BeerLambertTransmission(const FVector& CoefficientPerMeter, double DistanceMeters);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Optics")
    static FVector ExtinctionTransmission(const FVector& AbsorptionPerMeter, const FVector& ScatteringPerMeter, double DistanceMeters);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Optics")
    static double CalculateReferenceVisibilityMeters(const FVector& AbsorptionPerMeter, const FVector& ScatteringPerMeter, double TransmissionThreshold = 0.02);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Optics")
    static FVector ApplyHomogeneousWaterApproximation(
        const FVector& SceneLinearRGB,
        const FVector& InscatterLinearRGB,
        const FVector& AbsorptionPerMeter,
        const FVector& ScatteringPerMeter,
        double DistanceMeters,
        double InscatterIntensity = 1.0);
};
