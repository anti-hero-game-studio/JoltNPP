// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "HAL/Platform.h"

// Enum to identify the state types
enum class EJoltNetworkPredictionStateType : uint8
{
	Input,
	Sync,
	Aux
};

inline const TCHAR* LexToString(EJoltNetworkPredictionStateType A)
{
	switch(A)
	{
		case EJoltNetworkPredictionStateType::Input: return TEXT("Input");
		case EJoltNetworkPredictionStateType::Sync: return TEXT("Sync");
		case EJoltNetworkPredictionStateType::Aux: return TEXT("Aux");
	};
	return TEXT("Unknown");
}

// State type defines
template<typename InInputCmd=void, typename InSyncState=void, typename InAuxState=void>
struct TJoltNetworkPredictionStateTypes
{
	using InputType = InInputCmd;
	using SyncType = InSyncState;
	using AuxType = InAuxState;
};

// Tuple of state types
template<typename StateType=TJoltNetworkPredictionStateTypes<>>
struct TJoltNetworkPredictionState
{
	using InputType = typename StateType::InputType;
	using SyncType = typename StateType::SyncType;
	using AuxType = typename StateType::AuxType;

	const InputType* Cmd;
	const SyncType* Sync;
	const AuxType* Aux;

	TJoltNetworkPredictionState(const InputType* InInputCmd, const SyncType* InSync, const AuxType* InAux)
		: Cmd(InInputCmd), Sync(InSync), Aux(InAux) { }	

	// Allows implicit downcasting to a parent simulation's types
	template<typename T>
	TJoltNetworkPredictionState(const TJoltNetworkPredictionState<T>& Other)
		: Cmd(Other.Cmd), Sync(Other.Sync), Aux(Other.Aux) { }
};

// Just the Sync/Aux pair
template<typename StateType=TJoltNetworkPredictionStateTypes<>>
struct TJoltSyncAuxPair
{
	using SyncType = typename StateType::SyncType;
	using AuxType = typename StateType::AuxType;

	const SyncType* Sync;
	const AuxType* Aux;

	TJoltSyncAuxPair(const SyncType* InSync, const AuxType* InAux)
		: Sync(InSync), Aux(InAux) { }	

	// Allows implicit downcasting to a parent simulation's types
	template<typename T>
	TJoltSyncAuxPair(const TJoltSyncAuxPair<T>& Other)
		: Sync(Other.Sync), Aux(Other.Aux) { }
};
