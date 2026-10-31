#include "RealOceanWaveLibrary.h"

namespace RealOceanWaves
{
    constexpr int32 NumBands = 8;
    constexpr int32 NumSwellBands = 3;
    constexpr int32 NumWindBands = 5;
    constexpr double TwoPi = 6.28318530717958647692;

    // M26 dual spectrum. 0..2 = swell, 3..7 = local wind sea.
    constexpr double SwellLengthScale[NumSwellBands] = {1.000, 0.660, 0.430};
    constexpr double SwellAmplitudeWeight[NumSwellBands] = {0.500, 0.310, 0.190};
    // Hs = 4*sigma and sigma^2 = 0.5*sum(a_i^2) for independent sinusoidal bands.
    // These scales normalize the fixed band weights so each partition reconstructs its requested Hs.
    constexpr double SwellHsAmplitudeScale = 0.5718862635895968;
    constexpr double SwellDirectionOffsetDeg[NumSwellBands] = {0.0, 6.0, -9.0};
    constexpr double SwellPhaseOffset[NumSwellBands] = {0.37, 1.14, 2.21};

    constexpr double WindLengthScale[NumWindBands] = {1.000, 0.680, 0.460, 0.300, 0.190};
    constexpr double WindAmplitudeWeight[NumWindBands] = {0.340, 0.250, 0.180, 0.140, 0.090};
    constexpr double WindSeaHsAmplitudeScale = 0.7244094847496808;
    constexpr double WindDirectionOffsetDeg[NumWindBands] = {-12.0, 8.0, -24.0, 31.0, -43.0};
    constexpr double WindPhaseOffset[NumWindBands] = {0.79, 2.87, 1.73, 0.19, 2.48};

    constexpr int32 NumMicroBands = 4;
    constexpr double MicroWavelengthMeters[NumMicroBands] = {1.20, 0.55, 0.24, 0.11};
    constexpr double MicroDirectionOffsetDeg[NumMicroBands] = {7.0, -23.0, 41.0, -57.0};
    constexpr double MicroTimeScale[NumMicroBands] = {1.0, 1.35, 1.8, 2.35};

    static FVector2D DirectionFromDegrees(double Degrees)
    {
        const double Radians = FMath::DegreesToRadians(Degrees);
        return FVector2D(FMath::Cos(Radians), FMath::Sin(Radians));
    }

    struct FBand
    {
        double DirectionDegrees = 0.0;
        double WavelengthMeters = 1.0;
        double AmplitudeMeters = 0.0;
        double HorizontalScale = 0.0;
        double PhaseOffset = 0.0;
    };

    static FBand GetBand(
        int32 Index,
        const FRealOceanWaveSettings& Settings,
        const FRealOceanSpectrumPartition& Partition,
        double GravityMS2)
    {
        FBand B;
        const double G = FMath::Max(GravityMS2, 0.01);
        const double Q = FMath::Clamp(Settings.Steepness, 0.0, 0.95);
        if (Index < NumSwellBands)
        {
            const double Base = URealOceanWaveLibrary::DeepWaterWavelengthMeters(Settings.DominantSwellPeriodSeconds, G);
            B.WavelengthMeters = FMath::Max(0.05, Base * SwellLengthScale[Index]);
            B.AmplitudeMeters = FMath::Max(0.0, Partition.SwellSignificantWaveHeightMeters) * SwellHsAmplitudeScale * SwellAmplitudeWeight[Index];
            B.DirectionDegrees = Settings.TravelDirectionDegrees + SwellDirectionOffsetDeg[Index] * FMath::Clamp(Settings.DirectionalSpread, 0.0, 1.0);
            B.HorizontalScale = Q;
            B.PhaseOffset = SwellPhaseOffset[Index];
        }
        else
        {
            const int32 W = Index - NumSwellBands;
            const double Base = URealOceanWaveLibrary::DeepWaterWavelengthMeters(Partition.WindSeaPeakPeriodSeconds, G);
            B.WavelengthMeters = FMath::Max(0.05, Base * WindLengthScale[W]);
            B.AmplitudeMeters = FMath::Max(0.0, Partition.WindSeaSignificantWaveHeightMeters) * WindSeaHsAmplitudeScale * WindAmplitudeWeight[W];
            B.DirectionDegrees = Settings.WindDirectionDegrees + WindDirectionOffsetDeg[W] * FMath::Clamp(Settings.WindSeaDirectionalSpread, 0.0, 1.0);
            B.HorizontalScale = FMath::Clamp(Q * Settings.WindSeaSteepnessScale, 0.0, 0.95);
            B.PhaseOffset = WindPhaseOffset[W];
        }
        return B;
    }
}

