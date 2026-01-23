// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/TransactionallySafeRWLock.h"
#include "Templates/SubclassOf.h"
#include "JoltMovementMode.h"
#include "JoltInstantMovementEffect.h"
#include "JoltMovementModeStateMachine.generated.h"

#define UE_API JOLTMOVER_API

struct FJoltProposedMove;
class UJoltImmediateMovementModeTransition;
class UJoltMovementModeTransition;

/**
 * - Any movement modes registered are co-owned by the state machine
 * - There is always an active mode, falling back to a do-nothing 'null' mode
 * - Queuing a mode that is already active will cause it to exit and re-enter
 * - Modes only switch during simulation tick
 */
 UCLASS(MinimalAPI)
class UJoltMovementModeStateMachine : public UObject
{
	 GENERATED_UCLASS_BODY()

public:
	UE_API void RegisterMovementMode(FName ModeName, TObjectPtr<UJoltBaseMovementMode> Mode, bool bIsDefaultMode=false);
	UE_API void RegisterMovementMode(FName ModeName, TSubclassOf<UJoltBaseMovementMode> ModeType, bool bIsDefaultMode=false);

	UE_API void UnregisterMovementMode(FName ModeName);
	UE_API void ClearAllMovementModes();

	UE_API void RegisterGlobalTransition(TObjectPtr<UJoltBaseMovementModeTransition> Transition);
	UE_API void UnregisterGlobalTransition(TObjectPtr<UJoltBaseMovementModeTransition> Transition);
	UE_API void ClearAllGlobalTransitions();

	UE_API void SetDefaultMode(FName NewDefaultModeName);

	UE_API void QueueNextMode(FName DesiredNextModeName, bool bShouldReenter=false);
	UE_API void SetModeImmediately(FName DesiredModeName, bool bShouldReenter=false);
	UE_API void ClearQueuedMode();

	UE_API void OnSimulationTick(USceneComponent* UpdatedComponent, UPrimitiveComponent* UpdatedPrimitive, UJoltMoverBlackboard* SimBlackboard, const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, FJoltMoverTickEndData& OutputState);
 	UE_API void OnSimulationPreRollback(const FJoltMoverSyncState* InvalidSyncState, const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* InvalidAuxState, const FJoltMoverAuxStateContext* AuxState, const FJoltMoverTimeStep& NewBaseTimeStep);
	UE_API void OnSimulationRollback(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState, const FJoltMoverTimeStep& NewBaseTimeStep);

	FName GetCurrentModeName() const { return CurrentModeName; }

	UE_API const UJoltBaseMovementMode* GetCurrentMode() const;

	UE_API const UJoltBaseMovementMode* FindMovementMode(FName ModeName) const;

	UE_API void QueueLayeredMove(TSharedPtr<FJoltLayeredMoveBase> Move);
	
	UE_API void QueueActiveLayeredMove(const TSharedPtr<FJoltLayeredMoveInstance>& LayeredMove);

 	UE_API FJoltMovementModifierHandle QueueMovementModifier(TSharedPtr<FJoltMovementModifierBase> Modifier);

 	UE_API void CancelModifierFromHandle(FJoltMovementModifierHandle ModifierHandle);

 	UE_API const FJoltMovementModifierBase* FindQueuedModifier(FJoltMovementModifierHandle ModifierHandle) const;
 	UE_API const FJoltMovementModifierBase* FindQueuedModifierByType(const UScriptStruct* ModifierType) const;

	UE_API void CancelFeaturesWithTag(FGameplayTag TagToCancel, bool bRequireExactMatch=false);

	// This function is meant to be used only in async mode on the physics thread, not on the game thread
	void QueueInstantMovementEffect_Internal(const FJoltScheduledInstantMovementEffect& ScheduledInstantMovementEffect);
protected:
	UE_API void QueueInstantMovementEffect(const FJoltScheduledInstantMovementEffect& ScheduledInstantMovementEffect);
	UE_API void QueueInstantMovementEffects(const TArray<FJoltScheduledInstantMovementEffect>& ScheduledInstantMovementEffects);

