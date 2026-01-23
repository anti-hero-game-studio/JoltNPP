// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoveLibrary/JoltConstrainedMoveUtils.h"
#include "JoltMoverLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltConstrainedMoveUtils)


void UJoltPlanarConstraintUtils::SetPlanarConstraintEnabled(UPARAM(ref) FJoltPlanarConstraint& Constraint, bool bEnabled)
{
	Constraint.bConstrainToPlane = bEnabled;
}


void UJoltPlanarConstraintUtils::SetPlanarConstraintNormal(UPARAM(ref) FJoltPlanarConstraint& Constraint, FVector PlaneNormal)
{
	PlaneNormal = PlaneNormal.GetSafeNormal();

	if (PlaneNormal.IsNearlyZero())
	{
		UE_LOG(LogJoltMover, Warning, TEXT("Can't use SetPlanarConstraintNormal with a zero-length normal. Leaving normal as %s"), *Constraint.PlaneConstraintNormal.ToCompactString());
	}
	else
	{
		Constraint.PlaneConstraintNormal = PlaneNormal;
	}
}


void UJoltPlanarConstraintUtils::SetPlaneConstraintOrigin(UPARAM(ref) FJoltPlanarConstraint& Constraint, FVector PlaneOrigin)
{
	Constraint.PlaneConstraintOrigin = PlaneOrigin;
}


FVector UJoltPlanarConstraintUtils::ConstrainDirectionToPlane(const FJoltPlanarConstraint& Constraint, FVector Direction, bool bMaintainMagnitude)
{
	if (Constraint.bConstrainToPlane)
	{
		float OrigSize = Direction.Size();

		Direction = FVector::VectorPlaneProject(Direction, Constraint.PlaneConstraintNormal);

		if (bMaintainMagnitude)
		{
			Direction = Direction.GetSafeNormal() * OrigSize;
		}
	}

	return Direction;
}

FVector UJoltPlanarConstraintUtils::ConstrainLocationToPlane(const FJoltPlanarConstraint& Constraint, FVector Location)
{
	if (Constraint.bConstrainToPlane)
	{
		Location = FVector::PointPlaneProject(Location, Constraint.PlaneConstraintOrigin, Constraint.PlaneConstraintNormal);
	}

	return Location;
}

FVector UJoltPlanarConstraintUtils::ConstrainNormalToPlane(const FJoltPlanarConstraint& Constraint, FVector Normal)
{
	if (Constraint.bConstrainToPlane)
	{
		Normal = FVector::VectorPlaneProject(Normal, Constraint.PlaneConstraintNormal).GetSafeNormal();
	}

	return Normal;
}
