// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class JoltMoverCVDData : ModuleRules
{
    public JoltMoverCVDData(ReadOnlyTargetRules Target) : base(Target)
    {
	    OptimizeCode = CodeOptimization.InShippingBuildsOnly;
	    
	    PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"ChaosVDRuntime",
			}
		);
	}
}