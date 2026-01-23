// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Engine/EngineTypes.h"
#include "JoltNetworkPredictionBuffer.h"
#include "JoltNetworkPredictionConditionalState.h"
#include "JoltNetworkPredictionInstanceMap.h"
#include "JoltNetworkPredictionCues.h"
#include "JoltNetworkPredictionModelDef.h"
#include "JoltNetworkPredictionTickState.h"

// Enum that maps to internal NetworkPrediction services, see notes in NetworkPredictionServiceRegistry.h
enum class EJoltNetworkPredictionService : uint32
{
	None = 0,

	// Common services that fix/independent can share
	
	//MAX_COMMON				= ServerRPC,

	// Services exclusive to fix tick mode
	FixedServerRPC				= 1 << 0,
	FixedRollback				= 1 << 1,
	FixedPhysicsRollback		= 1 << 2,
	FixedExtrapolate			= 1 << 3,	// TODO
	FixedInterpolate			= 1 << 4,
	FixedInputLocal				= 1 << 5,
	FixedInputRemote			= 1 << 6,
	FixedTick					= 1 << 7,
    FixedPhysics			    = 1 << 8,
	FixedSmoothing				= 1 << 9,
	FixedFinalize				= 1 << 10,
	MAX_FIXED					= FixedFinalize,

	// Services exclusive to independent tick mode
	ServerRPC               =  1 << 11,
	IndependentRollback		= 1 << 12,
	IndependentPhysicsRollback		= 1 << 13,
	IndependentExtrapolate	= 1 << 14,	// TODO
	IndependentInterpolate	= 1 << 15,

	IndependentLocalInput	= 1 << 16,
	IndependentLocalTick	= 1 << 17,
	IndependentLocalPhysics	= 1 << 18,
	IndependentRemoteTick	= 1 << 19,
	IndependentRemotePhysics	= 1 << 20,
	
	IndependentSmoothingFinalize	= 1 << 21,	// TODO
	IndependentLocalFinalize		= 1 << 22,
	IndependentRemoteFinalize		= 1 << 23,
	MAX_INDEPENDENT					= IndependentRemoteFinalize,

	// Helper masks
	//ANY_COMMON = (MAX_COMMON<<1)-1,
	ANY_FIXED = ((MAX_FIXED<<1)-1),
	ANY_INDEPENDENT = (((MAX_INDEPENDENT<<1)-1) & ~ANY_FIXED),
};

ENUM_CLASS_FLAGS(EJoltNetworkPredictionService);

// Basic data that all instances have
template<typename ModelDef=FJoltNetworkPredictionModelDef>
struct TInstanceData
{
	TJoltNetworkPredictionModelInfo<ModelDef>		Info;
	
	ENetRole NetRole = ROLE_None;
	TUniqueObj<TJoltNetSimCueDispatcher<ModelDef>>	CueDispatcher;	// Should maybe be moved out?

	int32 TraceID;
	EJoltNetworkPredictionService ServiceMask = EJoltNetworkPredictionService::None;
};

// Frame data that instances with StateTypes will have.
template<typename ModelDef=FJoltNetworkPredictionModelDef>
struct TJoltInstanceFrameState
{
	using StateTypes = typename ModelDef::StateTypes;
	using InputType = typename StateTypes::InputType;
	using SyncType = typename StateTypes::SyncType;
	using AuxType = typename StateTypes::AuxType;

	struct FFrame
	{
		float InterpolationTimeMS = 0.f;
		TJoltConditionalState<InputType>	InputCmd;
		TJoltConditionalState<SyncType>	SyncState;
		TJoltConditionalState<AuxType>	AuxState;
	};

	TJoltNetworkPredictionBuffer<FFrame> Buffer;

	TJoltInstanceFrameState()
		: Buffer(64) { } // fixme
};

