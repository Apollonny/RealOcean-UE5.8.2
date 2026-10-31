using UnrealBuildTool;

public class RealOceanEditor : ModuleRules
{
    public RealOceanEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "RealOcean"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AssetRegistry",
            "AssetTools",
            "DeveloperToolSettings",
            "MaterialEditor",
            "Projects",
            "UnrealEd"
        });
    }
}
