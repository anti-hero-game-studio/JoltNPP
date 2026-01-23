// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltNetworkPredictionModule.h"
#include "JoltNetworkPredictionTrace.h"
#include "String/ParseTokens.h"
#include "JoltNetworkPredictionModelDefRegistry.h"
#include "Misc/CoreDelegates.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ISettingsModule.h"
#else
#include "Engine/World.h"
#endif


#define LOCTEXT_NAMESPACE "FJoltNetworkPredictionModule"

class FJoltNetworkPredictionModule : public IJoltNetworkPredictionModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void OnModulesChanged(FName ModuleThatChanged, EModuleChangeReason ReasonForChange);
	void FinalizeNetworkPredictionTypes();

	FDelegateHandle PieHandle;
	FDelegateHandle ModulesChangedHandle;
	FDelegateHandle WorldPreInitHandle;
};

void FJoltNetworkPredictionModule::StartupModule()
{
	// Disable by default unless in the command line args. This is temp as the existing insights -trace parsing happen before the plugin is loaded
	UE::Trace::ToggleChannel(TEXT("JoltNetworkPredictionChannel"), false);

	FString EnabledChannels;
	FParse::Value(FCommandLine::Get(), TEXT("-trace="), EnabledChannels, false);
	UE::String::ParseTokens(EnabledChannels, TEXT(","), [](FStringView Token) {
		if (Token.Compare(TEXT("JoltNetworkPrediction"), ESearchCase::IgnoreCase)==0 || Token.Compare(TEXT("NP"), ESearchCase::IgnoreCase)==0)
		{
		UE::Trace::ToggleChannel(TEXT("JoltNetworkPredictionChannel"), true);
		}
	});

	ModulesChangedHandle = FModuleManager::Get().OnModulesChanged().AddRaw(this, &FJoltNetworkPredictionModule::OnModulesChanged);

	// Finalize types if the engine is up and running, or register for callback for when it is
	if (GIsRunning)
	{
		FinalizeNetworkPredictionTypes();
	}
	else
	{
		FCoreDelegates::OnPostEngineInit.AddLambda([this]()
		{
			this->FinalizeNetworkPredictionTypes();
		});
	}
	
	this->WorldPreInitHandle = FWorldDelegates::OnPreWorldInitialization.AddLambda([this](UWorld* World, const UWorld::InitializationValues IVS)
	{
		UE_JNP_TRACE_WORLD_PREINIT();
	});

#if WITH_EDITOR
	PieHandle = FEditorDelegates::PreBeginPIE.AddLambda([](const bool bBegan)
	{
		UE_JNP_TRACE_PIE_START();
	});

	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{
		SettingsModule->RegisterSettings("Project", "Project", "Jolt Network Prediction",
			LOCTEXT("JoltNetworkPredictionSettingsName", "Jolt Network Prediction"),
			LOCTEXT("JoltNetworkPredictionSettingsDescription", "Settings for the Jolt Network Prediction runtime module."),
			GetMutableDefault<UJoltNetworkPredictionSettingsObject>()
		);
	}
#endif
}


void FJoltNetworkPredictionModule::ShutdownModule()
{
	if (ModulesChangedHandle.IsValid())
	{
		FModuleManager::Get().OnModulesChanged().Remove(ModulesChangedHandle);
		ModulesChangedHandle.Reset();
	}

	if (WorldPreInitHandle.IsValid())
	{
		FWorldDelegates::OnPreWorldInitialization.Remove(WorldPreInitHandle);
		WorldPreInitHandle.Reset();
	}

#if WITH_EDITOR
	FEditorDelegates::PreBeginPIE.Remove(PieHandle);

	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Project", "Jolt Network Prediction");
	}
#endif
}

void FJoltNetworkPredictionModule::OnModulesChanged(FName ModuleThatChanged, EModuleChangeReason ReasonForChange)
{
	// If we haven't finished loading, don't do per module finalizing
	if (GIsRunning == false)
	{
		return;
	}

	switch (ReasonForChange)
	{
	case EModuleChangeReason::ModuleLoaded:
		FinalizeNetworkPredictionTypes();
		break;

	case EModuleChangeReason::ModuleUnloaded:
		FinalizeNetworkPredictionTypes();
		break;
	}
}

void FJoltNetworkPredictionModule::FinalizeNetworkPredictionTypes()
{
	FGlobalCueTypeTable::Get().FinalizeCueTypes();
	FJoltNetworkPredictionModelDefRegistry::Get().FinalizeTypes();
}

IMPLEMENT_MODULE( FJoltNetworkPredictionModule, JoltNetworkPrediction )
#undef LOCTEXT_NAMESPACE

