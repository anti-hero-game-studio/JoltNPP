// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ChaosVDSolverDataComponent.h"
#include "JoltMoverCVDSimDataComponent.generated.h"

struct FJoltMoverSyncState;
struct FJoltMoverCVDSimDataWrapper;
struct FJoltMoverInputCmdContext;
struct FJoltMoverDataCollection;

struct FDeserializedJoltMoverStates
{
	TSharedPtr<FJoltMoverSyncState> SyncState;
	TSharedPtr<FJoltMoverInputCmdContext> InputCommand;
	TSharedPtr<FJoltMoverDataCollection> LocalSimData;
};

/** Component holding JoltMover data for the current visualized frame */
UCLASS()
class UJoltMoverCVDSimDataComponent : public UChaosVDSolverDataComponent
{
	GENERATED_BODY()

public:
	// That we chose to implement this function and not UpdateFromNewGameFrameData or UpdateFromNewSolverStageData is tied
	// to the implementation of FJoltMoverCVDSimDataProcessor, which currently add ths information to FChaosVDTraceProvider::GetCurrentSolverFrame()
	// Eventually we will record information at different stages of a solver frame and will be using UpdateFromNewSolverStageData instead,
	// to show the state of the sync state at the beginning of the frame, then at the end
	virtual void UpdateFromSolverFrameData(const FChaosVDSolverFrameData& InSolverFrameData) override;

	virtual void ClearData() override;
	
	TConstArrayView<TSharedPtr<FJoltMoverCVDSimDataWrapper>> GetFrameSimDataArray() const
	{
		return FrameSimDataArray;
	}

	bool FindAndUnwrapSimDataForParticle(
		uint32 ParticleID,
		TSharedPtr<FJoltMoverCVDSimDataWrapper>& OutSimDataWrapper,
		TSharedPtr<FJoltMoverSyncState>& OutSyncState,
		TSharedPtr<FJoltMoverInputCmdContext>& OutInputCmd,
		TSharedPtr<FJoltMoverDataCollection>& OutLocalSimData);

private:
	// This is the array of FJoltMoverCVDSimDataWrapper for the current frame (as it is updated in UpdateFromNewGameFrameData)
	// and corresponding to this DataComponent's SolverID
	TArray<TSharedPtr<FJoltMoverCVDSimDataWrapper>> FrameSimDataArray;

	TMap<FJoltMoverCVDSimDataWrapper*, TSharedPtr<FDeserializedJoltMoverStates>> DeserializedStates;
};
