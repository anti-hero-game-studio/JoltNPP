// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class JoltMoverEditor : ModuleRules
{
	public JoltMoverEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		OptimizeCode = CodeOptimization.Never;
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"JoltMover",
				"BlueprintGraph",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
			});

	}
}