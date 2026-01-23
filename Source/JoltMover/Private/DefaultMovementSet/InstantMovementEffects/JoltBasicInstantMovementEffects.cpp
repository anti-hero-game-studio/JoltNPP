// Copyright Epic Games, Inc. All Rights Reserved.


#include "DefaultMovementSet/InstantMovementEffects/JoltBasicInstantMovementEffects.h"

#include "JoltMoverComponent.h"
#include "JoltMoverDataModelTypes.h"
#include "JoltMoverSimulationTypes.h"
#include "JoltMoverSimulation.h"
#include "DefaultMovementSet/Settings/JoltCommonLegacyMovementSettings.h"
#include "MoveLibrary/JoltMovementUtils.h"
#include "MoveLibrary/JoltMoverBlackboard.h"
#include "DrawDebugHelpers.h"

// -------------------------------------------------------------------
// FJoltTeleportEffect
// -------------------------------------------------------------------

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltBasicInstantMovementEffects)

static int32 ShowTeleportDiffs = 0;
static float ShowTeleportDiffsLifetimeSecs = 3.0f;
FAutoConsoleVariableRef CVarShowTeleportDiffs(
	TEXT("jolt.mover.debug.ShowTeleportDiffs"),
	ShowTeleportDiffs,
	TEXT("Whether to draw teleportation differences (red is initially blocked, green is corrected).\n")
	TEXT("0: Disable, 1: Enable"),
	ECVF_Cheat);


FJoltTeleportEffect::FJoltTeleportEffect()
	: TargetLocation(FVector::ZeroVector)
	, bUseActorRotation(true)
	, TargetRotation(FRotator::ZeroRotator)
{
}

bool FJoltTeleportEffect::ApplyMovementEffect(FJoltApplyMovementEffectParams& ApplyEffectParams, FJoltMoverSyncState& OutputState)
{
	USceneComponent* UpdatedComponent = ApplyEffectParams.UpdatedComponent;
	const FRotator FinalTargetRotation = bUseActorRotation ? UpdatedComponent->GetComponentRotation() : TargetRotation;
	
	const FVector PreviousLocation = UpdatedComponent->GetComponentLocation();
	const FQuat PreviousRotation = UpdatedComponent->GetComponentQuat();
	AActor* OwnerActor = UpdatedComponent->GetOwner();

	if (OwnerActor->TeleportTo(TargetLocation, FinalTargetRotation))
	{
		const FVector UpdatedLocation = UpdatedComponent->GetComponentLocation();
#if !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
		if (ShowTeleportDiffs)
		{
			if (!(UpdatedLocation - TargetLocation).IsNearlyZero())	// if it was adjusted, show the original error
			{
				DrawDebugCapsule(OwnerActor->GetWorld(), TargetLocation, OwnerActor->GetSimpleCollisionHalfHeight(), OwnerActor->GetSimpleCollisionRadius(), FQuat::Identity, FColor::Red, false, ShowTeleportDiffsLifetimeSecs);
			}
			DrawDebugCapsule(OwnerActor->GetWorld(), UpdatedLocation, OwnerActor->GetSimpleCollisionHalfHeight(), OwnerActor->GetSimpleCollisionRadius(), FQuat::Identity, FColor(100, 100, 255), false, ShowTeleportDiffsLifetimeSecs);
		}
#endif // !defined(BUILD_SHIPPING) || !BUILD_SHIPPING

		FJoltUpdatedMotionState& OutputSyncState = OutputState.Collection.FindOrAddMutableDataByType<FJoltUpdatedMotionState>();
		OutputSyncState.SetTransforms_WorldSpace(UpdatedLocation,
													UpdatedComponent->GetComponentRotation(),
													OutputSyncState.GetVelocity_WorldSpace(),
													OutputSyncState.GetAngularVelocityDegrees_WorldSpace(),
													nullptr ); // no movement base
		
		// TODO: instead of invalidating it, consider checking for a floor. Possibly a dynamic base?
		if (UJoltMoverBlackboard* SimBlackboard = ApplyEffectParams.MoverComp->GetSimBlackboard_Mutable())
		{
			SimBlackboard->Invalidate(CommonBlackboard::LastFloorResult);
			SimBlackboard->Invalidate(CommonBlackboard::LastFoundDynamicMovementBase);
		}

		ApplyEffectParams.OutputEvents.Add(MakeShared<FJoltTeleportSucceededEventData>(ApplyEffectParams.TimeStep->BaseSimTimeMs, PreviousLocation, PreviousRotation, TargetLocation, FQuat(FinalTargetRotation)));

		return true;
	}

#if !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
	if (ShowTeleportDiffs)
	{
		DrawDebugCapsule(OwnerActor->GetWorld(), TargetLocation, OwnerActor->GetSimpleCollisionHalfHeight(), OwnerActor->GetSimpleCollisionRadius(), FQuat::Identity, FColor::Red, false, ShowTeleportDiffsLifetimeSecs);
	}
#endif // !defined(BUILD_SHIPPING) || !BUILD_SHIPPING

	ApplyEffectParams.OutputEvents.Add(MakeShared<FJoltTeleportFailedEventData>(ApplyEffectParams.TimeStep->BaseSimTimeMs, PreviousLocation, PreviousRotation, TargetLocation, FQuat(FinalTargetRotation), ETeleportFailureReason::Reason_NotAvailable));

	return false;
}

