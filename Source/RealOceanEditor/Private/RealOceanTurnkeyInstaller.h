#pragma once

struct FRealOceanTurnkeyInstaller
{
    static bool EnsureTurnkeyContent(bool bForceRebuild = false);
    static bool ValidateTurnkeyContent();
};
