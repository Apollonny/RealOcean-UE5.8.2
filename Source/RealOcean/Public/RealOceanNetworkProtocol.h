#pragma once

#include "CoreMinimal.h"

// Explicit wire/semantic versions for FRealOceanNetworkClockSync.
// SnapshotProtocolVersion changes when transport/epoch semantics are incompatible.
// SettingsSchemaVersion changes when FRealOceanSettings snapshot meaning/layout is incompatible.
namespace RealOceanNetworkProtocol
{
    inline constexpr uint16 SnapshotProtocolVersion = 1;
    inline constexpr uint16 SettingsSchemaVersion = 1;
}
