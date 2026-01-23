// Copyright Epic Games, Inc. All Rights Reserved

#include "JoltNetworkPredictionReplicationProxy.h"
#include "JoltNetworkPredictionProxy.h"
#include "Engine/NetConnection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltNetworkPredictionReplicationProxy)

// -------------------------------------------------------------------------------------------------------------------------------
//	FJoltReplicationProxy
// -------------------------------------------------------------------------------------------------------------------------------

void FJoltReplicationProxy::Init(FJoltNetworkPredictionProxy* InJoltNetSimProxy, EJoltReplicationProxyTarget InReplicationTarget)
{
	JoltNetSimProxy = InJoltNetSimProxy;
	ReplicationTarget = InReplicationTarget;
}

bool FJoltReplicationProxy::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	if (jnpEnsureMsgf(NetSerializeFunc, TEXT("NetSerializeFunc not set for FJoltReplicationProxy %d"), ReplicationTarget))
	{
		NetSerializeFunc(FJoltNetSerializeParams(Ar,Map,ReplicationTarget));
		return true;
	}
	return true;
}

void FJoltReplicationProxy::OnPreReplication()
{
	if (JoltNetSimProxy)
	{
		CachedPendingFrame = JoltNetSimProxy->GetPendingFrame();
	}
}

bool FJoltReplicationProxy::Identical(const FJoltReplicationProxy* Other, uint32 PortFlags) const
{
	return (CachedPendingFrame == Other->CachedPendingFrame);
}

// -------------------------------------------------------------------------------------------------------------------------------
//	FServerRPCProxyParameter
// -------------------------------------------------------------------------------------------------------------------------------

TArray<uint8> FJoltServerReplicationRPCParameter::TempStorage;

bool FJoltServerReplicationRPCParameter::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	if (Ar.IsLoading())
	{
		// Loading: serialize to temp storage. We'll do the real deserialize in a manual call to ::NetSerializeToProxy
		FNetBitReader& BitReader = (FNetBitReader&)Ar;
		CachedNumBits = BitReader.GetBitsLeft();
		CachedPackageMap = Map;

		const int64 BytesLeft = BitReader.GetBytesLeft();
		check(BytesLeft > 0); // Should not possibly be able to get here with an empty archive
		TempStorage.Reset(BytesLeft);
		TempStorage.SetNumUninitialized(BytesLeft);
		TempStorage.Last() = 0;

		BitReader.SerializeBits(TempStorage.GetData(), CachedNumBits);
	}
	else
	{
		// Saving: directly call into the proxy's NetSerialize. No need for temp storage.
		check(Proxy); // Must have been set before, via ctor.
		return Proxy->NetSerialize(Ar, Map, bOutSuccess);
	}

	return true;
}

void FJoltServerReplicationRPCParameter::NetSerializeToProxy(FJoltReplicationProxy& InProxy)
{
	check(CachedPackageMap != nullptr);
	check(CachedNumBits != -1);

	FNetBitReader BitReader(CachedPackageMap, TempStorage.GetData(), CachedNumBits);

	bool bOutSuccess = true;
	InProxy.NetSerialize(BitReader, CachedPackageMap, bOutSuccess);

	CachedNumBits = -1;
	CachedPackageMap = nullptr;
}

// -------------------------------------------------------------------------------------------------------------------------------
//	FJoltScopedBandwidthLimitBypass
// -------------------------------------------------------------------------------------------------------------------------------

FJoltScopedBandwidthLimitBypass::FJoltScopedBandwidthLimitBypass(AActor* OwnerActor)
{
	if (OwnerActor)
	{
		CachedNetConnection = OwnerActor->GetNetConnection();
		if (CachedNetConnection)
		{
			RestoreBits = CachedNetConnection->QueuedBits + CachedNetConnection->SendBuffer.GetNumBits();
		}
	}
}

FJoltScopedBandwidthLimitBypass::~FJoltScopedBandwidthLimitBypass()
{
	if (CachedNetConnection)
	{
		CachedNetConnection->QueuedBits = RestoreBits - CachedNetConnection->SendBuffer.GetNumBits();
	}
}