bool FJoltTeleportEffect::ApplyMovementEffect_Async(FJoltApplyMovementEffectParams_Async& ApplyEffectParams, FJoltMoverSyncState& OutputState)
{
	if (ApplyEffectParams.Simulation)
	{
		ApplyEffectParams.Simulation->AttemptTeleport(*ApplyEffectParams.TimeStep, FTransform(TargetRotation, TargetLocation), bUseActorRotation, OutputState);
		return true;
	}

	return false;
}

FJoltInstantMovementEffect* FJoltTeleportEffect::Clone() const
{
	FJoltTeleportEffect* CopyPtr = new FJoltTeleportEffect(*this);
	return CopyPtr;
}

void FJoltTeleportEffect::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	Ar << TargetLocation;
	
	Ar.SerializeBits(&bUseActorRotation, 1);
	if (!bUseActorRotation)
	{
		Ar << TargetRotation;
	}
}

UScriptStruct* FJoltTeleportEffect::GetScriptStruct() const
{
	return FJoltTeleportEffect::StaticStruct();
}

FString FJoltTeleportEffect::ToSimpleString() const
{
	return bUseActorRotation ? FString::Printf(TEXT("Teleport to %s (bUseActorRotation = True)"), *TargetLocation.ToString()) : FString::Printf(TEXT("Teleport to %s, %s (bUseActorRotation = False)"), *TargetLocation.ToString(), *FRotator(TargetRotation).ToString());
}

void FJoltTeleportEffect::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}


// -------------------------------------------------------------------
// FAsyncTeleportEffect
// -------------------------------------------------------------------

