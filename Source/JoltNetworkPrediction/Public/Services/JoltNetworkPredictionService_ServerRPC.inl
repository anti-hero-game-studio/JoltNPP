// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JoltNetworkPredictionDriver.h"
#include "JoltNetworkPredictionPlayerControllerComponent.h"
#include "JoltNetworkPredictionSerialization.h"
#include "Services/JoltNetworkPredictionInstanceData.h"

// The ServerRPCService's job is to tell the Driver to call the Server RPC that sends InputCmds upstream.
// This happens every frame on autonomous proxy clients.
//
// The implementation here is very basic. It may make sense to have some scalability at this level,
// options for throttling send rate, etc.

class IJoltServerRPCService
{
public:

	virtual ~IJoltServerRPCService() = default;
	virtual void CallServerRPC(float DeltaTimeSeconds) = 0;
};

template<typename InModelDef>
class TJoltServerRPCService : public IJoltServerRPCService	
{
public:

	using ModelDef = InModelDef;
	using DriverType = typename ModelDef::Driver;

	TJoltServerRPCService(TJoltModelDataStore<ModelDef>* InDataStore)
		: DataStore(InDataStore) { }

	void RegisterInstance(FJoltNetworkPredictionID ID)
	{
		TInstanceData<ModelDef>* InstanceData = DataStore->Instances.Find(ID);
		jnpCheckSlow(InstanceData);

		Instances.Add((int32)ID, FInstance{InstanceData->TraceID, InstanceData->Info.Driver});
	}

	void UnregisterInstance(FJoltNetworkPredictionID ID)
	{
		Instances.Remove((int32)ID);
	}

	void CallServerRPC(float DeltaTimeSeconds) final override
	{
		for (auto& MapIt : Instances)
		{
			FInstance& Instance = MapIt.Value;
			FJoltNetworkPredictionDriver<ModelDef>::CallServerRPC(Instance.Driver);
		}
	}

private:

	// The vast majority of the time there will be <= 1 instances that wants to call the ServerRPC.
	// Only split screen type situations will require more.
	struct FInstance
	{
		int32 TraceID;
		DriverType* Driver;
	};

	TSortedMap<int32, FInstance, TInlineAllocator<1>> Instances;
	TJoltModelDataStore<ModelDef>* DataStore;
};


class IJoltFixedServerRPCService
{
public:

	virtual ~IJoltFixedServerRPCService() = default;
	virtual void CallServerRPC(float DeltaTimeSeconds) = 0;
	virtual void AddInputToHandler(const int32& Frame) = 0;
};

template<typename InModelDef>
class TJoltFixedServerRPCService : public IJoltFixedServerRPCService	
{
public:

	using ModelDef = InModelDef;
	using DriverType = typename ModelDef::Driver;
	using StateTypes = typename ModelDef::StateTypes;
	using InputType = typename StateTypes::InputType;

	TJoltFixedServerRPCService(TJoltModelDataStore<ModelDef>* InDataStore)
		: DataStore(InDataStore) { }

	void RegisterInstance(FJoltNetworkPredictionID ID)
	{
		TInstanceData<ModelDef>* InstanceData = DataStore->Instances.Find(ID);
		jnpCheckSlow(InstanceData);
		int32 FramesID = DataStore->Frames.GetIndex(ID);
		const int32 InstanceIndex = DataStore->Instances.GetIndexChecked(ID);

		Instances.Add((int32)ID, FInstance{InstanceData->TraceID, FramesID,InstanceIndex,InstanceData->Info.Driver});
	}

	void UnregisterInstance(FJoltNetworkPredictionID ID)
	{
		Instances.Remove((int32)ID);
	}

	void CallServerRPC(float DeltaTimeSeconds) final override
	{
		for (auto& MapIt : Instances)
		{
			FInstance& Instance = MapIt.Value;
			FJoltNetworkPredictionDriver<ModelDef>::CallServerRPC(Instance.Driver);
		}
	}

	void AddInputToHandler(const int32& Frame) final override
	{
		for (auto& MapIt : Instances)
		{
			FInstance& Instance = MapIt.Value;
			UE_JNP_TRACE_SIM(Instance.TraceID);
			TInstanceData<ModelDef>& InstanceData = DataStore->Instances.GetByIndexChecked(Instance.InstanceIndex);
			UJoltNetworkPredictionPlayerControllerComponent* RPCHandler = InstanceData.Info.RPCHandler;
			if (RPCHandler && RPCHandler->GetNetConnection())
			{
				UPackageMap* Map = RPCHandler->GetNetConnection()->PackageMap;
				FNetBitWriter TempWriter(Map,0);
				FJoltNetSerializeParams Params(TempWriter,Map,EJoltReplicationProxyTarget::ServerRPC);
				TJoltInstanceFrameState<ModelDef>& Frames = DataStore->Frames.GetByIndexChecked(Instance.FramesID);
				if (NetworkPredictionCVars::ForceSendDefaultInputCommands())
				{
					// for debugging, send blank default input instead of what we've produced locally
					static TJoltConditionalState<InputType> DefaultInputCmd;
					FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(DefaultInputCmd, Params); 
				}
				else
				{
					FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(Frames.Buffer[Frame].InputCmd, Params); // 2. InputCmd
				}

				uint32 DataSize = (uint32)TempWriter.GetNumBits();
				RPCHandler->AddInputToSend(MapIt.Key, DataSize,*TempWriter.GetBuffer());
				RPCHandler->InterpolationTimeMS = Frames.Buffer[Frame].InterpolationTimeMS;
			}
		}
		
	}

private:

	// The vast majority of the time there will be <= 1 instances that wants to call the ServerRPC.
	// Only split screen type situations will require more.
	struct FInstance
	{
		int32 TraceID;
		int32 FramesID;
		int32 InstanceIndex;
		DriverType* Driver;
	};

	TSortedMap<int32, FInstance, TInlineAllocator<1>> Instances;
	TJoltModelDataStore<ModelDef>* DataStore;
};