// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class JoltMoverCVDEditor : ModuleRules
{
    public JoltMoverCVDEditor(ReadOnlyTargetRules Target) : base(Target)
    {
	    OptimizeCode = CodeOptimization.InShippingBuildsOnly;
	    
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
			});

		PrivateDependencyModuleNames.AddRange(
		    new string[]
		    {
				"ApplicationCore",
				"ChaosVD",
				"ChaosVDData",
				"ChaosVDRuntime",
				"CoreUObject",
				"EditorWidgets",
				"Engine",
				"JoltMover",
				"JoltMoverCVDData",
				"Projects",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"TraceServices",
				"TypedElementFramework",
				"UnrealEd",
			}
		);
        
        SetupModulePhysicsSupport(Target);

		SetupModuleChaosVisualDebuggerSupport(Target);
	}
}