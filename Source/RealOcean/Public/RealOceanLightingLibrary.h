#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RealOceanLightingLibrary.generated.h"

UCLASS()
class REALOCEAN_API URealOceanLightingLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category = "Real Ocean|Lighting")
    static double CalculateRefractedSunCosineWater(double SunCosZenithAir, double AirIOR = 1.00029, double WaterIOR = 1.333);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Lighting")
    static double CalculateSunInterfaceTransmissionAirToWater(double SunCosZenithAir, double AirIOR = 1.00029, double WaterIOR = 1.333);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Lighting|Day Cycle")
    static double CalculateRelativeOpticalAirMass(double SunElevationDegrees);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Lighting|Day Cycle")
    static FVector CalculateDayCycleAtmosphereTransmissionRGB(
        double SunElevationDegrees,
        const FVector& OpticalDepthRGB = FVector(0.04, 0.08, 0.16),
        double Strength = 0.65);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Lighting")
    static double CalculateDirectSunPathMeters(double DepthMeters, double SunCosZenithAir, double AirIOR = 1.00029, double WaterIOR = 1.333);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Lighting")
    static FVector CalculateSpectralSunTransmission(
        const FVector& AbsorptionRGBPerMeter,
        const FVector& ScatteringRGBPerMeter,
        double PathMeters);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Lighting")
    static double CalculateLinearLuminance(const FVector& LinearRGB);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Lighting")
    static double HenyeyGreenstein(double CosTheta, double G);
};
