// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

struct IConsoleCommand;
class UWorld;

namespace UE::JoltMover
{
	extern int32 DisableDataCopyInPlace;
}

class FJoltMoverModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	void ShowTrajectory(const TArray<FString>& Args, UWorld* World);
	void ShowTrail(const TArray<FString>& Args, UWorld* World);
	void ShowCorrections(const TArray<FString>& Args, UWorld* World);
	
private:
	TArray<IConsoleCommand*> ConsoleCommands;
};
