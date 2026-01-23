// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultMovementSet/Modes/JoltAsyncFlyingMode.h"
#include "MoveLibrary/JoltAirMovementUtils.h"
#include "MoveLibrary/JoltFloorQueryUtils.h"
#include "MoveLibrary/JoltGroundMovementUtils.h"
#include "MoveLibrary/JoltMovementUtils.h"
#include "MoveLibrary/JoltAsyncMovementUtils.h"
#include "JoltMoverComponent.h"
#include "DefaultMovementSet/Settings/JoltCommonLegacyMovementSettings.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltAsyncFlyingMode)


UJoltAsyncFlyingMode::UJoltAsyncFlyingMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SharedSettingsClasses.Add(UJoltCommonLegacyMovementSettings::StaticClass());
	
	GameplayTags.AddTag(JoltMover_IsInAir);
	GameplayTags.AddTag(JoltMover_IsFlying);
}

void UJoltAsyncFlyingMode::GenerateMove_Implementation(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, FJoltProposedMove& OutProposedMove) const
{
	const UJoltMoverComponent* MoverComp = GetMoverComponent();
	const FJoltCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.Collection.FindDataByType<FJoltCharacterDefaultInputs>();
	const FJoltUpdatedMotionState* StartingSyncState = StartState.SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>();
	check(StartingSyncState);

	const float DeltaSeconds = TimeStep.StepMs * 0.001f;

	FJoltFreeMoveParams Params;
	if (CharacterInputs)
	{
		Params.MoveInputType = CharacterInputs->GetMoveInputType();
		const bool bMaintainInputMagnitude = true;
		Params.MoveInput = UJoltPlanarConstraintUtils::ConstrainDirectionToPlane(MoverComp->GetPlanarConstraint(), CharacterInputs->GetMoveInput_WorldSpace(), bMaintainInputMagnitude);
	}
	else
	{
		Params.MoveInputType = EJoltMoveInputType::None;
		Params.MoveInput = FVector::ZeroVector;
	}

	FRotator IntendedOrientation_WorldSpace;
	// If there's no intent from input to change orientation, use the current orientation
	if (!CharacterInputs || CharacterInputs->OrientationIntent.IsNearlyZero())
	{
		IntendedOrientation_WorldSpace = StartingSyncState->GetOrientation_WorldSpace();
	}
	else
	{
		IntendedOrientation_WorldSpace = CharacterInputs->GetOrientationIntentDir_WorldSpace().ToOrientationRotator();
	}

	IntendedOrientation_WorldSpace = UJoltMovementUtils::ApplyGravityToOrientationIntent(IntendedOrientation_WorldSpace, MoverComp->GetWorldToGravityTransform(), CommonLegacySettings->bShouldRemainVertical);
	
	Params.OrientationIntent = IntendedOrientation_WorldSpace;
	Params.PriorVelocity = StartingSyncState->GetVelocity_WorldSpace();
	Params.PriorOrientation = StartingSyncState->GetOrientation_WorldSpace();
	Params.TurningRate = CommonLegacySettings->TurningRate;
	Params.TurningBoost = CommonLegacySettings->TurningBoost;
	Params.MaxSpeed = CommonLegacySettings->MaxSpeed;
	Params.Acceleration = CommonLegacySettings->Acceleration;
	Params.Deceleration = CommonLegacySettings->Deceleration;
	Params.DeltaSeconds = DeltaSeconds;
	Params.WorldToGravityQuat = MoverComp->GetWorldToGravityTransform();
	Params.bUseAccelerationForVelocityMove = CommonLegacySettings->bUseAccelerationForVelocityMove;
	
	OutProposedMove = UJoltAirMovementUtils::ComputeControlledFreeMove(Params);
}

