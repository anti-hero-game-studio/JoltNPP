// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltBridge.h"
#include "JoltBridgeCoreSettings.h"
#include "ISettingsModule.h"

#define LOCTEXT_NAMESPACE "FJoltBridgeNPPModule"


namespace JoltBridgePhysicsEngine
{
	int32 DisableDataCopyInPlace = 0;
	static FAutoConsoleVariableRef CVarDisableDataCopyInPlace(
		TEXT("joltBridge.debug.DisableDataCopyInPlace"),
		DisableDataCopyInPlace,
		TEXT("Whether to allow JoltBridge data collections with identical contained struct types to be copied in place, avoiding reallocating memory"),
		ECVF_Default);
}


void FJoltBridgeModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{
		SettingsModule->RegisterSettings("Project", "Project", "JoltBridge",
			LOCTEXT("JoltBridgeNetworkPredictionSettingsName", "JoltBridge"),
			LOCTEXT("JoltBridgeNetworkPredictionSettingsDescription", "Settings for the JoltBridge Physics runtime module."),
			GetMutableDefault<UJoltSettings>()
		);
	}
}

void FJoltBridgeModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	
#if WITH_EDITOR

	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Project", "JoltBridge");
	}
#endif
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FJoltBridgeModule, JoltBridge)