// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "ChaosVDTabSpawnerBase.h"
#include "Widgets/SChaosVDDetailsView.h"
#include "ChaosVDSolverDataSelection.h"
#include "ChaosVDObjectDetailsTab.h"
#include "TEDS/ChaosVDStructTypedElementData.h"

class SOutputLog;
class FOutputLogHistory;
class FName;
struct FJoltMoverSyncState;
struct FJoltMoverInputCmdContext;
struct FJoltMoverCVDSimDataWrapper;
struct FJoltMoverDataCollection;
class UJoltMoverCVDSimDataComponent;

/** This tab is an additional details tab displaying JoltMover info corresponding to the selected particles if they are moved by a JoltMover component */
class FJoltMoverCVDTab : public FChaosVDObjectDetailsTab
{
public:

	FJoltMoverCVDTab(const FName& InTabID, TSharedPtr<FTabManager> InTabManager, TWeakPtr<SChaosVDMainTab> InOwningTabWidget);
	virtual ~FJoltMoverCVDTab();

	// Implementation of FChaosVDObjectDetailsTab
	virtual TSharedRef<SDockTab> HandleTabSpawnRequest(const FSpawnTabArgs& Args) override;
	virtual void HandlePostSelectionChange(const UTypedElementSelectionSet* ChangedSelectionSet) override;

	virtual void HandleSolverDataSelectionChange(const TSharedPtr<FChaosVDSolverDataSelectionHandle>& SelectionHandle) override;

	// Scene callbacks
	void HandleSceneUpdated();

private:
	void DisplaySingleParticleInfo(int32 SelectedSolverID, int32 SelectedParticleID);
	void DisplayJoltMoverInfoForSelectedElements(const TArray<FTypedElementHandle>& SelectedElementHandles);

	// This function retrieves and caches all the JoltMover data components for all solvers, populating SolverToSimDataComponentMap
	void RetrieveAllSolversJoltMoverDataComponents();

	TWeakObjectPtr<UJoltMoverCVDSimDataComponent>* FindJoltMoverDataComponentForSolver(int32 SolverID);

	TWeakPtr<FChaosVDScene> SceneWeakPtr;

	TMap<int32, TWeakObjectPtr<UJoltMoverCVDSimDataComponent>> SolverToSimDataComponentMap;

	int32 CurrentlyDisplayedParticleID = INDEX_NONE;
	int32 CurrentlyDisplayedSolverID = INDEX_NONE;

	FChaosVDSelectionMultipleView MultiViewWrapper;

	TSharedPtr<FJoltMoverCVDSimDataWrapper> JoltMoverSimDataWrapper;
	TSharedPtr<FJoltMoverSyncState> JoltMoverSyncState;
	TSharedPtr<FJoltMoverInputCmdContext> JoltMoverInputCmd;
	TSharedPtr<FJoltMoverDataCollection> JoltMoverLocalSimData;
};