void UJoltAsyncFlyingMode::SimulationTick_Implementation(const FJoltSimulationTickParams& Params, FJoltMoverTickEndData& OutputState)
{
	const UJoltMoverComponent* MoverComp = GetMoverComponent();
	const FJoltMoverTickStartData& StartState = Params.StartState;
	USceneComponent* UpdatedComponent = Params.MovingComps.UpdatedComponent.Get();
	const FJoltProposedMove& ProposedMove = Params.ProposedMove;

	const FJoltCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.Collection.FindDataByType<FJoltCharacterDefaultInputs>();
	const FJoltUpdatedMotionState* StartingSyncState = StartState.SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>();
	check(StartingSyncState);

	FJoltUpdatedMotionState& OutputSyncState = OutputState.SyncState.Collection.FindOrAddMutableDataByType<FJoltUpdatedMotionState>();

	const float DeltaSeconds = Params.TimeStep.StepMs * 0.001f;

	FJoltMovementRecord MoveRecord;
	MoveRecord.SetDeltaSeconds(DeltaSeconds);

	UJoltMoverBlackboard* SimBlackboard = MoverComp->GetSimBlackboard_Mutable();
	SimBlackboard->Invalidate(CommonBlackboard::LastFloorResult);	// flying = no valid floor
	SimBlackboard->Invalidate(CommonBlackboard::LastFoundDynamicMovementBase);

	OutputSyncState.MoveDirectionIntent = (ProposedMove.bHasDirIntent ? ProposedMove.DirectionIntent : FVector::ZeroVector);

	// Use the orientation intent directly. If no intent is provided, use last frame's orientation. Note that we are assuming rotation changes can't fail. 
	const FRotator StartingOrient = StartingSyncState->GetOrientation_WorldSpace();
	const FRotator TargetOrient = UJoltMovementUtils::ApplyAngularVelocityToRotator(StartingOrient, ProposedMove.AngularVelocityDegrees, DeltaSeconds);
	
	const FVector StartLocation = StartingSyncState->GetLocation_WorldSpace();
	const FVector TargetLocation = StartLocation + (ProposedMove.LinearVelocity * DeltaSeconds);

	const FQuat StartRotation = StartingOrient.Quaternion();
	FQuat TargetRotation = TargetOrient.Quaternion();
	if (CommonLegacySettings->bShouldRemainVertical)
	{
		TargetRotation = FRotationMatrix::MakeFromZX(MoverComp->GetUpDirection(), TargetRotation.GetForwardVector()).ToQuat();
	}

	FHitResult SweepHit(1.f);
	FJoltMovementRecord SweepRecord;
	SweepRecord.SetDeltaSeconds(DeltaSeconds);
	FVector LocationInProgress = StartLocation;
	FQuat   RotationInProgress = StartRotation;

	const bool bWouldMove = UJoltAsyncMovementUtils::TestDepenetratingMove(Params.MovingComps, StartLocation, TargetLocation, StartRotation, TargetRotation, /* bShouldSweep */ true, OUT SweepHit, SweepRecord);

	LocationInProgress = StartLocation + ((TargetLocation - StartLocation) * SweepHit.Time);
	RotationInProgress = FQuat::Slerp(StartRotation, TargetRotation, SweepHit.Time);

	if (SweepHit.IsValidBlockingHit())
	{
		const float PctOfTimeUsedForSliding = UJoltAsyncMovementUtils::TestSlidingMoveAlongHitSurface(Params.MovingComps, TargetLocation - StartLocation, LocationInProgress, TargetRotation, SweepHit, SweepRecord);

		if (PctOfTimeUsedForSliding > 0.f)
		{
			LocationInProgress = SweepHit.TraceStart + ((SweepHit.TraceEnd - SweepHit.TraceStart) * PctOfTimeUsedForSliding);
			RotationInProgress = FQuat::Slerp(RotationInProgress, TargetRotation, PctOfTimeUsedForSliding);
		}
	}

	if (bRespectDistanceOverWalkableSurfaces)
	{
		// If we are very close to a walkable surface, make sure we maintain a small gap over it
		FJoltFloorCheckResult FloorUnderActor;
		UJoltFloorQueryUtils::FindFloor(Params.MovingComps, CommonLegacySettings->FloorSweepDistance, CommonLegacySettings->MaxWalkSlopeCosine, CommonLegacySettings->bUseFlatBaseForFloorChecks, LocationInProgress, FloorUnderActor);

		if (FloorUnderActor.IsWalkableFloor())
		{
			LocationInProgress = UJoltGroundMovementUtils::TestMoveToKeepMinHeightAboveFloor(Params.MovingComps, LocationInProgress, RotationInProgress, CommonLegacySettings->MaxWalkSlopeCosine, IN OUT FloorUnderActor, MoveRecord);
		}
	}

	OutputSyncState.SetTransforms_WorldSpace(
		LocationInProgress,
		RotationInProgress.Rotator(),
		SweepRecord.GetRelevantVelocity(),
		ProposedMove.AngularVelocityDegrees,
		nullptr); // no movement base
}


void UJoltAsyncFlyingMode::OnRegistered(const FName ModeName)
{
	Super::OnRegistered(ModeName);

	CommonLegacySettings = GetMoverComponent()->FindSharedSettings<UJoltCommonLegacyMovementSettings>();
	ensureMsgf(CommonLegacySettings, TEXT("Failed to find instance of CommonLegacyMovementSettings on %s. Movement may not function properly."), *GetPathNameSafe(this));
}


void UJoltAsyncFlyingMode::OnUnregistered()
{
	CommonLegacySettings = nullptr;

	Super::OnUnregistered();
}