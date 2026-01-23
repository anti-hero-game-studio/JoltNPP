// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltMoverCVDEditor.h"

#include "JoltMoverCVDExtension.h"
#include "ExtensionsSystem/ChaosVDExtensionsManager.h"

#define LOCTEXT_NAMESPACE "FJoltMoverCVDEditorModule"

void FJoltMoverCVDEditorModule::StartupModule()
{
	TSharedRef<FJoltMoverCVDExtension> NewExtension = MakeShared<FJoltMoverCVDExtension>();
	FChaosVDExtensionsManager::Get().RegisterExtension(NewExtension);
	AvailableExtensions.Add(NewExtension);
}

void FJoltMoverCVDEditorModule::ShutdownModule()
{
	for (const TWeakPtr<FChaosVDExtension>& Extension : AvailableExtensions)
	{
		if (const TSharedPtr<FChaosVDExtension>& ExtensionPtr = Extension.Pin())
		{
			FChaosVDExtensionsManager::Get().UnRegisterExtension(ExtensionPtr.ToSharedRef());
		}
	}

	AvailableExtensions.Reset();
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FJoltMoverCVDEditorModule, JoltMoverCVDEditor)