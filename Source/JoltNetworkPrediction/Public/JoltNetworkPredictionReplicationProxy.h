// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "JoltNetworkPredictionCheck.h"

#include "JoltNetworkPredictionReplicationProxy.generated.h"

struct FJoltNetworkPredictionProxy;
class UPackageMap;

// Target of replication
enum class EJoltReplicationProxyTarget: uint8
{
	ServerRPC,			// Client -> Server
	AutonomousProxy,	// Owning/Controlling client
	SimulatedProxy,		// Non owning client
	Replay,				// Replay net driver
};

inline FString LexToString(EJoltReplicationProxyTarget A)
{
	return *UEnum::GetValueAsString(TEXT("JoltNetworkPrediction.EJoltReplicationProxyTarget"), A);
}

// The parameters for NetSerialize that are passed around the system. Everything should use this, expecting to have to add more.
struct FJoltNetSerializeParams
{
	FJoltNetSerializeParams(FArchive& InAr) : Ar(InAr),Map(nullptr) { }
	FJoltNetSerializeParams(FArchive& InAr,UPackageMap* InMap) : Ar(InAr),Map(InMap) { }
	FJoltNetSerializeParams(FArchive& InAr,UPackageMap* InMap, const EJoltReplicationProxyTarget& InReplicationTarget) : Ar(InAr),Map(InMap),ReplicationTarget(InReplicationTarget) { }
	FArchive& Ar;
	UPackageMap* Map;
	EJoltReplicationProxyTarget ReplicationTarget = EJoltReplicationProxyTarget::ServerRPC;
	template<typename T>
	const T* GetBaseDeltaState() const
	{
		return static_cast<const T*>(BaseDeltaStatePtr);
	}
	
	const void* BaseDeltaStatePtr = nullptr;
};

// Redirects NetSerialize to a dynamically set NetSerializeFunc.
// This is how we hook into the replication systems role-based serialization
USTRUCT()
struct JOLTNETWORKPREDICTION_API FJoltReplicationProxy
{
	GENERATED_BODY()

	void Init(FJoltNetworkPredictionProxy* InJoltNetSimProxy, EJoltReplicationProxyTarget InReplicationTarget);
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
	void OnPreReplication();	
	bool Identical(const FJoltReplicationProxy* Other, uint32 PortFlags) const;

	TFunction<void(const FJoltNetSerializeParams& P)> NetSerializeFunc;
	FJoltNetworkPredictionProxy* JoltNetSimProxy = nullptr;

private:

	EJoltReplicationProxyTarget ReplicationTarget;
	int32 CachedPendingFrame = INDEX_NONE;
};

template<>
struct TStructOpsTypeTraits<FJoltReplicationProxy> : public TStructOpsTypeTraitsBase2<FJoltReplicationProxy>
{
	enum
	{
		WithNetSerializer = true,
		WithIdentical = true,
	};
};

// Collection of each replication proxy
struct FJoltReplicationProxySet
{
	FJoltReplicationProxy* ServerRPC = nullptr;
	FJoltReplicationProxy* AutonomousProxy = nullptr;
	FJoltReplicationProxy* SimulatedProxy = nullptr;
	FJoltReplicationProxy* Replay = nullptr;

	void UnbindAll() const
	{
		jnpCheckSlow(ServerRPC && AutonomousProxy && SimulatedProxy && Replay);
		ServerRPC->NetSerializeFunc = nullptr;
		AutonomousProxy->NetSerializeFunc = nullptr;
		SimulatedProxy->NetSerializeFunc = nullptr;
		Replay->NetSerializeFunc = nullptr;
	}
};

// -------------------------------------------------------------------------------------------------------------------------------
//	FServerRPCProxyParameter
//	Used for the client->Server RPC. Since this is instantiated on the stack by the replication system prior to net serializing,
//	we have no opportunity to point the RPC parameter to the member variables we want. So we serialize into a generic temp byte buffer
//	and move them into the real buffers on the component in the RPC body (via ::NetSerialzeToProxy).
// -------------------------------------------------------------------------------------------------------------------------------
USTRUCT()
struct JOLTNETWORKPREDICTION_API FJoltServerReplicationRPCParameter
{
	GENERATED_BODY()