bool FAsyncTeleportEffect::ApplyMovementEffect(FJoltApplyMovementEffectParams& ApplyEffectParams, FJoltMoverSyncState& OutputState)
{
	FVector TeleportLocation = TargetLocation;
	const FRotator TeleportRotation = bUseActorRotation ? ApplyEffectParams.UpdatedComponent->GetComponentRotation() : TargetRotation;

	if (UJoltMovementUtils::FindTeleportSpot(ApplyEffectParams.MoverComp, OUT TeleportLocation, TeleportRotation))
	{
		if (ShowTeleportDiffs)
		{
			const AActor* OwnerActor = ApplyEffectParams.UpdatedComponent->GetOwner();

			if (!(TeleportLocation - TargetLocation).IsNearlyZero())	// if it was adjusted, show the original error
			{
				DrawDebugCapsule(OwnerActor->GetWorld(), TargetLocation, OwnerActor->GetSimpleCollisionHalfHeight(), OwnerActor->GetSimpleCollisionRadius(), FQuat::Identity, FColor::Red, false, ShowTeleportDiffsLifetimeSecs);
			}

			DrawDebugCapsule(OwnerActor->GetWorld(), TeleportLocation, OwnerActor->GetSimpleCollisionHalfHeight(), OwnerActor->GetSimpleCollisionRadius(), FQuat::Identity, FColor(100, 100, 255), false, ShowTeleportDiffsLifetimeSecs);
		}

		FJoltUpdatedMotionState& OutputSyncState = OutputState.Collection.FindOrAddMutableDataByType<FJoltUpdatedMotionState>();

		if (const FJoltUpdatedMotionState* StartingSyncState = ApplyEffectParams.StartState->SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>())
		{
			OutputSyncState.SetTransforms_WorldSpace(TeleportLocation,
				TeleportRotation,
				OutputSyncState.GetVelocity_WorldSpace(),
				OutputSyncState.GetAngularVelocityDegrees_WorldSpace(),
				nullptr); // no movement base

			// TODO: instead of invalidating it, consider checking for a floor. Possibly a dynamic base?
			if (UJoltMoverBlackboard* SimBlackboard = ApplyEffectParams.MoverComp->GetSimBlackboard_Mutable())
			{
				SimBlackboard->Invalidate(CommonBlackboard::LastFloorResult);
				SimBlackboard->Invalidate(CommonBlackboard::LastFoundDynamicMovementBase);
			}

			return true;
		}
	}


	if (ShowTeleportDiffs)
	{
		const AActor* OwnerActor = ApplyEffectParams.UpdatedComponent->GetOwner();
		DrawDebugCapsule(OwnerActor->GetWorld(), TargetLocation, OwnerActor->GetSimpleCollisionHalfHeight(), OwnerActor->GetSimpleCollisionRadius(), FQuat::Identity, FColor::Red, false, ShowTeleportDiffsLifetimeSecs);
	}

	return false;
}

FJoltInstantMovementEffect* FAsyncTeleportEffect::Clone() const
{
	FAsyncTeleportEffect* CopyPtr = new FAsyncTeleportEffect(*this);
	return CopyPtr;
}

UScriptStruct* FAsyncTeleportEffect::GetScriptStruct() const
{
	return FAsyncTeleportEffect::StaticStruct();
}

FString FAsyncTeleportEffect::ToSimpleString() const
{
	return FString::Printf(TEXT("Async Teleport"));
}


// -------------------------------------------------------------------
// FJumpImpulseEffect
// -------------------------------------------------------------------

FJumpImpulseEffect::FJumpImpulseEffect()
	: UpwardsSpeed(0.f)
{
}

bool FJumpImpulseEffect::ApplyMovementEffect(FJoltApplyMovementEffectParams& ApplyEffectParams, FJoltMoverSyncState& OutputState)
{
	if (const FJoltUpdatedMotionState* SyncState = ApplyEffectParams.StartState->SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>())
	{
		FJoltUpdatedMotionState& OutputSyncState = OutputState.Collection.FindOrAddMutableDataByType<FJoltUpdatedMotionState>();
		
		const FVector UpDir = ApplyEffectParams.MoverComp->GetUpDirection();
		const FVector ImpulseVelocity = UpDir * UpwardsSpeed;
	
		// Jump impulse overrides vertical velocity while maintaining the rest
		const FVector PriorVelocityWS = SyncState->GetVelocity_WorldSpace();
		const FVector StartingNonUpwardsVelocity = PriorVelocityWS - PriorVelocityWS.ProjectOnToNormal(UpDir);

		if (const UJoltCommonLegacyMovementSettings* CommonSettings = ApplyEffectParams.MoverComp->FindSharedSettings<UJoltCommonLegacyMovementSettings>())
		{
			OutputState.MovementMode = CommonSettings->AirMovementModeName;
		}
		
		FJoltRelativeBaseInfo MovementBaseInfo;
		if (const UJoltMoverBlackboard* SimBlackboard = ApplyEffectParams.MoverComp->GetSimBlackboard())
		{
			SimBlackboard->TryGet(CommonBlackboard::LastFoundDynamicMovementBase, MovementBaseInfo);
		}

		const FVector FinalVelocity = StartingNonUpwardsVelocity + ImpulseVelocity;
		OutputSyncState.SetTransforms_WorldSpace( ApplyEffectParams.UpdatedComponent->GetComponentLocation(),
												  ApplyEffectParams.UpdatedComponent->GetComponentRotation(),
												  FinalVelocity,
												  FVector::ZeroVector,
												  MovementBaseInfo.MovementBase.Get(),
												  MovementBaseInfo.BoneName);
		
		ApplyEffectParams.UpdatedComponent->ComponentVelocity = FinalVelocity;
		
		return true;
	}

	return false;
}

