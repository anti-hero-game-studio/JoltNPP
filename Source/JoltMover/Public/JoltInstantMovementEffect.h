// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MoveLibrary/JoltMovementUtilsTypes.h"

#include "JoltInstantMovementEffect.generated.h"

#define UE_API JOLTMOVER_API

class UJoltMoverComponent;
class UJoltMoverSimulation;
struct FJoltMoverTimeStep;
struct FJoltMoverTickStartData;
struct FJoltMoverSyncState;
struct FJoltMoverSimulationEventData;

struct FJoltApplyMovementEffectParams
{
	USceneComponent* UpdatedComponent;

	UPrimitiveComponent* UpdatedPrimitive;

	const UJoltMoverComponent* MoverComp;

	const FJoltMoverTickStartData* StartState;

	const FJoltMoverTimeStep* TimeStep;

	TArray<TSharedPtr<FJoltMoverSimulationEventData>> OutputEvents;
};

/** 
 * Async safe parameters passed to ApplyMovementEffect_Async. 
 * It is almost certainly missing the Physics Object handle and other things, this is just a first pass
 */
struct FJoltApplyMovementEffectParams_Async
{
	UJoltMoverSimulation* Simulation;
	const FJoltMoverTickStartData* StartState;
	const FJoltMoverTimeStep* TimeStep;
};

/**
 * Instant Movement Effects are methods of affecting movement state directly on a Mover-based actor for one tick.
 * Note: This is only applied one tick and then removed
 * Common uses would be for Teleporting, Changing Movement Modes directly, one time force application, etc.
 * Multiple Instant Movement Effects can be active at the time
 */
USTRUCT(BlueprintInternalUseOnly)
struct FJoltInstantMovementEffect
{
	GENERATED_BODY()

	FJoltInstantMovementEffect() { }

	virtual ~FJoltInstantMovementEffect() { }
	
	// @return newly allocated copy of this FJoltInstantMovementEffect. Must be overridden by child classes
	UE_API virtual FJoltInstantMovementEffect* Clone() const;

	UE_API virtual void NetSerialize(FArchive& Ar);

	UE_API virtual UScriptStruct* GetScriptStruct() const;

	UE_API virtual FString ToSimpleString() const;

	virtual void AddReferencedObjects(class FReferenceCollector& Collector) {}

	virtual bool ApplyMovementEffect(FJoltApplyMovementEffectParams& ApplyEffectParams, FJoltMoverSyncState& OutputState) { return false; }
	virtual bool ApplyMovementEffect_Async(FJoltApplyMovementEffectParams_Async& ApplyEffectParams, FJoltMoverSyncState& OutputState) { return false; }
};

template<>
struct TStructOpsTypeTraits< FJoltInstantMovementEffect > : public TStructOpsTypeTraitsBase2< FJoltInstantMovementEffect >
{
	enum
	{
		//WithNetSerializer = true,
		WithCopy = true
	};
};

#undef UE_API