// Data the client receives from the server
template<typename ModelDef=FJoltNetworkPredictionModelDef>
struct TJoltClientRecvData
{
	using StateTypes = typename ModelDef::StateTypes;
	using InputType = typename StateTypes::InputType;
	using SyncType = typename StateTypes::SyncType;
	using AuxType = typename StateTypes::AuxType;

	int32 ServerFrame; // Fixed tick || Independent AP only
	int32 SimTimeMS; // Independent tick only
	
	TJoltConditionalState<InputType> InputCmd; // SP Only
	TJoltConditionalState<SyncType>	SyncState;
	TJoltConditionalState<AuxType>	AuxState;

	// Delta Serialization
	struct AckedFrame
	{
		TJoltConditionalState<InputType> InputCmd; // SP Only
		TJoltConditionalState<SyncType>	SyncState;
		TJoltConditionalState<AuxType>	AuxState;
	};
	
	TMap<int32, typename TJoltInstanceFrameState<ModelDef>::FFrame> AckedFrames;
	// Acceleration data.
	int32 ID = INDEX_NONE;
	int32 TraceID = INDEX_NONE;
	int32 InstanceIdx = INDEX_NONE;	// Index into TJoltModelDataStore::Instances
	int32 FramesIdx = INDEX_NONE;	// Index into TJoltModelDataStore::Frames
	ENetRole NetRole = ROLE_None;
};

// Data the server receives from a fixed ticking AP client
template<typename ModelDef=FJoltNetworkPredictionModelDef>
struct TJoltServerRecvData_Fixed
{
	using StateTypes = typename ModelDef::StateTypes;
	using InputType = typename StateTypes::InputType;

	TJoltNetworkPredictionBuffer<TPair<double,TJoltConditionalState<InputType>>> InputBuffer;

	// Note that these are client frame numbers, they do not match the servers local PendingFrame
	int32 LastConsumedFrame = INDEX_NONE;
	int32 LastRecvFrame = INDEX_NONE;

	int32 InputFault = 0;
	int32 TraceID = INDEX_NONE;
	//Added By Kai , Delta Serialization Support
	int32 ID = INDEX_NONE;

	TJoltServerRecvData_Fixed()
		: InputBuffer(32) {} // fixme
};

// Data the server receives from an independent ticking AP client
template<typename ModelDef=FJoltNetworkPredictionModelDef>
struct TJoltServerRecvData_Independent
{
	using StateTypes = typename ModelDef::StateTypes;
	using InputType = typename StateTypes::InputType;

	struct FFrame
	{
		TJoltConditionalState<InputType>	InputCmd;
		int32	DeltaTimeMS;
	};

	TJoltServerRecvData_Independent()
		: InputBuffer(16) { }

	int32 PendingFrame = 0;
	int32 TotalSimTimeMS = 0;
	float UnspentTimeMS = 0.f;

	int32 LastConsumedFrame = INDEX_NONE;
	int32 LastRecvFrame = INDEX_NONE;

	TJoltNetworkPredictionBuffer<FFrame> InputBuffer;

	// Acceleration data.
	int32 TraceID = INDEX_NONE;
	int32 InstanceIdx = INDEX_NONE;	// Index into TJoltModelDataStore::Instances
	int32 FramesIdx = INDEX_NONE;	// Index into TJoltModelDataStore::Frames
};

// Stores all public data for a given model def
template<typename ModelDef=FJoltNetworkPredictionModelDef>
struct TJoltModelDataStore
{
	TJoltStableInstanceMap<TInstanceData<ModelDef>>	Instances;

	TJoltInstanceMap<TJoltInstanceFrameState<ModelDef>> Frames;
	
	TJoltInstanceMap<TJoltClientRecvData<ModelDef>> ClientRecv;
	TBitArray<> ClientRecvBitMask;

	TJoltInstanceMap<TJoltServerRecvData_Fixed<ModelDef>> ServerRecv;

	TJoltInstanceMap<TJoltServerRecvData_Independent<ModelDef>> ServerRecv_IndependentTick;

	TJoltInstanceMap<FDelegateHandle> DeferredRegisterHandle;
};
