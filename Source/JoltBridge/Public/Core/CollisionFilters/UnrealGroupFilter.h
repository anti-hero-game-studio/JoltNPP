// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "JoltBridgeMain.h"

/**
 * 
 */
class JOLTBRIDGE_API FUnrealGroupFilter final : public JPH::GroupFilter
{
public:
	

	bool CanCollide(const JPH::CollisionGroup& A, const JPH::CollisionGroup& B) const override
	{
		const FJoltUserData* UA = JoltHelpers::UnpackDataFromGroupIDs<FJoltUserData>(A.GetGroupID(), A.GetSubGroupID());
		const FJoltUserData* UB = JoltHelpers::UnpackDataFromGroupIDs<FJoltUserData>(B.GetGroupID(), B.GetSubGroupID());

		if (!UA || UA->Magic != FJoltUserData::MagicValue) return true;  // or false depending on your policy
		if (!UB || UB->Magic != FJoltUserData::MagicValue) return true;
		
		
		// Allow both Block and Overlap through to narrowphase.
		return JoltHelpers::IsAnyCollisionAllowed(UA, UB);
	}


};
