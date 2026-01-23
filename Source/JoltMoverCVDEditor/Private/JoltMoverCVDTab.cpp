// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltMoverCVDTab.h"

#include "ChaosLog.h"
#include "ChaosVDModule.h"
#include "ChaosVDScene.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SChaosVDMainTab.h"
#include "JoltMoverCVDStyle.h"
#include "JoltMoverSimulationTypes.h"
#include "JoltMoverCVDDataWrappers.h"
#include "JoltMoverCVDSimDataComponent.h"
#include "TEDS/ChaosVDSelectionInterface.h"
#include "ChaosVDSceneParticle.h"

#define LOCTEXT_NAMESPACE "JoltMoverCVDExtension"

FJoltMoverCVDTab::FJoltMoverCVDTab(const FName& InTabID, TSharedPtr<FTabManager> InTabManager, TWeakPtr<SChaosVDMainTab> InOwningTabWidget)
	: FChaosVDObjectDetailsTab(InTabID, InTabManager, InOwningTabWidget)
{
	// Cache a scene pointer for future reference
	if (TSharedPtr<SChaosVDMainTab> OwningTabWidgetSharedPtr = OwningTabWidget.Pin())
	{
		if (TSharedPtr<FChaosVDScene> Scene = OwningTabWidgetSharedPtr->GetScene())
		{
			SceneWeakPtr = Scene;
		}
	}
}

FJoltMoverCVDTab::~FJoltMoverCVDTab()
{
	if (TSharedPtr<FChaosVDScene> Scene = SceneWeakPtr.Pin())
	{
		Scene->OnSceneUpdated().RemoveAll(this);
	}
}

TSharedRef<SDockTab> FJoltMoverCVDTab::HandleTabSpawnRequest(const FSpawnTabArgs& Args)
{
	// I can't add callbacks in the constructor as SharedFromThis would complain
	if (TSharedPtr<FChaosVDScene> Scene = SceneWeakPtr.Pin())
	{
		Scene->OnSceneUpdated().AddSP(this, &FJoltMoverCVDTab::HandleSceneUpdated);
	}

	TSharedRef<SDockTab> Tab = FChaosVDObjectDetailsTab::HandleTabSpawnRequest(Args);

	// Let's rename the tab so its name is unique
	Tab->SetLabel(Args.GetTabId().ToText());

	Tab->SetTabIcon(FJoltMoverCVDStyle::Get().GetBrush("TabIconJoltMoverInfoPanel"));

	return Tab;
}

void FJoltMoverCVDTab::HandleSolverDataSelectionChange(const TSharedPtr<FChaosVDSolverDataSelectionHandle>& SelectionHandle)
{
	// We override the default behavior, otherwise we will display any struct that gets selected and can be displayed
	// while we only want to display JoltMover info
	return;
}

void FJoltMoverCVDTab::RetrieveAllSolversJoltMoverDataComponents()
{
	SolverToSimDataComponentMap.Empty();

	const TSharedPtr<FChaosVDScene> Scene = SceneWeakPtr.Pin();
	if (Scene)
	{
		const FChaosVDSolverInfoByIDMap& SolverInfoByIDMap = Scene->GetSolverInfoActorsMap();
		for (const auto& It : SolverInfoByIDMap)
		{
			int32 SolverID = It.Key;
			// No need to know about AChaosVDSolverInfoActor as we intend to solely find an actor component,
			// but also at the time of writing, AChaosVDSolverInfoActor was no a public include
			const AActor* SolverInfoActor = reinterpret_cast<const AActor*>(It.Value);
			if (SolverInfoActor)
			{
				if (UJoltMoverCVDSimDataComponent* SolverSimDataComponent = SolverInfoActor->FindComponentByClass<UJoltMoverCVDSimDataComponent>())
				{
					SolverToSimDataComponentMap.Add(SolverID, SolverSimDataComponent);
				}
			}
		}
	}
}

TWeakObjectPtr<UJoltMoverCVDSimDataComponent>* FJoltMoverCVDTab::FindJoltMoverDataComponentForSolver(int32 SolverID)
{
	TWeakObjectPtr<UJoltMoverCVDSimDataComponent>* FoundSimDataComponent = SolverToSimDataComponentMap.Find(SolverID);
	if (!FoundSimDataComponent || FoundSimDataComponent->IsStale())
	{
		// Refresh SolverToSimDataComponentMap
		RetrieveAllSolversJoltMoverDataComponents();

		// Try again
		FoundSimDataComponent = SolverToSimDataComponentMap.Find(SolverID);
	}

	return FoundSimDataComponent;
}

