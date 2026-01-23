using UnrealBuildTool;

public class JoltEditor : ModuleRules
{
    public JoltEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "UnrealEd"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore", 
                "JoltBridge",
                "UnrealEd",
                "ToolMenus",
                "Projects"
            }
        );
    }
}