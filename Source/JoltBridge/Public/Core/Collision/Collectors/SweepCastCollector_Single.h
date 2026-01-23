// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "JoltBridgeMain.h"

/**
 * 
 */
class JOLTBRIDGE_API FClosestShapeCastHitCollector final : public JPH::CastShapeCollector
{
public:
	
	FClosestShapeCastHitCollector(const JPH::PhysicsSystem& inPhysicsSystem, const JPH::RShapeCast& inRay)
		: mPhysicsSystem(inPhysicsSystem), mRay(inRay)
	{
	}
	
	virtual void AddHit(const JPH::ShapeCastResult& inResult) override
	{
		// Test if this collision is closer than the previous one
		if (inResult.mFraction < GetEarlyOutFraction())
		{
			// Lock the body
			JPH::BodyLockRead lock(mPhysicsSystem.GetBodyLockInterfaceNoLock(), inResult.mBodyID2);
			JPH_ASSERT(lock.Succeeded()); // When this runs all bodies are locked so this should not fail
			mBody = &lock.GetBody();

			if (mBody->IsSensor()) return;

			UpdateEarlyOutFraction(inResult.mFraction);

			// Get the contact properties
			if (mBody != nullptr)
			{
				mHasHit = true;
			}
			mSubShapeID2 = inResult.mSubShapeID2;
			mContactPosition = mRay.GetPointOnRay(inResult.mFraction);
			mContactNormal = mBody->GetWorldSpaceSurfaceNormal(inResult.mSubShapeID2, mContactPosition);
			mBodyID = inResult.mBodyID2;
		}
	}
	
	bool HasHit() const
	{
		return mHasHit;
	}
	
	
	// Configuration
	const JPH::PhysicsSystem& mPhysicsSystem;
	JPH::RShapeCast			  mRay;

	// Resulting closest collision
	const JPH::Body* mBody = nullptr;
	JPH::BodyID		 mBodyID;
	JPH::SubShapeID	 mSubShapeID2;
	JPH::RVec3		 mContactPosition;
	JPH::Vec3		 mContactNormal;
	bool			 mHasHit = false;
	
private:
	


};
