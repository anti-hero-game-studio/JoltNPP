// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "JoltNetworkPredictionStateTypes.h"

template <typename ElementType> struct TJoltNetSimLazyWriter;

struct FJoltNetSimCueDispatcher;

// Input state is just a collection of references to the simulation state types
template<typename StateTypes=TJoltNetworkPredictionStateTypes<>>
using TJoltNetSimInput = TJoltNetworkPredictionState<StateTypes>;

// Output state: the output SyncState (always created) and TJoltNetSimLazyWriter for the AuxState (created on demand since every tick does not generate a new aux frame)
template<typename StateType=TJoltNetworkPredictionStateTypes<>>
struct TJoltNetSimOutput
{
	using InputType = typename StateType::InputType;
	using SyncType = typename StateType::SyncType;
	using AuxType = typename StateType::AuxType;

	SyncType* Sync;
	const TJoltNetSimLazyWriter<AuxType>& Aux;
	FJoltNetSimCueDispatcher& CueDispatch;

	TJoltNetSimOutput(SyncType* InSync, const TJoltNetSimLazyWriter<AuxType>& InAux, FJoltNetSimCueDispatcher& InCueDispatch)
		: Sync(InSync), Aux(InAux), CueDispatch(InCueDispatch) { }
};