	void ProcessEvents(const TArray<TSharedPtr<FJoltMoverSimulationEventData>>& InEvents);
	UE_API virtual void ProcessSimulationEvent(const FJoltMoverSimulationEventData& EventData);

	UE_API virtual void PostInitProperties() override;

	UPROPERTY()
	TMap<FName, TObjectPtr<UJoltBaseMovementMode>> Modes;
	TArray<TObjectPtr<UJoltBaseMovementModeTransition>> GlobalTransitions;

	UPROPERTY(Transient)
	TObjectPtr<UJoltImmediateMovementModeTransition> QueuedModeTransition;

	FName DefaultModeName = NAME_None;
	FName CurrentModeName = NAME_None;

	// Represents the current sim time that's passed, and the next frame number that's next to be simulated.
	FJoltMoverTimeStep CurrentBaseTimeStep;

	/** Moves that are queued to be added to the simulation at the start of the next sim subtick. Access covered by lock. */
	TArray<TSharedPtr<FJoltLayeredMoveBase>> QueuedLayeredMoves;

	/** Moves that are queued to be added to the simulation at the start of the next sim subtick. Access covered by lock. */
 	TArray<TSharedPtr<FJoltLayeredMoveInstance>> QueuedLayeredMoveInstances;
 	
 	/** Effects that are queued to be applied to the simulation at the start of the next sim subtick or at the end of this tick.  Access covered by lock. */
 	TArray<FJoltScheduledInstantMovementEffect> QueuedInstantEffects;

 	/** Modifiers that are queued to be added to the simulation at the start of the next sim subtick. Access covered by lock. */
 	TArray<TSharedPtr<FJoltMovementModifierBase>> QueuedMovementModifiers;

 	/** Modifiers that are to be canceled at the start of the next sim subtick.  Access covered by lock. */
 	TArray<FJoltMovementModifierHandle> ModifiersToCancel;
 	
	/** Tags that are used to cancel any matching movement features (modifiers, layered moves, etc). Access covered by lock. */
	TArray<TPair<FGameplayTag, bool>> TagCancellationRequests;

	// Internal-use-only tick data structs, for efficiency since they typically have the same contents from frame to frame
	FJoltMoverTickStartData WorkingSubstepStartData;
	FJoltSimulationTickParams WorkingSimTickParams;

private:
	// Locks for thread safety on queueing mechanisms
	mutable FTransactionallySafeRWLock LayeredMoveQueueLock;
	mutable FTransactionallySafeRWLock InstantEffectsQueueLock;
	mutable FTransactionallySafeRWLock ModifiersQueueLock;
	mutable FTransactionallySafeRWLock ModifierCancelQueueLock;
	mutable FTransactionallySafeRWLock TagCancellationRequestsLock;

	UE_API void ConstructDefaultModes();
	UE_API void AdvanceToNextMode();
	UE_API void FlushQueuedMovesToGroup(FJoltLayeredMoveGroup& Group);
	// Flushes queued ActiveLayeredMoves to FJoltLayeredMoveInstanceGroup for this frame
 	UE_API void ActivateQueuedMoves(FJoltLayeredMoveInstanceGroup& Group);
 	UE_API void FlushQueuedModifiersToGroup(FJoltMovementModifierGroup& ModifierGroup);
 	UE_API void FlushModifierCancellationsToGroup(FJoltMovementModifierGroup& ActiveModifierGroup);
	UE_API void FlushTagCancellationsToSyncState(FJoltMoverSyncState& SyncState);
 	UE_API void RollbackModifiers(const FJoltMoverSyncState* InvalidSyncState, const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* InvalidAuxState, const FJoltMoverAuxStateContext* AuxState);
	UE_API bool HasAnyInstantEffectsQueued() const;
 	UE_API bool ApplyInstantEffects(FJoltApplyMovementEffectParams& ApplyEffectParams, FJoltMoverSyncState& OutputState);
	UE_API AActor* GetOwnerActor() const;
};

#undef UE_API