void FJoltMoverCVDTab::DisplaySingleParticleInfo(int32 SelectedSolverID, int32 SelectedParticleID)
{
	// At the moment this is really written to support only one particle selected at a time
	// I we were to have multiple selection we would have to cache the whole list of particle IDs selected
	bool IsParticleBeingSelected = (SelectedParticleID != INDEX_NONE && SelectedSolverID != INDEX_NONE);
	bool WasDataFoundForParticle = false;
	if (IsParticleBeingSelected)
	{
		// Find the sim data component weak pointer for the selected solver ID
		TWeakObjectPtr<UJoltMoverCVDSimDataComponent>* FoundSimDataComponent = FindJoltMoverDataComponentForSolver(SelectedSolverID);
		if (FoundSimDataComponent)
		{
			// Pin a strong pointer to it so it doesn't get away while in use
			TStrongObjectPtr<UJoltMoverCVDSimDataComponent> SimDataComponentSharedPtr = FoundSimDataComponent->Pin();
			UJoltMoverCVDSimDataComponent* SimDataComponent = SimDataComponentSharedPtr.Get();
			if (IsValid(SimDataComponent))
			{
				if (SimDataComponent->FindAndUnwrapSimDataForParticle(SelectedParticleID, JoltMoverSimDataWrapper, JoltMoverSyncState, JoltMoverInputCmd, JoltMoverLocalSimData))
				{
					if (JoltMoverSimDataWrapper && JoltMoverInputCmd && JoltMoverSyncState)
					{
						if (DetailsPanelView)
						{
							MultiViewWrapper.Clear();

							// This displays the particle ID and solver ID
							MultiViewWrapper.AddData(JoltMoverSimDataWrapper.Get());

							// Input Command Context structs
							for (const TSharedPtr<FJoltMoverDataStructBase>& InputDataStructSharedPtr : JoltMoverInputCmd->Collection.GetDataArray())
							{
								if (FJoltMoverDataStructBase* JoltMoverDataStructBase = InputDataStructSharedPtr.Get())
								{
									MultiViewWrapper.AddData(MakeShared<FStructOnScope>(JoltMoverDataStructBase->GetScriptStruct(), reinterpret_cast<uint8*>(JoltMoverDataStructBase)));
								}
							}

							MultiViewWrapper.AddData(JoltMoverSyncState.Get());
							// Sync State Data Collection structs
							for (const TSharedPtr<FJoltMoverDataStructBase>& InputDataStructSharedPtr : JoltMoverSyncState->Collection.GetDataArray())
							{
								if (FJoltMoverDataStructBase* JoltMoverDataStructBase = InputDataStructSharedPtr.Get())
								{
									MultiViewWrapper.AddData(MakeShared<FStructOnScope>(JoltMoverDataStructBase->GetScriptStruct(), reinterpret_cast<uint8*>(JoltMoverDataStructBase)));
								}
							}

							// Local Simulation State structs
							if (JoltMoverLocalSimData)
							{
								for (const TSharedPtr<FJoltMoverDataStructBase>& LocalSimDataStructSharedPtr : JoltMoverLocalSimData->GetDataArray())
								{
									if (FJoltMoverDataStructBase* JoltMoverDataStructBase = LocalSimDataStructSharedPtr.Get())
									{
										MultiViewWrapper.AddData(MakeShared<FStructOnScope>(JoltMoverDataStructBase->GetScriptStruct(), reinterpret_cast<uint8*>(JoltMoverDataStructBase)));
									}
								}
							}

							WasDataFoundForParticle = true;

							SetStructToInspect(&MultiViewWrapper);
						}
					}
					// Cache the particle ID so we can display info for that same particle when we scrub to a different frame that also has that particle
					// WARNING: Apparently IDs can be reused across frames for different particles, so this might not be good enough
					CurrentlyDisplayedSolverID = SelectedSolverID;
					CurrentlyDisplayedParticleID = SelectedParticleID;
				}
			}
		}
	}
	
	if (!WasDataFoundForParticle)
	{
		CurrentlyDisplayedSolverID = INDEX_NONE;
		CurrentlyDisplayedParticleID = INDEX_NONE;
	}
}

void FJoltMoverCVDTab::DisplayJoltMoverInfoForSelectedElements(const TArray<FTypedElementHandle>& SelectedElementHandles)
{
	if (DetailsPanelView)
	{
		DetailsPanelView->SetSelectedStruct(nullptr);

		bool ShouldClearSimDataDetailsPanel = true;
		using namespace Chaos::VD::TypedElementDataUtil;
		for (int32 SelectionIndex = 0; SelectionIndex < SelectedElementHandles.Num(); ++SelectionIndex)
		{
			if (const FChaosVDSceneParticle* Particle = GetStructDataFromTypedElementHandle<FChaosVDSceneParticle>(SelectedElementHandles[SelectionIndex]))
			{
				int32 SelectedParticleID = Particle->GetParticleData()->ParticleIndex; // Replace with ID of selected particle if one is selected
				int32 SelectedSolverID = Particle->GetParticleData()->SolverID;
				ShouldClearSimDataDetailsPanel = false;
				DisplaySingleParticleInfo(SelectedSolverID, SelectedParticleID);

				// Right now, we only handle displaying info for the first particle in the selection, but later we might not break here and display info for all particles
				break;
			}
		}

		// Clear the details panel view if we didn't find particles in the selection
		if (ShouldClearSimDataDetailsPanel)
		{
			DetailsPanelView->SetSelectedStruct(nullptr);
		}
	}
}

void FJoltMoverCVDTab::HandleSceneUpdated()
{
	// This is called when we scrub the timelines for instance
	bool IsParticleSelected = (CurrentlyDisplayedSolverID != INDEX_NONE && CurrentlyDisplayedParticleID != INDEX_NONE);

	if (!IsParticleSelected)
	{
		return;
	}

	const TSharedPtr<FChaosVDScene> Scene = SceneWeakPtr.Pin();
	if (!Scene)
	{
		return;
	}

	bool ShouldClearSimDataDetailsPanel = true;
	using namespace Chaos::VD::TypedElementDataUtil;
	TArray<FTypedElementHandle> SelectedParticlesHandles = Scene->GetSelectedElementHandles();
	DisplayJoltMoverInfoForSelectedElements(SelectedParticlesHandles);
}

void FJoltMoverCVDTab::HandlePostSelectionChange(const UTypedElementSelectionSet* ChangedSelectionSet)
{
	// This is called when the selection changes
	TArray<FTypedElementHandle> SelectedParticlesHandles;
	ChangedSelectionSet->GetSelectedElementHandles(SelectedParticlesHandles, UChaosVDSelectionInterface::StaticClass());	
	DisplayJoltMoverInfoForSelectedElements(SelectedParticlesHandles);
}

#undef LOCTEXT_NAMESPACE

