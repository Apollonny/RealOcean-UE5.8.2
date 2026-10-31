#include "RealOceanLightingLibrary.h"

namespace
{
    constexpr double Pi = 3.14159265358979323846;
}

double URealOceanLightingLibrary::CalculateRefractedSunCosineWater(double SunCosZenithAir, double AirIOR, double WaterIOR)
{
    const double MuAir = FMath::Clamp(SunCosZenithAir, 0.0, 1.0);
    if (MuAir <= 0.0 || AirIOR <= 0.0 || WaterIOR <= 0.0)
    {
        return 0.0;
    }

    const double SinAirSquared = FMath::Max(0.0, 1.0 - MuAir * MuAir);
    const double Eta = AirIOR / WaterIOR;
    const double SinWaterSquared = FMath::Clamp(Eta * Eta * SinAirSquared, 0.0, 1.0);
    return FMath::Sqrt(FMath::Max(0.0, 1.0 - SinWaterSquared));
}

double URealOceanLightingLibrary::CalculateRelativeOpticalAirMass(double SunElevationDegrees)
{
    // Kasten-Young 1989 relative optical air-mass approximation. It stays finite near the horizon
    // and is only used for the RealOcean custom-light compensation path, not for UE SkyAtmosphere.
    if (SunElevationDegrees <= -5.0)
    {
        return 40.0;
    }

    const double ClampedElevation = FMath::Clamp(SunElevationDegrees, -4.999, 90.0);
    const double ZenithDegrees = 90.0 - ClampedElevation;
    const double CosZenith = FMath::Cos(FMath::DegreesToRadians(ZenithDegrees));
    const double CorrectionBase = FMath::Max(0.001, 96.07995 - ZenithDegrees);
    const double Denominator = CosZenith + 0.50572 * FMath::Pow(CorrectionBase, -1.6364);
    return FMath::Clamp(1.0 / FMath::Max(Denominator, 0.025), 1.0, 40.0);
}

FVector URealOceanLightingLibrary::CalculateDayCycleAtmosphereTransmissionRGB(
    double SunElevationDegrees,
    const FVector& OpticalDepthRGB,
    double Strength)
{
    if (SunElevationDegrees <= 0.0 || Strength <= 0.0)
    {
        return SunElevationDegrees > 0.0 ? FVector::OneVector : FVector::ZeroVector;
    }

    const double AirMass = CalculateRelativeOpticalAirMass(SunElevationDegrees);
    const double ExcessAirMass = FMath::Max(0.0, AirMass - 1.0);
    const double S = FMath::Max(0.0, Strength);
    return FVector(
        FMath::Exp(-FMath::Max(0.0, OpticalDepthRGB.X) * ExcessAirMass * S),
        FMath::Exp(-FMath::Max(0.0, OpticalDepthRGB.Y) * ExcessAirMass * S),
        FMath::Exp(-FMath::Max(0.0, OpticalDepthRGB.Z) * ExcessAirMass * S));
}

double URealOceanLightingLibrary::CalculateSunInterfaceTransmissionAirToWater(double SunCosZenithAir, double AirIOR, double WaterIOR)
{
    const double Ci = FMath::Clamp(SunCosZenithAir, 0.0, 1.0);
    if (Ci <= 0.0 || AirIOR <= 0.0 || WaterIOR <= 0.0)
    {
        return 0.0;
    }

    const double N1 = FMath::Max(AirIOR, UE_DOUBLE_SMALL_NUMBER);
    const double N2 = FMath::Max(WaterIOR, UE_DOUBLE_SMALL_NUMBER);
    const double Eta = N1 / N2;
    const double SinT2 = Eta * Eta * FMath::Max(0.0, 1.0 - Ci * Ci);
    if (SinT2 >= 1.0)
    {
        return 0.0;
    }

    const double Ct = FMath::Sqrt(FMath::Max(0.0, 1.0 - SinT2));
    const double RsDen = FMath::Max(N1 * Ci + N2 * Ct, UE_DOUBLE_SMALL_NUMBER);
    const double RpDen = FMath::Max(N2 * Ci + N1 * Ct, UE_DOUBLE_SMALL_NUMBER);
    const double Rs = (N1 * Ci - N2 * Ct) / RsDen;
    const double Rp = (N2 * Ci - N1 * Ct) / RpDen;
    const double Reflectance = FMath::Clamp(0.5 * (Rs * Rs + Rp * Rp), 0.0, 1.0);
    return 1.0 - Reflectance;
}

double URealOceanLightingLibrary::CalculateDirectSunPathMeters(double DepthMeters, double SunCosZenithAir, double AirIOR, double WaterIOR)
{
    if (DepthMeters <= 0.0)
    {
        return 0.0;
    }

    const double MuWater = CalculateRefractedSunCosineWater(SunCosZenithAir, AirIOR, WaterIOR);
    if (MuWater <= KINDA_SMALL_NUMBER)
    {
        return TNumericLimits<double>::Max();
    }

    return FMath::Max(0.0, DepthMeters) / MuWater;
}

FVector URealOceanLightingLibrary::CalculateSpectralSunTransmission(
    const FVector& AbsorptionRGBPerMeter,
    const FVector& ScatteringRGBPerMeter,
    double PathMeters)
{
    if (!FMath::IsFinite(PathMeters))
    {
        return FVector::ZeroVector;
    }

    const double D = FMath::Max(0.0, PathMeters);
    const FVector Extinction(
        FMath::Max(0.0, AbsorptionRGBPerMeter.X + ScatteringRGBPerMeter.X),
        FMath::Max(0.0, AbsorptionRGBPerMeter.Y + ScatteringRGBPerMeter.Y),
        FMath::Max(0.0, AbsorptionRGBPerMeter.Z + ScatteringRGBPerMeter.Z));

    return FVector(
        FMath::Exp(-Extinction.X * D),
        FMath::Exp(-Extinction.Y * D),
        FMath::Exp(-Extinction.Z * D));
}

double URealOceanLightingLibrary::CalculateLinearLuminance(const FVector& LinearRGB)
{
    return FMath::Max(0.0,
        0.2126 * LinearRGB.X +
        0.7152 * LinearRGB.Y +
        0.0722 * LinearRGB.Z);
}

double URealOceanLightingLibrary::HenyeyGreenstein(double CosTheta, double G)
{
    const double ClampedG = FMath::Clamp(G, -0.98, 0.98);
    const double GG = ClampedG * ClampedG;
    const double C = FMath::Clamp(CosTheta, -1.0, 1.0);
    const double Denom = FMath::Max(1.0 + GG - 2.0 * ClampedG * C, 1.0e-8);
    return (1.0 - GG) / (4.0 * Pi * FMath::Pow(Denom, 1.5));
}
