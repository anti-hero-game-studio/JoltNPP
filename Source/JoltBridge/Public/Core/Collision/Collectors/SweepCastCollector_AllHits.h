// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "JoltBridgeMain.h"
#include "Core/DataTypes/JoltBridgeTypes.h"
/**
 * 
 */
class JOLTBRIDGE_API FSweepCastCollector_AllHits final : public JPH::CastShapeCollector
{
public:
	
	FSweepCastCollector_AllHits(const JPH::PhysicsSystem& inPhysicsSystem, const JPH::RShapeCast& inRay)
		: mPhysicsSystem(inPhysicsSystem), mRay(inRay)
	{
	}
	
	// See: CollectorType::Reset
	virtual void Reset() override
	{
		JPH::CastShapeCollector::Reset();

		mHits.clear();
	}

	// See: CollectorType::AddHit
	virtual void AddHit(const JPH::ShapeCastResult &inResult) override
	{
		
		// Test if this collision is closer than the previous one
		if (inResult.mFraction < GetEarlyOutFraction())
		{
			// Lock the body
			JPH::BodyLockRead lock(mPhysicsSystem.GetBodyLockInterfaceNoLock(), inResult.mBodyID2);
			JPH_ASSERT(lock.Succeeded()); // When this runs all bodies are locked so this should not fail
			mBody = &lock.GetBody();

			if (mBody->IsSensor()) return;
			
			mHits.push_back(inResult);

			UpdateEarlyOutFraction(inResult.mFraction);
		}
		
	}

	/// Order hits on closest first
	void Sort()
	{
		QuickSort(mHits.begin(), mHits.end(), [](const ResultType &inLHS, const ResultType &inRHS) { return inLHS.GetEarlyOutFraction() < inRHS.GetEarlyOutFraction(); });
	}

	/// Check if any hits were collected
	FORCEINLINE bool HadHit() const
	{
		return !mHits.empty();
	}
	
	void GetData(const JPH::ShapeCastResult& Hit, JPH::BodyID& OutBodyID, JPH::SubShapeID& OutSubShapeId, FVector& OutContactPosition, FVector& OutContactNormal) const
	{
		// Get the contact properties
		
		JPH::BodyLockRead lock(mPhysicsSystem.GetBodyLockInterfaceNoLock(), Hit.mBodyID2);
		JPH_ASSERT(lock.Succeeded()); // When this runs all bodies are locked so this should not fail
		OutSubShapeId = Hit.mSubShapeID2;
		OutContactPosition = JoltHelpers::ToUnrealPosition(mRay.GetPointOnRay(Hit.mFraction));
		OutContactNormal = JoltHelpers::ToUnrealNormal( mBody->GetWorldSpaceSurfaceNormal(Hit.mSubShapeID2, mRay.GetPointOnRay(Hit.mFraction)));
		OutBodyID = Hit.mBodyID2;
	}

	JPH::Array<JPH::ShapeCastResult> mHits;
	const JPH::PhysicsSystem& mPhysicsSystem;
	JPH::RShapeCast mRay;
	const JPH::Body* mBody = nullptr;
};
