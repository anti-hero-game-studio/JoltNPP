// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "../../../../../../../../EpicGames/UE_5.7/Engine/Plugins/ChaosVD/Source/ChaosVD/Public/ExtensionsSystem/ChaosVDExtension.h"
#include "Templates/SubclassOf.h"
#include "Templates/SharedPointer.h"

class FChaosVDTraceProvider;
class UActorComponent;
class SChaosVDMainTab;

/** JoltMoverCVDExtension is where we register JoltMoverCVDTab as a displayable tab, register JoltMoverCVDSimDataProcessor and give access to the JoltMoverSimDataComponent */
class FJoltMoverCVDExtension final : public FChaosVDExtension
{
public:
	
	FJoltMoverCVDExtension();
	virtual ~FJoltMoverCVDExtension() override;

	virtual void RegisterDataProcessorsInstancesForProvider(const TSharedRef<FChaosVDTraceProvider>& InTraceProvider) override;
	virtual TConstArrayView<TSubclassOf<UActorComponent>> GetSolverDataComponentsClasses() override;

	// Registers all available Tab Spawner instances in this extension, if any
	virtual void RegisterCustomTabSpawners(const TSharedRef<SChaosVDMainTab>& InParentTabWidget) override;

private:
	TArray<TSubclassOf<UActorComponent>> DataComponentsClasses;
};


