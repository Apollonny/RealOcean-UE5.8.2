#pragma once

#include "CoreMinimal.h"
#include "RealOceanGeneratedFoamReleaseGate.h"

// Shipping release-path selection is compile-time on purpose.
// Define REALOCEAN_RELEASE_CAUSTICS_PATH from the game's Target.cs GlobalDefinitions:
//   0 = Full-resolution material caustics (default / safest fallback)
//   1 = Spatial half-resolution RDG caustics
//   2 = Velocity-aware temporal half-resolution RDG caustics
#ifndef REALOCEAN_RELEASE_CAUSTICS_PATH
#define REALOCEAN_RELEASE_CAUSTICS_PATH 0
#endif

// Stateful GPU foam is evidence-locked.
// 0 = M27 analytic fallback (default, no GPU clipmap allocation in Shipping)
// 1 = persistent GPU clipmap, but ONLY when RealOceanGeneratedFoamReleaseGate.h contains
//     a certificate produced by the project's own target-hardware qualification pipeline.
#ifndef REALOCEAN_RELEASE_FOAM_PATH
#define REALOCEAN_RELEASE_FOAM_PATH 0
#endif

static_assert(REALOCEAN_RELEASE_CAUSTICS_PATH >= 0 && REALOCEAN_RELEASE_CAUSTICS_PATH <= 2,
    "REALOCEAN_RELEASE_CAUSTICS_PATH must be 0 (Full), 1 (SpatialHalf), or 2 (TemporalHalf).");
static_assert(REALOCEAN_RELEASE_FOAM_PATH >= 0 && REALOCEAN_RELEASE_FOAM_PATH <= 1,
    "REALOCEAN_RELEASE_FOAM_PATH must be 0 (AnalyticM27) or 1 (GPUClipmapM32).");

#if REALOCEAN_RELEASE_FOAM_PATH == 1
static_assert(REALOCEAN_GPU_FOAM_RELEASE_CERTIFIED == 1,
    "GPU foam Shipping path is fail-closed in this distribution. Keep AnalyticM27 or install a separately qualified target-hardware certificate.");
static_assert(REALOCEAN_GPU_FOAM_RELEASE_CERT_SCHEMA == 2,
    "GPU foam release certificate schema must be M32 schema 2.");
static_assert(REALOCEAN_GPU_FOAM_RELEASE_CERT_MILESTONE == 32,
    "GPU foam release certificate was not generated for Milestone 32.");
static_assert(REALOCEAN_GPU_FOAM_RELEASE_CERT_BUILD_REVISION == 335,
    "GPU foam release certificate does not match RealOcean 0.33.5 build revision 335. Re-profile and re-certify this exact build.");
static_assert(REALOCEAN_GPU_FOAM_RELEASE_CERT_PRESET >= 1 && REALOCEAN_GPU_FOAM_RELEASE_CERT_PRESET <= 3,
    "GPU foam release certificate must lock Low256(1), Medium384(2), or High512(3).");
static_assert(
    (REALOCEAN_GPU_FOAM_RELEASE_CERT_PRESET == 1 && REALOCEAN_GPU_FOAM_RELEASE_CERT_RESOLUTION == 256 && REALOCEAN_GPU_FOAM_RELEASE_CERT_UPDATE_RATE_HZ == 30) ||
    (REALOCEAN_GPU_FOAM_RELEASE_CERT_PRESET == 2 && REALOCEAN_GPU_FOAM_RELEASE_CERT_RESOLUTION == 384 && REALOCEAN_GPU_FOAM_RELEASE_CERT_UPDATE_RATE_HZ == 45) ||
    (REALOCEAN_GPU_FOAM_RELEASE_CERT_PRESET == 3 && REALOCEAN_GPU_FOAM_RELEASE_CERT_RESOLUTION == 512 && REALOCEAN_GPU_FOAM_RELEASE_CERT_UPDATE_RATE_HZ == 60),
    "GPU foam certificate preset/resolution/update-rate tuple is inconsistent.");
#endif

namespace RealOceanRelease
{
    enum class ECausticsPath : uint8
    {
        FullResolution = 0,
        SpatialHalfResolution = 1,
        TemporalHalfResolution = 2
    };

    constexpr ECausticsPath GetCompiledCausticsPath()
    {
        return static_cast<ECausticsPath>(REALOCEAN_RELEASE_CAUSTICS_PATH);
    }

    constexpr bool UsesRDGCaustics()
    {
        return REALOCEAN_RELEASE_CAUSTICS_PATH == 1 || REALOCEAN_RELEASE_CAUSTICS_PATH == 2;
    }

    constexpr bool UsesTemporalRDGCaustics()
    {
        return REALOCEAN_RELEASE_CAUSTICS_PATH == 2;
    }

    constexpr bool UsesGPUFoamClipmap()
    {
        return REALOCEAN_RELEASE_FOAM_PATH == 1;
    }

    constexpr bool HasCertifiedGPUFoamRelease()
    {
        return REALOCEAN_GPU_FOAM_RELEASE_CERTIFIED == 1;
    }

    constexpr int32 GetCertifiedFoamPresetCode()
    {
        return REALOCEAN_GPU_FOAM_RELEASE_CERT_PRESET;
    }

    constexpr int32 GetCertifiedFoamResolution()
    {
        return REALOCEAN_GPU_FOAM_RELEASE_CERT_RESOLUTION;
    }

    constexpr int32 GetCertifiedFoamUpdateRateHz()
    {
        return REALOCEAN_GPU_FOAM_RELEASE_CERT_UPDATE_RATE_HZ;
    }

    inline FString GetReleaseBundleId()
    {
        return UsesGPUFoamClipmap() ? FString(UTF8_TO_TCHAR(REALOCEAN_GPU_FOAM_RELEASE_CERT_BUNDLE_ID)) : FString(TEXT("ANALYTIC-M32"));
    }

    inline FString GetCertifiedProfileBuildId()
    {
        return FString(UTF8_TO_TCHAR(REALOCEAN_GPU_FOAM_RELEASE_CERT_BUILD_ID));
    }

    inline FString GetCertifiedCaptureSHA256()
    {
        return FString(UTF8_TO_TCHAR(REALOCEAN_GPU_FOAM_RELEASE_CERT_CAPTURE_SHA256));
    }

    inline const TCHAR* GetCertifiedFoamPresetName()
    {
        switch (GetCertifiedFoamPresetCode())
        {
        case 1: return TEXT("Low256");
        case 2: return TEXT("Medium384");
        case 3: return TEXT("High512");
        default: return TEXT("Uncertified");
        }
    }

    inline const TCHAR* GetCompiledFoamPathName()
    {
        return UsesGPUFoamClipmap() ? TEXT("GPUClipmapM32Certified") : TEXT("AnalyticM27");
    }

    inline const TCHAR* GetCompiledCausticsPathName()
    {
        switch (GetCompiledCausticsPath())
        {
        case ECausticsPath::SpatialHalfResolution:
            return TEXT("SpatialHalfResolution");
        case ECausticsPath::TemporalHalfResolution:
            return TEXT("TemporalHalfResolution");
        case ECausticsPath::FullResolution:
        default:
            return TEXT("FullResolution");
        }
    }
}