double URealOceanWaveLibrary::DeepWaterWavelengthMeters(double PeriodSeconds, double GravityMS2)
{
    const double T = FMath::Max(0.01, PeriodSeconds);
    const double G = FMath::Max(0.01, GravityMS2);
    return G * T * T / RealOceanWaves::TwoPi;
}

FRealOceanSpectrumPartition URealOceanWaveLibrary::CalculateSpectrumPartition(
    const FRealOceanWaveSettings& Settings,
    double GravityMS2)
{
    using namespace RealOceanWaves;
    FRealOceanSpectrumPartition Result;
    const double G = FMath::Max(GravityMS2, 0.01);
    const double U10 = FMath::Max(Settings.WindSpeedMetersPerSecond, 0.0);
    const double FetchMeters = FMath::Max(Settings.WindSeaFetchLengthKm * 1000.0, 100.0);
    const double TotalHs = FMath::Max(Settings.SignificantWaveHeightMeters, 0.0);

    if (U10 < 0.25 || TotalHs <= KINDA_SMALL_NUMBER)
    {
        Result.SwellSignificantWaveHeightMeters = TotalHs;
        Result.WindSeaSignificantWaveHeightMeters = 0.0;
        Result.WindSeaPeakPeriodSeconds = 2.0;
        Result.WindSeaEnergyFraction = 0.0;
        return Result;
    }

    // Fetch-limited JONSWAP-style relations (Hasselmann family): dimensionless fetch X~=gF/U10^2,
    // peak frequency f~p=3.5 X~^-0.33 and surface-variance epsilon~=9.91e-8 X~^1.1.
    const double X = FMath::Clamp(G * FetchMeters / (U10 * U10), 10.0, 1.0e8);
    const double DimensionlessPeakFrequency = 3.5 * FMath::Pow(X, -0.33);
    const double PeakFrequencyHz = DimensionlessPeakFrequency * G / U10;
    const double RawTp = 1.0 / FMath::Max(PeakFrequencyHz, 0.05);
    Result.WindSeaPeakPeriodSeconds = FMath::Clamp(
        FMath::Min(RawTp, Settings.DominantSwellPeriodSeconds * 0.92), 1.5, 12.0);

    const double DimensionlessVariance = 9.91e-8 * FMath::Pow(X, 1.1);
    const double ForecastHs = 4.0 * FMath::Sqrt(FMath::Max(DimensionlessVariance, 0.0)) * (U10 * U10) / G;
    const double WindHs = FMath::Clamp(ForecastHs * FMath::Max(Settings.WindSeaEnergyScale, 0.0), 0.0, TotalHs);
    const double SwellHs = FMath::Sqrt(FMath::Max(TotalHs * TotalHs - WindHs * WindHs, 0.0));

    Result.WindSeaSignificantWaveHeightMeters = WindHs;
    Result.SwellSignificantWaveHeightMeters = SwellHs;
    Result.WindSeaEnergyFraction = (TotalHs > KINDA_SMALL_NUMBER)
        ? FMath::Clamp((WindHs * WindHs) / (TotalHs * TotalHs), 0.0, 1.0)
        : 0.0;
    return Result;
}

void URealOceanWaveLibrary::CalculateSpatiotemporalPhaseAnchors(
    FVector2D AnchorWorldXYMeters,
    const FRealOceanWaveSettings& Settings,
    double GravityMS2,
    double TimeSeconds,
    FLinearColor& OutPhase0123,
    FLinearColor& OutPhase4567)
{
    using namespace RealOceanWaves;
    const double G = FMath::Max(GravityMS2, 0.01);
    const FRealOceanSpectrumPartition Partition = CalculateSpectrumPartition(Settings, G);
    float Phases[NumBands] = {};

    for (int32 Index = 0; Index < NumBands; ++Index)
    {
        const FBand B = GetBand(Index, Settings, Partition, G);
        const double K = TwoPi / FMath::Max(B.WavelengthMeters, 0.05);
        const FVector2D D = DirectionFromDegrees(B.DirectionDegrees);
        const double Omega = FMath::Sqrt(G * K);
        double Phase = FMath::Fmod(K * FVector2D::DotProduct(D, AnchorWorldXYMeters) - Omega * TimeSeconds, TwoPi);
        if (Phase < 0.0) Phase += TwoPi;
        Phases[Index] = static_cast<float>(Phase);
    }

    OutPhase0123 = FLinearColor(Phases[0], Phases[1], Phases[2], Phases[3]);
    OutPhase4567 = FLinearColor(Phases[4], Phases[5], Phases[6], Phases[7]);
}

