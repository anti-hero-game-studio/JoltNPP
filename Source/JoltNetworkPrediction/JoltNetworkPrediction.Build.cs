// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class JoltNetworkPrediction : ModuleRules
	{
		public JoltNetworkPrediction(ReadOnlyTargetRules Target) : base(Target)
		{
			OptimizeCode = CodeOptimization.InShippingBuildsOnly;
			
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"NetCore",
                    "Engine",
                    "RenderCore",
					"PhysicsCore",
					"Chaos",
					"TraceLog",
					"JoltBridge"
				}
				);

            // Only needed for the PIE delegate in FJoltNetworkPredictionModule::StartupModule
            if (Target.Type == TargetType.Editor) {
                PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "UnrealEd",
                });
            }

			SetupIrisSupport(Target);
		}
	}
}
