// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MotionWarpingAdapter.h"
#include "JoltMoverComponent.h"
#include "MotionWarpingJoltMoverAdapter.generated.h"

#define UE_API JOLTMOVER_API

// Adapter for MoverComponent actors to participate in motion warping

UCLASS(MinimalAPI)
class UMotionWarpingJoltMoverAdapter : public UMotionWarpingBaseAdapter
{
	GENERATED_BODY()

public:
	UE_API virtual void BeginDestroy() override;

	UE_API void SetMoverComp(UJoltMoverComponent* InMoverComp);

	UE_API virtual AActor* GetActor() const override;
	UE_API virtual USkeletalMeshComponent* GetMesh() const override;
	UE_API virtual FVector GetVisualRootLocation() const override;
	UE_API virtual FVector GetBaseVisualTranslationOffset() const override;
	UE_API virtual FQuat GetBaseVisualRotationOffset() const override;

private:
	// This is called when our Mover actor wants to warp local motion, and passes the responsibility onto the warping component
	FTransform WarpLocalRootMotionOnMoverComp(const FTransform& LocalRootMotionTransform, float DeltaSeconds, const FMotionWarpingUpdateContext* OptionalWarpingContext);

	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<UJoltMoverComponent> TargetMoverComp;
};

#undef UE_API
