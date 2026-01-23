// Fill out your copyright notice in the Description page of Project Settings.


#include "MoveLibrary/JoltPhysicsGroundMovementUtils.h"
#include "Core/Singletons/JoltPhysicsWorldSubsystem.h"
#include "MoveLibrary/JoltFloorQueryUtils.h"

FVector UJoltPhysicsGroundMovementUtils::ComputeLocalGroundVelocity_Internal(const UObject* WorldContextObject,
const FVector& Position, const FJoltFloorCheckResult& FloorResult)
{

	FVector GroundVelocity = FVector::ZeroVector;
	
	if (!WorldContextObject) return GroundVelocity;

	UJoltPhysicsWorldSubsystem* Subsystem = WorldContextObject->GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>();
	if (!Subsystem) return GroundVelocity;
	
	if (const JPH::Body* Rigid = Subsystem->GetRigidBody(FloorResult.HitResult))
	{
		const FTransform ComTransform = JoltHelpers::ToUnrealTransform(Rigid->GetCenterOfMassTransform(), FVector(0));
		FVector Offset = Position - ComTransform.GetLocation();
		Offset -= Offset.ProjectOnToNormal(FloorResult.HitResult.ImpactNormal);
		GroundVelocity = JoltHelpers::ToUnrealVector3(Rigid->GetLinearVelocity()) + JoltHelpers::ToUnrealVector3(Rigid->GetAngularVelocity()).Cross(Offset);
	}
	return GroundVelocity;
}
