#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FRealOceanEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    bool HandleTurnkeyRetry(float DeltaTime);
    void CancelTurnkeyRetry();

    FDelegateHandle TurnkeyRetryHandle;
    int32 TurnkeyRetryAttempts = 0;
};
