#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RealOceanNetworkProtocol.h"
#include "RealOceanValidationLibrary.generated.h"

class ARealOceanController;

UENUM(BlueprintType)
enum class ERealOceanValidationSeverity : uint8
{
    Info    UMETA(DisplayName = "Info"),
    Warning UMETA(DisplayName = "Warning"),
    Error   UMETA(DisplayName = "Error")
};

USTRUCT(BlueprintType)
struct REALOCEAN_API FRealOceanValidationMessage
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Real Ocean|Validation")
    ERealOceanValidationSeverity Severity = ERealOceanValidationSeverity::Info;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Real Ocean|Validation")
    FString Message;
};

USTRUCT(BlueprintType)
struct REALOCEAN_API FRealOceanValidationReport
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Real Ocean|Validation")
    bool bReadyForRuntime = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Real Ocean|Validation")
    int32 ErrorCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Real Ocean|Validation")
    int32 WarningCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Real Ocean|Validation")
    TArray<FRealOceanValidationMessage> Messages;
};

UCLASS()
class REALOCEAN_API URealOceanValidationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Real Ocean|Validation")
    static FRealOceanValidationReport ValidateController(const ARealOceanController* Controller, bool bLogResults = true);

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Validation")
    static int32 GetMPCContractVersion() { return 12; }

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Validation")
    static int32 GetShaderContractVersion() { return 19; }

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Validation")
    static int32 GetReleaseCandidateVersion() { return 37; }

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Validation")
    static int32 GetNetworkSnapshotProtocolVersion() { return RealOceanNetworkProtocol::SnapshotProtocolVersion; }

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Validation")
    static int32 GetNetworkSettingsSchemaVersion() { return RealOceanNetworkProtocol::SettingsSchemaVersion; }


    UFUNCTION(BlueprintPure, Category = "Real Ocean|Validation")
    static FString GetCompiledReleaseCausticsPath();

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Validation")
    static bool IsCompiledRDGCausticsReleasePath();

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Validation")
    static FString GetCompiledReleaseFoamPath();

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Validation")
    static bool IsCompiledGPUFoamReleasePath();


    UFUNCTION(BlueprintPure, Category = "Real Ocean|Validation")
    static bool IsGPUFoamReleaseCertified();

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Validation")
    static FString GetCertifiedGPUFoamPreset();

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Validation")
    static int32 GetCertifiedGPUFoamResolution();

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Validation")
    static int32 GetCertifiedGPUFoamUpdateRateHz();

    UFUNCTION(BlueprintPure, Category = "Real Ocean|Validation")
    static FString GetCompiledReleaseBundleId();

    static const TArray<FName>& GetRequiredScalarParameters();
    static const TArray<FName>& GetRequiredVectorParameters();
};
