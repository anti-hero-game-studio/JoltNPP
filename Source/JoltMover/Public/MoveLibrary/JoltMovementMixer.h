// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JoltMovementMixer.generated.h"

#define UE_API JOLTMOVER_API

struct FJoltLayeredMoveBase;
struct FJoltLayeredMoveInstance;
struct FJoltProposedMove;

/**
 * Class in charge of Mixing various moves when evaluating/combining moves. The mixer used can be set on the MoverComponent itself.
 */
UCLASS(MinimalAPI, BlueprintType)
class UJoltMovementMixer : public UObject
{
	GENERATED_BODY()

public:
	UE_API UJoltMovementMixer();
	
	/** In charge of mixing Layered Move proposed moves into a cumulative proposed move based on mix mode and priority.*/
	UE_API virtual void MixLayeredMove(const FJoltLayeredMoveBase& ActiveMove, const FJoltProposedMove& MoveStep, FJoltProposedMove& OutCumulativeMove);

	/** In charge of mixing Layered Move proposed moves into a cumulative proposed move based on mix mode and priority.*/
	UE_API virtual void MixLayeredMove(const FJoltLayeredMoveInstance& ActiveMove, const FJoltProposedMove& MoveStep, FJoltProposedMove& OutCumulativeMove);

	/** In charge of mixing proposed moves together. Is similar to MixLayeredMove but is only responsible for mixing proposed moves instead of layered moves. */
	UE_API virtual void MixProposedMoves(const FJoltProposedMove& MoveToMix, FVector UpDirection, FJoltProposedMove& OutCumulativeMove);

	/** Resets all state used for mixing. Should be called before or after finished mixing moves. */
	UE_API virtual void ResetMixerState();
	
protected:
	// Stores the current highest priority we've hit during this round of mixing. Will get reset. Note: Currently only used for mixing layered moves
	uint8 CurrentHighestPriority;

	// Earliest start time of the layered move with highest priority. Used to help break ties of moves with same priority. Note: Currently only used for mixing layered moves
	double CurrentLayeredMoveStartTime;
	
	/**
	 * Helper function for layered move mixing to check priority and start time if priority is the same.
	 * Returns true if this layered move should take priority given current HighestPriority and CurrentLayeredMoveStartTimeMs
	 */
	static UE_API bool CheckPriority(const FJoltLayeredMoveBase* LayeredMove, uint8& InOutHighestPriority, double& InOutCurrentLayeredMoveStartTimeMs);
	static UE_API bool CheckPriority(uint8 LayeredMovePriority, double LayeredMoveStartTimeMs, uint8& InOutHighestPriority, double& InOutCurrentLayeredMoveStartTimeMs);
};

#undef UE_API
