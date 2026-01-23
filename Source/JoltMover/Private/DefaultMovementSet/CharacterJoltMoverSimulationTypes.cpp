// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultMovementSet/CharacterJoltMoverSimulationTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CharacterJoltMoverSimulationTypes)

bool FJoltFloorResultData::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Super::NetSerialize(Ar, Map, bOutSuccess);

	bool bBlockingHit = (bool)FloorResult.bBlockingHit;
	bool bLineTrace = (bool)FloorResult.bLineTrace;
	bool bWalkableFloor = (bool)FloorResult.bWalkableFloor;
	Ar.SerializeBits(&bBlockingHit, 1);
	Ar.SerializeBits(&bLineTrace, 1);
	Ar.SerializeBits(&bWalkableFloor, 1);

	if (Ar.IsLoading())
	{
		FloorResult.bBlockingHit = bBlockingHit;
		FloorResult.bLineTrace = bLineTrace;
		FloorResult.bWalkableFloor = bWalkableFloor;
	}

	Ar << FloorResult.FloorDist;
	FloorResult.HitResult.NetSerialize(Ar, Map, bOutSuccess);

	return true;
}

void FJoltFloorResultData::ToString(FAnsiStringBuilderBase& Out) const
{
	Super::ToString(Out);

	Out.Appendf("bBlockingHit: %i | ", FloorResult.bBlockingHit);
	Out.Appendf("bLineTrace: %i | ", FloorResult.bLineTrace);
	Out.Appendf("bWalkableFloor: %i | ", FloorResult.bWalkableFloor);
	Out.Appendf("FloorDist: %.2f/n", FloorResult.FloorDist);
	Out.Appendf("HitResult: %s/n", *FloorResult.HitResult.ToString());
}

bool FJoltFloorResultData::ShouldReconcile(const FJoltMoverDataStructBase& AuthorityState) const
{
	return false;
}

void FJoltFloorResultData::Interpolate(const FJoltMoverDataStructBase& From, const FJoltMoverDataStructBase& To, float Pct)
{
	if (Pct < 0.5f)
	{
		*this = static_cast<const FJoltFloorResultData&>(From);
	}
	else
	{
		*this = static_cast<const FJoltFloorResultData&>(To);
	}
}

void FJoltFloorResultData::Merge(const FJoltMoverDataStructBase& From)
{
}

void FJoltFloorResultData::Decay(float DecayAmount)
{
}
