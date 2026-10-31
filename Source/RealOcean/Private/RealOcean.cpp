#include "RealOcean.h"
#include "RealOceanExperimentalViewExtension.h"
#include "RealOceanReleaseConfig.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogRealOcean);

#define LOCTEXT_NAMESPACE "FRealOceanModule"

void FRealOceanModule::StartupModule()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("RealOcean"));
    if (Plugin.IsValid())
    {
        const FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders/Private"));
        AddShaderSourceDirectoryMapping(TEXT("/Plugin/RealOcean"), ShaderDir);
    }

    StartRealOceanExperimentalViewExtension();
    UE_LOG(LogRealOcean, Display, TEXT("RealOcean 0.33.5 visual-polish turnkey runtime; Shipping caustics: %s; Shipping foam: %s; Certified foam preset: %s"),
        RealOceanRelease::GetCompiledCausticsPathName(), RealOceanRelease::GetCompiledFoamPathName(), RealOceanRelease::GetCertifiedFoamPresetName());
    UE_LOG(LogRealOcean, Display,
        TEXT("RO_M32_RELEASE bundle=%s path=%s certified=%d preset=%s resolution=%d rate_hz=%d profile_build_id=%s capture_sha256=%s"),
        *RealOceanRelease::GetReleaseBundleId(), RealOceanRelease::GetCompiledFoamPathName(), RealOceanRelease::HasCertifiedGPUFoamRelease() ? 1 : 0,
        RealOceanRelease::GetCertifiedFoamPresetName(), RealOceanRelease::GetCertifiedFoamResolution(), RealOceanRelease::GetCertifiedFoamUpdateRateHz(),
        *RealOceanRelease::GetCertifiedProfileBuildId(), *RealOceanRelease::GetCertifiedCaptureSHA256());
}

void FRealOceanModule::ShutdownModule()
{
    StopRealOceanExperimentalViewExtension();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRealOceanModule, RealOcean)