	// Receive flow: ctor() -> NetSerializetoProxy
	FJoltServerReplicationRPCParameter() : Proxy(nullptr)	{ }
	void NetSerializeToProxy(FJoltReplicationProxy& InProxy);

	// Send flow: ctor(Proxy) -> NetSerialize
	FJoltServerReplicationRPCParameter(FJoltReplicationProxy& InProxy) : Proxy(&InProxy) { }
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

private:

	static TArray<uint8> TempStorage;

	FJoltReplicationProxy* Proxy;
	int64 CachedNumBits = -1;
	class UPackageMap* CachedPackageMap = nullptr;
};

template<>
struct TStructOpsTypeTraits<FJoltServerReplicationRPCParameter> : public TStructOpsTypeTraitsBase2<FJoltServerReplicationRPCParameter>
{
	enum
	{
		WithNetSerializer = true
	};
};

// Helper struct to bypass the bandwidth limit imposed by the engine's NetDriver (QueuedBits, NetSpeed, etc).
// This is really a temp measure to make the system easier to drop in/try in a project without messing with your engine settings.
// (bandwidth optimizations have not been done yet and the system in general hasn't been stressed with packetloss / gaps in command streams)
// So, you are free to use this in your own code but it may be removed one day. Hopefully in general bandwidth limiting will also become more robust.
struct JOLTNETWORKPREDICTION_API FJoltScopedBandwidthLimitBypass
{
	FJoltScopedBandwidthLimitBypass(AActor* OwnerActor);
	~FJoltScopedBandwidthLimitBypass();
private:

	int64 RestoreBits = 0;
	class UNetConnection* CachedNetConnection = nullptr;
};


USTRUCT()
struct FJoltSimulationReplicatedInput
{
	GENERATED_BODY()

	FJoltSimulationReplicatedInput(){}
	FJoltSimulationReplicatedInput(const int32& InID,const uint32& InDataSize, const TArray<uint8>& InData)
	{
		ID = InID;
		DataSize = InDataSize;
		InputData = InData;
	};
	
	UPROPERTY()
	uint32 ID = 0;

	UPROPERTY()
	uint32 DataSize = 0;

	UPROPERTY()
	TArray<uint8> InputData;

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& OutSuccess)
	{
		Ar.SerializeIntPacked(ID);
		Ar.SerializeIntPacked(DataSize);
		
		uint32 Num = InputData.Num();
		Ar.SerializeIntPacked(Num);
		if (Ar.IsLoading())
		{
			InputData.SetNum(Num);
		}
		for (uint32 i = 0; i < Num; i++)
		{
			Ar << InputData[i];
		}
		
		OutSuccess = true;
		return true;
	}
};
template<>
struct TStructOpsTypeTraits<FJoltSimulationReplicatedInput> : public TStructOpsTypeTraitsBase2<FJoltSimulationReplicatedInput>
{
	enum
	{
		WithNetSerializer = true,
	};
};

USTRUCT()
struct FJoltSimTimeDilation
{
	GENERATED_BODY()

	FJoltSimTimeDilation(){}
	FJoltSimTimeDilation(const float& InTimeDilation)
	{
		UpdateTimeDilation(InTimeDilation);
	};
	
	float GetTimeDilation() const {return TimeDilation / 10000.f;}
	void UpdateTimeDilation(const float& InTimeDilation)
	{
		TimeDilation = FMath::Clamp(FMath::RoundToInt32(InTimeDilation * 10000.f), 0, UINT16_MAX);
	}

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& OutSuccess)
	{
		Ar << TimeDilation;
		OutSuccess = true;
		return true;
	}

private:
	UPROPERTY()
	uint16 TimeDilation = 10000;
};
template<>
struct TStructOpsTypeTraits<FJoltSimTimeDilation> : public TStructOpsTypeTraitsBase2<FJoltSimTimeDilation>
{
	enum
	{
		WithNetSerializer = true,
	};
};

