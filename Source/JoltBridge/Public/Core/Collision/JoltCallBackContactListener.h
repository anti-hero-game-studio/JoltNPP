// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "JoltBridgeMain.h"
#include "JoltCallBackContactListener.generated.h"

USTRUCT(BlueprintType)
struct FContactAddedInfo
{
	GENERATED_USTRUCT_BODY();

	FContactAddedInfo() = default;

	FContactAddedInfo(const int32 BodyID1, const int32 BodyID2, const FVector& BodyID1ContactLocation, const FVector& BodyID2ContactLocation, const float NormalImpulse, const FVector& NormalDir, const bool bIsOverlap)
		: BodyID1(BodyID1)
		, BodyID2(BodyID2)
		, BodyID1ContactLocation(BodyID1ContactLocation)
		, BodyID2ContactLocation(BodyID2ContactLocation)
		, NormalImpulse(NormalImpulse)
		, NormalDir(NormalDir)
		, bIsOverlap(bIsOverlap)
	{}

	int32 BodyID1;

	int32 BodyID2;

	FVector BodyID1ContactLocation;

	FVector BodyID2ContactLocation;

	float NormalImpulse;

	FVector NormalDir;
	
	bool bIsOverlap;
};

USTRUCT(BlueprintType)
struct FContactRemovedInfo
{
	GENERATED_BODY()
	
	FContactRemovedInfo() = default;
	
	FContactRemovedInfo(const int32 InBodyID1, const int32 InBodyID2)
		: BodyID1(InBodyID1), BodyID2(InBodyID2)
	{
		
	}
	
	int32 BodyID1;
	int32 BodyID2;
};

/**
 * 
 */
class JOLTBRIDGE_API FJoltCallBackContactListener : public JPH::ContactListener
{

public:
	virtual JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2, JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult) override;

	virtual void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override;

	virtual void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override;

	virtual void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override;

	bool ConsumeAddedContacts(FContactAddedInfo& OutItem)
	{
		return AddedContactQueue.Dequeue(OutItem);
	}
	
	bool ConsumeRemovedContacts(FContactRemovedInfo& OutItem)
	{
		return RemovedContactQueue.Dequeue(OutItem);
	}

	TQueue<FContactAddedInfo, EQueueMode::Mpsc>* GetContactQueue() { return &AddedContactQueue; };

private:
	TQueue<FContactAddedInfo, EQueueMode::Mpsc> AddedContactQueue = TQueue<FContactAddedInfo, EQueueMode::Mpsc>();
	TQueue<FContactRemovedInfo, EQueueMode::Mpsc> RemovedContactQueue = TQueue<FContactRemovedInfo, EQueueMode::Mpsc>();
};
