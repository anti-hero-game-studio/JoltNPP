// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "JoltMoverSimulation.generated.h"

#define UE_API JOLTMOVER_API

class UJoltMoverBlackboard;
struct FJoltMoverSyncState;
class UJoltRollbackBlackboard_InternalWrapper;

/**
* WIP Base class for a Mover simulation.
* The simulation is intended to be the thing that updates the Mover
* state and should be safe to run on an async thread
*/
UCLASS(MinimalAPI, BlueprintType)
class UJoltMoverSimulation : public UObject
{
	GENERATED_BODY()

public:
	UE_API UJoltMoverSimulation();

	// Warning: the regular blackboard will be fully replaced by the rollback blackboard in the future
	UFUNCTION(BlueprintPure, Category = Mover)
	UE_API const UJoltMoverBlackboard* GetBlackboard() const;

	// Warning: the regular blackboard will be fully replaced by the rollback blackboard in the future
	UFUNCTION(BlueprintPure, Category = Mover)
	UE_API UJoltMoverBlackboard* GetBlackboard_Mutable();

	UFUNCTION(BlueprintPure, Category = Mover)
	UE_API const UJoltRollbackBlackboard_InternalWrapper* GetRollbackBlackboard() const;

	UFUNCTION(BlueprintPure, Category = Mover)
	UE_API UJoltRollbackBlackboard_InternalWrapper* GetRollbackBlackboard_Mutable();


	/**
	* Attempt to teleport to TargetTransform. The teleport is not guaranteed to happen. This function is meant to be called by an instant movement effect as part of its effect application.
	* If it succeeds a FJoltTeleportSucceededEventData will be emitted, if it fails a FJoltTeleportFailedEventData will be sent.
	* @param TimeStep The time step of the current step or substep being simulated. This will come from the ApplyMovementEffect function.
	* @param TargetTransform The transform to teleport to. In the case bUseActorRotation is true, the rotation of this transform will be ignored.
	* @param bUseActorRotation If true, the rotation will not be modified upon teleportation. If false, the rotation in TargetTransform will be used to orient the teleported.
	* @param OutputState This is the sync state that me modified as a result of the application of this effect. Like TimeStep, this should come from the ApplyMovementEffect function.
	*/
	UFUNCTION(BlueprintCallable, Category = Mover)
	virtual void AttemptTeleport(const FJoltMoverTimeStep& TimeStep, const FTransform& TargetTransform, bool bUseActorRotation, FJoltMoverSyncState& OutputState) {}


	// Used during initialization only
	UE_API void SetRollbackBlackboard(UJoltRollbackBlackboard_InternalWrapper* RollbackSimBlackboard);

protected:
	UPROPERTY(Transient)
	TObjectPtr<UJoltMoverBlackboard> Blackboard = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UJoltRollbackBlackboard_InternalWrapper> RollbackBlackboard = nullptr;

};

#undef UE_API
