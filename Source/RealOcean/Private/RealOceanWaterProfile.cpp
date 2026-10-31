#include "RealOceanWaterProfile.h"

void URealOceanWaterProfile::ApplyToSettings(FRealOceanSettings& InOutSettings) const
{
    InOutSettings.AbsorptionRGBPerMeter = AbsorptionRGBPerMeter;
    InOutSettings.ScatteringRGBPerMeter = ScatteringRGBPerMeter;
    InOutSettings.InscatterColor = InscatterColor;
    InOutSettings.InscatterIntensity = InscatterIntensity;
    InOutSettings.Turbidity = Turbidity;
    InOutSettings.MaximumDepthMeters = 3000.0;
}
