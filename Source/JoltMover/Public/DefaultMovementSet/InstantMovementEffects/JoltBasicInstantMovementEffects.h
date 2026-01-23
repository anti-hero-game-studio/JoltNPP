// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "JoltInstantMovementEffect.h"
#include "JoltBasicInstantMovementEffects.generated.h"

#define UE_API JOLTMOVER_API

/** Teleport: instantly moves an actor to a new location and rotation
 * For async-compatible teleportation, use AsyncTeleportEffect instead.
 */
USTRUCT(BlueprintType, Category = "Jolt Mover|Instant Movement Effect", DisplayName = "Teleport Instant Movement Effect")
struct FJoltTeleportEffect : public FJoltInstantMovementEffect
{
	GENERATED_BODY()

	UE_API FJoltTeleportEffect();
	virtual ~FJoltTeleportEffect() {}

	// Location to teleport to, in world space
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	FVector TargetLocation;

	// Whether this teleport effect should keep the actor's current rotation or use a specified one (TargetRotation)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	bool bUseActorRotation;
	
	// Actor rotation is set to this value on teleport if bUseActorRotation is false
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover, meta=(EditCondition="bUseActorRotation==false", EditConditionHides))
	FRotator TargetRotation;
	
	UE_API virtual bool ApplyMovementEffect(FJoltApplyMovementEffectParams& ApplyEffectParams, FJoltMoverSyncState& OutputState) override;
	UE_API virtual bool ApplyMovementEffect_Async(FJoltApplyMovementEffectParams_Async& ApplyEffectParams, FJoltMoverSyncState& OutputState) override;

	UE_API virtual FJoltInstantMovementEffect* Clone() const override;

	UE_API virtual void NetSerialize(FArchive& Ar) override;

	UE_API virtual UScriptStruct* GetScriptStruct() const override;

	UE_API virtual FString ToSimpleString() const override;

	UE_API virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;
};

/** Async Teleport: instantly moves an actor to a new location and rotation (compatible with async movement simulation) */
USTRUCT(BlueprintType, Category = "Jolt Mover|Instant Movement Effect", DisplayName = "Async Teleport Instant Movement Effect")
struct FAsyncTeleportEffect : public FJoltTeleportEffect
{
	GENERATED_BODY()

	UE_API virtual bool ApplyMovementEffect(FJoltApplyMovementEffectParams& ApplyEffectParams, FJoltMoverSyncState& OutputState) override;

	UE_API virtual FJoltInstantMovementEffect* Clone() const override;

	UE_API virtual UScriptStruct* GetScriptStruct() const override;

	UE_API virtual FString ToSimpleString() const override;
};

/** Jump Impulse: introduces an instantaneous upwards change in velocity. This overrides the existing 'up' component of the actor's current velocity
  * Note: this only applies the impulse for one tick!
  */
USTRUCT(BlueprintType, Category = "Jolt Mover|Instant Movement Effect", DisplayName = "Jump Impulse Instant Movement Effect")
struct FJumpImpulseEffect : public FJoltInstantMovementEffect
{
	GENERATED_BODY()

	UE_API FJumpImpulseEffect();

	virtual ~FJumpImpulseEffect() {}

	// Units per second, in whatever direction the target actor considers 'up'
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover, meta = (ClampMin = "0", UIMin = "0", ForceUnits = "cm/s"))
	float UpwardsSpeed;

	UE_API virtual bool ApplyMovementEffect(FJoltApplyMovementEffectParams& ApplyEffectParams, FJoltMoverSyncState& OutputState) override;

	UE_API virtual FJoltInstantMovementEffect* Clone() const override;

	UE_API virtual void NetSerialize(FArchive& Ar) override;

	UE_API virtual UScriptStruct* GetScriptStruct() const override;

	UE_API virtual FString ToSimpleString() const override;

	UE_API virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;
};

/** Apply Velocity: provides an impulse velocity to the actor after (optionally) forcing them into a particular movement mode
  * Note: this only applies the impulse for one tick!
  */
USTRUCT(BlueprintType, Category = "Jolt Mover|Instant Movement Effect", DisplayName = "Apply Velocity Instant Movement Effect")
struct FJoltApplyVelocityEffect : public FJoltInstantMovementEffect
{
	GENERATED_BODY()

	UE_API FJoltApplyVelocityEffect();
	virtual ~FJoltApplyVelocityEffect() {}

	// Velocity to apply to the actor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover, meta=(ForceUnits="cm/s"))
	FVector VelocityToApply;

	// If true VelocityToApply will be added to current velocity on this actor. If false velocity will be set directly to VelocityToApply
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	bool bAdditiveVelocity;
	
	// Optional movement mode name to force the actor into before applying the impulse velocity.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	FName ForceMovementMode;

	UE_API virtual bool ApplyMovementEffect(FJoltApplyMovementEffectParams& ApplyEffectParams, FJoltMoverSyncState& OutputState) override;
	
	UE_API virtual FJoltInstantMovementEffect* Clone() const override;

	UE_API virtual void NetSerialize(FArchive& Ar) override;

	UE_API virtual UScriptStruct* GetScriptStruct() const override;

	UE_API virtual FString ToSimpleString() const override;

	UE_API virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;
};

#undef UE_API
