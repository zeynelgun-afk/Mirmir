using UnrealBuildTool;

public class Mirmir : ModuleRules
{
    public Mirmir(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore"
        });
        PrivateDependencyModuleNames.AddRange(new string[] { "Json", "ImageWrapper", "ImageCore", "RenderCore", "RHI" });
    }
}
