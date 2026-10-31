#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RealOceanTypes.h"
#include "RealOceanWaterProfile.generated.h"

// Optional versioned optical profile. It changes water appearance only; the physical 3000 m depth contract stays fixed.
UCLASS(BlueprintType)
class REALOCEAN_API URealOceanWaterProfile : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Real Ocean|Profile")
    int32 ProfileVersion = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Real Ocean|Profile")
    FName ProfileName = TEXT("ClearOpenOcean");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Real Ocean|Optics", meta=(ClampMin="0.0"))
    FVector AbsorptionRGBPerMeter = FVector(0.15, 0.03, 0.012);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Real Ocean|Optics", meta=(ClampMin="0.0"))
    FVector ScatteringRGBPerMeter = FVector(0.020, 0.012, 0.008);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Real Ocean|Optics")
    FLinearColor InscatterColor = FLinearColor(0.018f, 0.115f, 0.165f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Real Ocean|Optics", meta=(ClampMin="0.0", ClampMax="8.0"))
    double InscatterIntensity = 1.0;

    // Dimensionless particulate optical control; 0.10 is neutral relative to the profile coefficients.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Real Ocean|Optics", meta=(ClampMin="0.0", ClampMax="5.0"))
    double Turbidity = 0.10;

    UFUNCTION(BlueprintCallable, Category = "Real Ocean|Profile")
    void ApplyToSettings(UPARAM(ref) FRealOceanSettings& InOutSettings) const;
};
