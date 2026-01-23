// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltMoverCVDSimDataComponent.h"

#include "ChaosVDRecording.h"
#include "ChaosVDScene.h"
#include "JoltMoverCVDDataWrappers.h"
#include "Actors/ChaosVDDataContainerBaseActor.h"
#include "JoltMoverCVDTab.h"
#include "JoltMoverSimulationTypes.h"
#include "ChaosVisualDebugger/JoltMoverCVDRuntimeTrace.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMoverCVDSimDataComponent)

void UJoltMoverCVDSimDataComponent::UpdateFromSolverFrameData(const FChaosVDSolverFrameData& InSolverFrameData)
{
	Super::UpdateFromSolverFrameData(InSolverFrameData);

	if (TSharedPtr<FJoltMoverCVDSimDataContainer> SimDataContainer = InSolverFrameData.GetCustomData().GetData<FJoltMoverCVDSimDataContainer>())
	{
		if (const TArray<TSharedPtr<FJoltMoverCVDSimDataWrapper>>* RecordedData = SimDataContainer->SimDataBySolverID.Find(SolverID))
		{
			// Load the recorded data into the Physics JoltMover CVD component
			FrameSimDataArray.Reset(RecordedData->Num());
			FrameSimDataArray = *RecordedData;

			// Also, clear all cached deserialized data, we're starting from scratch at a new frame
			DeserializedStates.Empty();
		}
	}
}

void UJoltMoverCVDSimDataComponent::ClearData()
{
	FrameSimDataArray.Reset();
}

bool UJoltMoverCVDSimDataComponent::FindAndUnwrapSimDataForParticle(uint32 ParticleID, TSharedPtr<FJoltMoverCVDSimDataWrapper>& OutSimDataWrapper, TSharedPtr<FJoltMoverSyncState>& OutSyncState, TSharedPtr<FJoltMoverInputCmdContext>& OutInputCmd, TSharedPtr<FJoltMoverDataCollection>& OutLocalSimData)
{
	// Look for a sim data corresponding to ParticleID
	TSharedPtr<FJoltMoverCVDSimDataWrapper>* FoundSimData = FrameSimDataArray.FindByPredicate
		(
			[&](const TSharedPtr<FJoltMoverCVDSimDataWrapper>& SimData)
			{
				return (SimData->HasValidData() && (SimData->ParticleID == ParticleID));
			}
		);

	if (!FoundSimData || !*FoundSimData)
	{
		return false;
	}

	// We use the data wrapper pointer as key in the arrays of deserialized structs (input command, sync state)
	FJoltMoverCVDSimDataWrapper* SimDataPtr = (*FoundSimData).Get();
	OutSimDataWrapper = *FoundSimData;

	// Did we previously deserialize?
	TSharedPtr<FDeserializedJoltMoverStates> DeserializedJoltMoverStates = DeserializedStates.FindOrAdd(SimDataPtr);
	if (!DeserializedJoltMoverStates)
	{
		DeserializedJoltMoverStates = MakeShared<FDeserializedJoltMoverStates>();
		// This means that the SimData wasn't deserialized yet, so we do it now
		// Otherwise we use the cached version
		UE::JoltMoverUtils::FJoltMoverCVDRuntimeTrace::UnwrapSimData(*SimDataPtr, DeserializedJoltMoverStates->InputCommand, DeserializedJoltMoverStates->SyncState, DeserializedJoltMoverStates->LocalSimData);
	}
	if (DeserializedJoltMoverStates)
	{
		OutSyncState = DeserializedJoltMoverStates->SyncState;
		OutInputCmd = DeserializedJoltMoverStates->InputCommand;
		OutLocalSimData = DeserializedJoltMoverStates->LocalSimData;
	}

	return true;
}
