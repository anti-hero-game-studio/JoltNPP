// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "JoltNetworkPredictionModelDef.h"
#include "JoltNetworkPredictionDriver.h"

struct FJoltGenericKinematicActorSyncState
{
	FVector_NetQuantize100	Location;
	FQuat Rotation;
};

// Generic def for kinematic (non physics) actor that doesn't have a backing simulation. This is quite limited in what it can do,
// but hopefully useful is that they can still be recorded and restored
struct FJoltGenericKinematicActorDef : FJoltNetworkPredictionModelDef
{
	JNP_MODEL_BODY();

	using StateTypes = TJoltNetworkPredictionStateTypes<void, FJoltGenericKinematicActorSyncState, void>;
	using Driver = AActor;
	static const TCHAR* GetName() { return TEXT("Generic Kinematic Actor"); }
	static constexpr int32 GetSortPriority() { return (int32)EJoltNetworkPredictionSortPriority::PreKinematicMovers; }
};

template<>
struct FJoltNetworkPredictionDriver<FJoltGenericKinematicActorDef> : FJoltNetworkPredictionDriverBase<FJoltGenericKinematicActorDef>
{
	static void InitializeSimulationState(AActor* ActorDriver, FJoltGenericKinematicActorSyncState* Sync, void* Aux)
	{
		const FTransform& Transform = ActorDriver->GetActorTransform();
		Sync->Location = Transform.GetLocation();
		Sync->Rotation = Transform.GetRotation();
	}
};
