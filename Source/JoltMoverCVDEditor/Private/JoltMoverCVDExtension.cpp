// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltMoverCVDExtension.h"
#include "JoltMoverCVDSimDataComponent.h"
#include "JoltMoverCVDSimDataProcessor.h"
#include "JoltMoverCVDTab.h"
#include "JoltMoverCVDStyle.h"
#include "Widgets/SChaosVDMainTab.h"

namespace NJoltMoverCVDExtension
{
	static const FName JoltMoverTabName = FName(TEXT("JoltMover Info"));
	static const FName ExtensionName = FName(TEXT("FJoltMoverCVDExtension"));
};

FJoltMoverCVDExtension::FJoltMoverCVDExtension() : FChaosVDExtension()
{
	DataComponentsClasses.Add(UJoltMoverCVDSimDataComponent::StaticClass());

	ExtensionName = NJoltMoverCVDExtension::ExtensionName;

	FJoltMoverCVDStyle::Initialize();
}

FJoltMoverCVDExtension::~FJoltMoverCVDExtension()
{
	DataComponentsClasses.Reset();

	FJoltMoverCVDStyle::Shutdown();
}

void FJoltMoverCVDExtension::RegisterDataProcessorsInstancesForProvider(const TSharedRef<FChaosVDTraceProvider>& InTraceProvider)
{
	FChaosVDExtension::RegisterDataProcessorsInstancesForProvider(InTraceProvider);

    TSharedPtr<FJoltMoverCVDSimDataProcessor> SimDataProcessor = MakeShared<FJoltMoverCVDSimDataProcessor>();
    SimDataProcessor->SetTraceProvider(InTraceProvider);
    InTraceProvider->RegisterDataProcessor(SimDataProcessor);
}

TConstArrayView<TSubclassOf<UActorComponent>> FJoltMoverCVDExtension::GetSolverDataComponentsClasses()
{
	return DataComponentsClasses;
}

void FJoltMoverCVDExtension::RegisterCustomTabSpawners(const TSharedRef<SChaosVDMainTab>& InParentTabWidget)
{
	InParentTabWidget->RegisterTabSpawner<FJoltMoverCVDTab>(NJoltMoverCVDExtension::JoltMoverTabName);
}
