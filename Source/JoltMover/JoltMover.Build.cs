// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class JoltMover : ModuleRules
{
	public JoltMover(ReadOnlyTargetRules Target) : base(Target)
	{
		OptimizeCode = CodeOptimization.InShippingBuildsOnly;

		// TODO: find a better way to manage optional dependencies, such as Water and PoseSearch. This includes module dependencies here, as well as .uplugin dependencies.

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"NetCore",
				"InputCore",
				"JoltNetworkPrediction",
				"JoltBridge",
				"AnimGraphRuntime",
				"MotionWarping",
				"Water",
				"GameplayTags",
				"NavigationSystem",
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Chaos",
				"CoreUObject",
				"Engine",
				"PhysicsCore",
				"DeveloperSettings",
				"PoseSearch",
				"JoltBridge",
			}
			);

		if (IsChaosVisualDebuggerSupported(Target))
		{
			PublicDependencyModuleNames.Add("JoltMoverCVDData");
		}
		
		SetupModuleChaosVisualDebuggerSupport(Target);
		SetupGameplayDebuggerSupport(Target);

		//SetupModulePhysicsSupport(Target);
	}
}
