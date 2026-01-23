// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "GameFramework/Actor.h"

#include "JoltNetworkPredictionReplicatedManager.generated.h"

// This is a replicated "manager" for network prediction. Its purpose is only to replicate system-wide data that is not bound to an actor.
// Currently this is only to house a "mini packagemap" which allows stable shared indices that map to a small set of uobjects to be.
// UPackageMap can assign per-client net indices which invalidates sharing as well as forces 32 bit guis. this is a more specialzed case
// where we want to replicate IDs as btyes.

USTRUCT()
struct FJoltSharedPackageMapItem
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftObjectPtr<UObject> SoftPtr;
};

USTRUCT()
struct FJoltSharedPackageMap
{
	GENERATED_BODY()
	
	UPROPERTY()
	TArray<FJoltSharedPackageMapItem> Items;
};

UCLASS()
class JOLTNETWORKPREDICTION_API AJoltNetworkPredictionReplicatedManager : public AActor
{
	GENERATED_BODY()

public:

	AJoltNetworkPredictionReplicatedManager();

	static FDelegateHandle OnAuthoritySpawn(const TFunction<void(AJoltNetworkPredictionReplicatedManager*)>& Func);
	static void UnregisterOnAuthoritySpawn(FDelegateHandle Handle) { OnAuthoritySpawnDelegate.Remove(Handle); }

	virtual void BeginPlay();

	uint8 AddObjectToSharedPackageMap(TSoftObjectPtr<UObject> SoftPtr);

	uint8 GetIDForObject(UObject* Obj) const;

	TSoftObjectPtr<UObject> GetObjectForID(uint8 ID) const;

private:

	UPROPERTY(Replicated)
	FJoltSharedPackageMap SharedPackageMap;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAuthoritySpawn, AJoltNetworkPredictionReplicatedManager*)
	static FOnAuthoritySpawn OnAuthoritySpawnDelegate;

	static TWeakObjectPtr<AJoltNetworkPredictionReplicatedManager> AuthorityInstance;
};
