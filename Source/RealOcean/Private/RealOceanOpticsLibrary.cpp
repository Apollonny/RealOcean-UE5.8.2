#include "RealOceanOpticsLibrary.h"

namespace RealOceanOptics
{
    static FVector MaxZero(const FVector& V)
    {
        return FVector(FMath::Max(0.0, V.X), FMath::Max(0.0, V.Y), FMath::Max(0.0, V.Z));
    }

    static FVector ExpNeg(const FVector& V)
    {
        return FVector(FMath::Exp(-V.X), FMath::Exp(-V.Y), FMath::Exp(-V.Z));
    }
}

void URealOceanOpticsLibrary::CalculateTurbidityAdjustedCoefficients(
    const FVector& BaseAbsorptionPerMeter,
    const FVector& BaseScatteringPerMeter,
    double Turbidity,
    FVector& OutEffectiveAbsorptionPerMeter,
    FVector& OutEffectiveScatteringPerMeter)
{
    constexpr double ReferenceTurbidity = 0.10;
    const double T = FMath::Max(0.0, Turbidity);
    const double ClearRatio = FMath::Clamp(T / ReferenceTurbidity, 0.0, 1.0);
    const double ClearAbsorptionScale = FMath::Lerp(0.92, 1.0, ClearRatio);
    const double ClearScatteringScale = FMath::Lerp(0.35, 1.0, ClearRatio);
    const double Particulate = FMath::Loge(1.0 + 1.5 * FMath::Max(T - ReferenceTurbidity, 0.0)) * 1.4426950408889634;

    const FVector BaseA = RealOceanOptics::MaxZero(BaseAbsorptionPerMeter);
    const FVector BaseS = RealOceanOptics::MaxZero(BaseScatteringPerMeter);
    OutEffectiveAbsorptionPerMeter = BaseA * ClearAbsorptionScale
        + FVector(0.012, 0.009, 0.006) * Particulate;
    OutEffectiveScatteringPerMeter = BaseS * ClearScatteringScale
        + FVector(0.050, 0.060, 0.070) * Particulate;
}

FVector URealOceanOpticsLibrary::BeerLambertTransmission(const FVector& CoefficientPerMeter, double DistanceMeters)
{
    const FVector C = RealOceanOptics::MaxZero(CoefficientPerMeter);
    const double D = FMath::Max(0.0, DistanceMeters);
    return RealOceanOptics::ExpNeg(C * D);
}

FVector URealOceanOpticsLibrary::ExtinctionTransmission(const FVector& AbsorptionPerMeter, const FVector& ScatteringPerMeter, double DistanceMeters)
{
    return BeerLambertTransmission(RealOceanOptics::MaxZero(AbsorptionPerMeter) + RealOceanOptics::MaxZero(ScatteringPerMeter), DistanceMeters);
}

double URealOceanOpticsLibrary::CalculateReferenceVisibilityMeters(const FVector& AbsorptionPerMeter, const FVector& ScatteringPerMeter, double TransmissionThreshold)
{
    const FVector Extinction = RealOceanOptics::MaxZero(AbsorptionPerMeter) + RealOceanOptics::MaxZero(ScatteringPerMeter);

    // Linear Rec.709 luminance weighting. This is a useful scalar diagnostic only; the renderer remains spectral RGB.
    const double LuminanceExtinction =
        Extinction.X * 0.2126 +
        Extinction.Y * 0.7152 +
        Extinction.Z * 0.0722;

    if (LuminanceExtinction <= UE_DOUBLE_SMALL_NUMBER)
    {
        return TNumericLimits<double>::Max();
    }

    const double Threshold = FMath::Clamp(TransmissionThreshold, 0.000001, 0.999999);
    return -FMath::Loge(Threshold) / LuminanceExtinction;
}

FVector URealOceanOpticsLibrary::ApplyHomogeneousWaterApproximation(
    const FVector& SceneLinearRGB,
    const FVector& InscatterLinearRGB,
    const FVector& AbsorptionPerMeter,
    const FVector& ScatteringPerMeter,
    double DistanceMeters,
    double InscatterIntensity)
{
    const FVector A = RealOceanOptics::MaxZero(AbsorptionPerMeter);
    const FVector S = RealOceanOptics::MaxZero(ScatteringPerMeter);
    const FVector Extinction = A + S;
    const FVector T = ExtinctionTransmission(A, S, DistanceMeters);

    // Single-scattering albedo per channel: scattering / total extinction.
    FVector Albedo = FVector::ZeroVector;
    Albedo.X = Extinction.X > UE_DOUBLE_SMALL_NUMBER ? S.X / Extinction.X : 0.0;
    Albedo.Y = Extinction.Y > UE_DOUBLE_SMALL_NUMBER ? S.Y / Extinction.Y : 0.0;
    Albedo.Z = Extinction.Z > UE_DOUBLE_SMALL_NUMBER ? S.Z / Extinction.Z : 0.0;

    const FVector Source = InscatterLinearRGB * Albedo * FMath::Max(0.0, InscatterIntensity);
    const FVector OneMinusT(1.0 - T.X, 1.0 - T.Y, 1.0 - T.Z);

    return FVector(
        SceneLinearRGB.X * T.X + Source.X * OneMinusT.X,
        SceneLinearRGB.Y * T.Y + Source.Y * OneMinusT.Y,
        SceneLinearRGB.Z * T.Z + Source.Z * OneMinusT.Z);
}