void URealOceanWaveLibrary::CalculateMicroSpatiotemporalPhaseAnchors(
    FVector2D AnchorWorldXYMeters,
    const FRealOceanWaveSettings& Settings,
    double TimeSeconds,
    FLinearColor& OutPhase0123)
{
    using namespace RealOceanWaves;
    float Phases[NumMicroBands] = {};
    for (int32 Index = 0; Index < NumMicroBands; ++Index)
    {
        const double K = TwoPi / MicroWavelengthMeters[Index];
        const FVector2D D = DirectionFromDegrees(Settings.WindDirectionDegrees + MicroDirectionOffsetDeg[Index]);
        const double Omega = FMath::Sqrt(9.80665 * K) * MicroTimeScale[Index];
        double Phase = FMath::Fmod(K * FVector2D::DotProduct(D, AnchorWorldXYMeters) - Omega * TimeSeconds, TwoPi);
        if (Phase < 0.0) Phase += TwoPi;
        Phases[Index] = static_cast<float>(Phase);
    }
    OutPhase0123 = FLinearColor(Phases[0], Phases[1], Phases[2], Phases[3]);
}

double URealOceanWaveLibrary::CalculateCriticalAngleDegrees(double IncidentIOR, double TransmittedIOR)
{
    const double N1 = FMath::Max(1.0e-6, IncidentIOR);
    const double N2 = FMath::Max(1.0e-6, TransmittedIOR);
    if (N1 <= N2) return 90.0;
    return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(N2 / N1, 0.0, 1.0)));
}

FRealOceanWaveSample URealOceanWaveLibrary::EvaluateWaveField(
    FVector2D WorldXYMeters,
    double TimeSeconds,
    const FRealOceanWaveSettings& Settings,
    double GravityMS2)
{
    using namespace RealOceanWaves;
    FRealOceanWaveSample Result;
    const double G = FMath::Max(GravityMS2, 0.01);
    const FRealOceanSpectrumPartition Partition = CalculateSpectrumPartition(Settings, G);

    FVector Displacement = FVector::ZeroVector;
    FVector TangentX(1.0, 0.0, 0.0);
    FVector TangentY(0.0, 1.0, 0.0);

    for (int32 Index = 0; Index < NumBands; ++Index)
    {
        const FBand B = GetBand(Index, Settings, Partition, G);
        const double K = TwoPi / FMath::Max(B.WavelengthMeters, 0.05);
        const double Omega = FMath::Sqrt(G * K);
        const FVector2D D = DirectionFromDegrees(B.DirectionDegrees);
        const double Phase = K * FVector2D::DotProduct(D, WorldXYMeters) - Omega * TimeSeconds + B.PhaseOffset;
        const double S = FMath::Sin(Phase);
        const double C = FMath::Cos(Phase);

        const double HorizontalAmplitude = B.HorizontalScale * B.AmplitudeMeters;
        const double Horizontal = HorizontalAmplitude * C;
        Displacement.X += D.X * Horizontal;
        Displacement.Y += D.Y * Horizontal;
        Displacement.Z += B.AmplitudeMeters * S;

        const double CommonHorizontal = HorizontalAmplitude * K * S;
        const double CommonVertical = B.AmplitudeMeters * K * C;
        TangentX.X += -D.X * D.X * CommonHorizontal;
        TangentX.Y += -D.X * D.Y * CommonHorizontal;
        TangentX.Z +=  D.X * CommonVertical;
        TangentY.X += -D.X * D.Y * CommonHorizontal;
        TangentY.Y += -D.Y * D.Y * CommonHorizontal;
        TangentY.Z +=  D.Y * CommonVertical;
    }

    Result.HeightMeters = Displacement.Z;
    Result.DisplacementMeters = Displacement;
    Result.HorizontalJacobian = TangentX.X * TangentY.Y - TangentX.Y * TangentY.X;
    Result.Normal = FVector::CrossProduct(TangentX, TangentY).GetSafeNormal();
    if (Result.Normal.Z < 0.0) Result.Normal *= -1.0;
    return Result;
}

FRealOceanWaveSample URealOceanWaveLibrary::EvaluateSurfaceAtWorldXY(
    FVector2D WorldXYMeters,
    double TimeSeconds,
    const FRealOceanWaveSettings& Settings,
    double GravityMS2,
    int32 SolveIterations)
{
    FVector2D ParameterXY = WorldXYMeters;
    const int32 Iterations = FMath::Clamp(SolveIterations, 0, 8);
    for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
    {
        const FRealOceanWaveSample Probe = EvaluateWaveField(ParameterXY, TimeSeconds, Settings, GravityMS2);
        ParameterXY = WorldXYMeters - FVector2D(Probe.DisplacementMeters.X, Probe.DisplacementMeters.Y);
    }
    return EvaluateWaveField(ParameterXY, TimeSeconds, Settings, GravityMS2);
}
