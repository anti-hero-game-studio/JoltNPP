// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "JoltNetworkPredictionStateTypes.h"

class UJoltNetworkPredictionPlayerControllerComponent;
struct FJoltNetworkPredictionStateView;

// Arbitrary sort values used by system default definitions
enum class EJoltNetworkPredictionSortPriority : uint8
{
	First = 1,
	PreKinematicMovers = 50,
	KinematicMovers = 75,
	PostKinematicMovers = 100,
	Physics = 125,	// Note this not where physics itself *ticks*! Just a priority value for physics having definitions to be sorted in the various services.
	Last = 250
};

using FModelDefId = int32;
#define JNP_MODEL_BODY() static FModelDefId ID

struct FJoltNetworkPredictionModelDef
{
	// Actual defs should have:
	// JNP_MODEL_BODY(); 

	// TJoltNetworkPredictionStateTypes: User State Types (Input, Sync, Aux)
	// Enables: Reconcile, Ticking, Input, Finalize
	using StateTypes = TJoltNetworkPredictionStateTypes<void,void,void>;

	// Object that runs SimulationTick
	// Requires: valid StateTypes
	// Enables: Ticking
	using Simulation = void;

	// Object class that can take output from prediction system. E.g AActor, AMyPawn.
	// See notes in NetworkPredictionDriver.h
	// Requires: StateTypes || PhysicsState
	// Enables: Finalize, Cues
	using Driver = void;

	// Physics state. Void = no physics, FJoltNetworkPredictionPhysicsState is the default physics state that synchronizes X,R,V,W.
	// Enables: Reconcile, Finalize
	using PhysicsState = void;

	static const TCHAR* GetName() { return nullptr; }

	static constexpr int32 GetSortPriority() { return (int32)EJoltNetworkPredictionSortPriority::Last; }
};

// ----------------------------------------------------------------------

template<typename ModelDef=FJoltNetworkPredictionModelDef, typename SimulationType=typename ModelDef::Simulation>
struct FJoltConditionalSimulationPtr
{
	FJoltConditionalSimulationPtr(SimulationType* Sim=nullptr) : Simulation(Sim) { }
	SimulationType* operator->() const { return Simulation; }

private:
	SimulationType* Simulation = nullptr;
};

template<typename ModelDef>
struct FJoltConditionalSimulationPtr<ModelDef, void>
{
	FJoltConditionalSimulationPtr(void* v=nullptr) { }
};

// ----------------------------------------------------------------------

template<typename ModelDef=FJoltNetworkPredictionModelDef>
struct TJoltNetworkPredictionModelInfo
{
	using SimulationType = typename ModelDef::Simulation;
	using DriverType = typename ModelDef::Driver;
	using PhysicsState = typename ModelDef::PhysicsState;

	FJoltConditionalSimulationPtr<ModelDef> Simulation;			// Object that ticks this instance
	DriverType* Driver = nullptr;							// Object that handles input/out
	struct FJoltNetworkPredictionStateView* View = nullptr; // Game side view of state to update
	UJoltNetworkPredictionPlayerControllerComponent* RPCHandler = nullptr; // RPC Handler, an Actor component
	//responsible for dealing with all simulations RPC

	TJoltNetworkPredictionModelInfo(SimulationType* InSimulation=nullptr, DriverType* InDriver=nullptr, FJoltNetworkPredictionStateView* InView=nullptr)
		: Simulation(InSimulation), Driver(InDriver), View(InView) { }
};
