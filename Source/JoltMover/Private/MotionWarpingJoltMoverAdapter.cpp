// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotionWarpingJoltMoverAdapter.h"
#include "Components/SkeletalMeshComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MotionWarpingJoltMoverAdapter)

void UMotionWarpingJoltMoverAdapter::BeginDestroy()
{
	if (TargetMoverComp)
	{
		TargetMoverComp->ProcessLocalRootMotionDelegate.Unbind();
	}

	Super::BeginDestroy();
}

void UMotionWarpingJoltMoverAdapter::SetMoverComp(UJoltMoverComponent* InMoverComp)
{

	TargetMoverComp = InMoverComp;
	TargetMoverComp->ProcessLocalRootMotionDelegate.BindUObject(this, &UMotionWarpingJoltMoverAdapter::WarpLocalRootMotionOnMoverComp);
}

AActor* UMotionWarpingJoltMoverAdapter::GetActor() const
{
	return TargetMoverComp->GetOwner();
}

USkeletalMeshComponent* UMotionWarpingJoltMoverAdapter::GetMesh() const
{
	return TargetMoverComp->GetPrimaryVisualComponent<USkeletalMeshComponent>();
}

FVector UMotionWarpingJoltMoverAdapter::GetVisualRootLocation() const
{
	if (const USceneComponent* PrimaryVisualComp = TargetMoverComp->GetPrimaryVisualComponent())
	{
		return PrimaryVisualComp->GetComponentLocation();
	}

	if (const USceneComponent* UpdatedComponent = TargetMoverComp->GetUpdatedComponent())
	{
		const FVector ActorRootLocation = UpdatedComponent->GetComponentLocation();
		const FQuat ActorRootOrientation = UpdatedComponent->GetComponentQuat();
		FBoxSphereBounds ActorRootBounds = UpdatedComponent->GetLocalBounds();

		return ActorRootLocation - (ActorRootOrientation.GetUpVector() * ActorRootBounds.BoxExtent.Z);
	}

	return TargetMoverComp->GetOwner()->GetActorLocation();
}

FVector UMotionWarpingJoltMoverAdapter::GetBaseVisualTranslationOffset() const
{
	// TODO: rework these GetBase****Offset functions once MoverComponent supports primary visual component offset (coming as part of mesh-based smoothing)
	if (const USceneComponent* VisualComp = TargetMoverComp->GetPrimaryVisualComponent())
	{
		return VisualComp->GetRelativeLocation();
	}

	return FVector::ZeroVector;
}

FQuat UMotionWarpingJoltMoverAdapter::GetBaseVisualRotationOffset() const
{
	if (const USceneComponent* VisualComp = TargetMoverComp->GetPrimaryVisualComponent())
	{
		return VisualComp->GetRelativeRotation().Quaternion();
	}

	return FQuat::Identity;
}

FTransform UMotionWarpingJoltMoverAdapter::WarpLocalRootMotionOnMoverComp(const FTransform& LocalRootMotionTransform, float DeltaSeconds, const FMotionWarpingUpdateContext* OptionalWarpingContext)
{
	if (WarpLocalRootMotionDelegate.IsBound())
	{
		return WarpLocalRootMotionDelegate.Execute(LocalRootMotionTransform, DeltaSeconds, OptionalWarpingContext);
	}

	return LocalRootMotionTransform;
}
