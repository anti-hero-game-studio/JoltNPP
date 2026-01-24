// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/Collision/JoltCallBackContactListener.h"
#include "Core/Libraries/JoltBridgeLibrary.h"

JPH::ValidateResult FJoltCallBackContactListener::OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2, JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult)
{
	return ContactListener::OnContactValidate(inBody1, inBody2, inBaseOffset, inCollisionResult);
}

void FJoltCallBackContactListener::OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings)
{

	bool bIsAnOverlap = false;
	JPH::CollisionEstimationResult result;
	if (JoltHelpers::IsOverlappingCollisionAllowed(&inBody1, &inBody2))
	{
		ioSettings.mIsSensor = true;
		bIsAnOverlap = true;
	}
	EstimateCollisionResponse(inBody1, inBody2, inManifold, result, ioSettings.mCombinedFriction, ioSettings.mCombinedRestitution);

	for (uint8 i = 0; const JPH::CollisionEstimationResult::Impulse& impulse : result.mImpulses)
	{
		
		AddedContactQueue.Enqueue(
			FContactAddedInfo
			(
				inBody1.GetID().GetIndexAndSequenceNumber(),
				inBody2.GetID().GetIndexAndSequenceNumber(),
				JoltHelpers::ToUnrealPosition(inManifold.GetWorldSpaceContactPointOn1(i)),
				JoltHelpers::ToUnrealPosition(inManifold.GetWorldSpaceContactPointOn2(i)),
				JoltHelpers::ToUnrealFloat(impulse.mContactImpulse),
				JoltHelpers::ToUnrealNormal(inManifold.mWorldSpaceNormal),
				bIsAnOverlap
			)

		);

		i++;
	}
}

void FJoltCallBackContactListener::OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings)
{
	// return ContactListener::OnContactPersisted(inBody1, inBody2, inManifold, ioSettings);
}

void FJoltCallBackContactListener::OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) 
{
	RemovedContactQueue.Enqueue
	(
		FContactRemovedInfo(inSubShapePair.GetBody1ID().GetIndexAndSequenceNumber(), inSubShapePair.GetBody2ID().GetIndexAndSequenceNumber())
	);
};
