#include "RealOceanEditor.h"

#include "RealOceanTurnkeyInstaller.h"
#include "RealOcean.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"

namespace
{
    constexpr int32 MaxTurnkeyRetryAttempts = 5;
    constexpr float TurnkeyRetryDelaySeconds = 1.0f;

    FAutoConsoleCommand RebuildTurnkeyContentCommand(
        TEXT("RealOcean.RebuildTurnkeyContent"),
        TEXT("Rebuilds RealOcean-owned generated MPC/material assets. User-owned path collisions are never overwritten; cook/CI performs authoritative shader compilation."),
        FConsoleCommandDelegate::CreateStatic([]()
        {
            if (!FRealOceanTurnkeyInstaller::EnsureTurnkeyContent(true))
            {
                UE_LOG(LogRealOcean, Error, TEXT("RealOcean.RebuildTurnkeyContent failed. See earlier RealOcean errors for the blocking condition."));
            }
        }));
}

void FRealOceanEditorModule::StartupModule()
{
    // Cook/CI is validation-only. Never mutate project content from a commandlet: this makes missing
    // generated content a deterministic non-zero failure instead of an editor-only side effect.
    if (IsRunningCommandlet())
    {
        if (!FRealOceanTurnkeyInstaller::ValidateTurnkeyContent())
        {
            UE_LOG(LogRealOcean, Error, TEXT("RealOcean turnkey validation failed in commandlet/CI."));
            FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("RealOcean turnkey commandlet validation failed"));
        }
        return;
    }

    // PostEngineInit is normally late enough for AssetTools/MaterialEditor. Still fail safely and retry
    // transient startup failures (asset registry/source-control/config timing) instead of leaving a fresh
    // install permanently half-generated until the next editor restart.
    if (FRealOceanTurnkeyInstaller::EnsureTurnkeyContent(false))
    {
        return;
    }

    TurnkeyRetryAttempts = 0;
    TurnkeyRetryHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FRealOceanEditorModule::HandleTurnkeyRetry),
        TurnkeyRetryDelaySeconds);
    UE_LOG(LogRealOcean, Warning, TEXT("RealOcean turnkey startup was not ready; scheduling bounded automatic retries."));
}

void FRealOceanEditorModule::ShutdownModule()
{
    CancelTurnkeyRetry();
}

bool FRealOceanEditorModule::HandleTurnkeyRetry(float DeltaTime)
{
    (void)DeltaTime;
    ++TurnkeyRetryAttempts;

    if (FRealOceanTurnkeyInstaller::EnsureTurnkeyContent(false))
    {
        UE_LOG(LogRealOcean, Display, TEXT("RealOcean turnkey content recovered after %d retry attempt(s)."), TurnkeyRetryAttempts);
        TurnkeyRetryHandle.Reset();
        return false;
    }

    if (TurnkeyRetryAttempts >= MaxTurnkeyRetryAttempts)
    {
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean turnkey content is still not ready after %d retries. Check the preceding compile/save/path-collision error and restart after fixing it."), TurnkeyRetryAttempts);
        TurnkeyRetryHandle.Reset();
        return false;
    }

    return true;
}

void FRealOceanEditorModule::CancelTurnkeyRetry()
{
    if (TurnkeyRetryHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TurnkeyRetryHandle);
        TurnkeyRetryHandle.Reset();
    }
}

IMPLEMENT_MODULE(FRealOceanEditorModule, RealOceanEditor)