FJoltInstantMovementEffect* FJumpImpulseEffect::Clone() const
{
	FJumpImpulseEffect* CopyPtr = new FJumpImpulseEffect(*this);
	return CopyPtr;
}

void FJumpImpulseEffect::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	Ar << UpwardsSpeed;
}

UScriptStruct* FJumpImpulseEffect::GetScriptStruct() const
{
	return FJumpImpulseEffect::StaticStruct();
}

FString FJumpImpulseEffect::ToSimpleString() const
{
	return FString::Printf(TEXT("JumpImpulse"));
}

void FJumpImpulseEffect::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}

// -------------------------------------------------------------------
// FJoltApplyVelocityEffect
// -------------------------------------------------------------------

FJoltApplyVelocityEffect::FJoltApplyVelocityEffect()
	: VelocityToApply(FVector::ZeroVector)
	, bAdditiveVelocity(false)
	, ForceMovementMode(NAME_None)
{
}

bool FJoltApplyVelocityEffect::ApplyMovementEffect(FJoltApplyMovementEffectParams& ApplyEffectParams, FJoltMoverSyncState& OutputState)
{
	FJoltUpdatedMotionState& OutputSyncState = OutputState.Collection.FindOrAddMutableDataByType<FJoltUpdatedMotionState>();
	
	OutputState.MovementMode = ForceMovementMode;
	
	FJoltRelativeBaseInfo MovementBaseInfo;
	if (const UJoltMoverBlackboard* SimBlackboard = ApplyEffectParams.MoverComp->GetSimBlackboard())
	{
		SimBlackboard->TryGet(CommonBlackboard::LastFoundDynamicMovementBase, MovementBaseInfo);
	}

	FVector Velocity = VelocityToApply;
	if (bAdditiveVelocity)
	{
		if (const FJoltUpdatedMotionState* SyncState = ApplyEffectParams.StartState->SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>())
		{
			Velocity += SyncState->GetVelocity_WorldSpace();
		}
	}

	OutputSyncState.SetTransforms_WorldSpace( ApplyEffectParams.UpdatedComponent->GetComponentLocation(),
											  ApplyEffectParams.UpdatedComponent->GetComponentRotation(),
											  Velocity,
											  FVector::ZeroVector,
											  MovementBaseInfo.MovementBase.Get(),
											  MovementBaseInfo.BoneName);

	ApplyEffectParams.UpdatedComponent->ComponentVelocity = Velocity;
	
	return true;
}

FJoltInstantMovementEffect* FJoltApplyVelocityEffect::Clone() const
{
	FJoltApplyVelocityEffect* CopyPtr = new FJoltApplyVelocityEffect(*this);
	return CopyPtr;
}

void FJoltApplyVelocityEffect::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	SerializePackedVector<10, 16>(VelocityToApply, Ar);

	Ar << bAdditiveVelocity;
	
	bool bUsingForcedMovementMode = !ForceMovementMode.IsNone();
	Ar.SerializeBits(&bUsingForcedMovementMode, 1);

	if (bUsingForcedMovementMode)
	{
		Ar << ForceMovementMode;
	}
}

UScriptStruct* FJoltApplyVelocityEffect::GetScriptStruct() const
{
	return FJoltApplyVelocityEffect::StaticStruct();
}

FString FJoltApplyVelocityEffect::ToSimpleString() const
{
	return FString::Printf(TEXT("ApplyVelocity"));
}

void FJoltApplyVelocityEffect::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}
