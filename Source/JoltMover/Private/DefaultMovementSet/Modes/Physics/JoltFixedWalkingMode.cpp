// Fill out your copyright notice in the Description page of Project Settings.


#include "DefaultMovementSet/Modes/Physics/JoltFixedWalkingMode.h"
#include "JoltMoverComponent.h"
#include "Core/Interfaces/JoltPrimitiveComponentInterface.h"
#include "Core/Singletons/JoltPhysicsWorldSubsystem.h"
#include "DefaultMovementSet/Settings/JoltCommonLegacyMovementSettings.h"
#include "MoveLibrary/JoltGroundMovementUtils.h"
#include "MoveLibrary/JoltMovementUtils.h"

static bool TryGetCapsuleSizeCm(const UPrimitiveComponent* UpdatedComponent, float& OutHalfHeightCm, float& OutRadiusCm)
{
	if (!UpdatedComponent) return false;

	const UWorld* World = UpdatedComponent->GetWorld();
	if (!World) return false;

	const UJoltPhysicsWorldSubsystem* Subsystem = World->GetSubsystem<UJoltPhysicsWorldSubsystem>();
	if (!Subsystem) return false;
	
	if (const FJoltUserData* UserData = Subsystem->GetUserData(UpdatedComponent))
	{
		OutRadiusCm = UserData->ShapeRadius;
		OutHalfHeightCm = UserData->ShapeHeight;
		return true;
	}
	return false;
}

void UJoltFixedWalkingMode::GenerateMove_Implementation(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, FJoltProposedMove& OutProposedMove) const
{
	const UJoltMoverComponent* MoverComp = GetMoverComponent();
	if (!MoverComp) return;
	const UPrimitiveComponent* UpdatedComponent = MoverComp->GetUpdatedComponent<UPrimitiveComponent>();
	if (!UpdatedComponent) return;
	const FJoltCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.Collection.FindDataByType<FJoltCharacterDefaultInputs>();
	const FJoltUpdatedMotionState* StartingSyncState = StartState.SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>();
	check(StartingSyncState);

	if (!CommonLegacySettings)
	{
		return;
	}

	const float DeltaSeconds = TimeStep.StepMs * 0.001f;

	UJoltMoverBlackboard* SimBlackboard = MoverComp->GetSimBlackboard_Mutable();
	FVector UpDirection = MoverComp->GetUpDirection();
	FVector MovementNormal = UpDirection;
	

	FRotator IntendedOrientation_WorldSpace;
	// If there's no intent from input to change orientation, use the current orientation
	if (!CharacterInputs || CharacterInputs->OrientationIntent.IsNearlyZero())
	{
		IntendedOrientation_WorldSpace = StartingSyncState->GetOrientation_WorldSpace_Quantized();
	}
	else
	{
		IntendedOrientation_WorldSpace = CharacterInputs->GetOrientationIntentDir_WorldSpace().ToOrientationRotator();
	}

	IntendedOrientation_WorldSpace = UJoltMovementUtils::ApplyGravityToOrientationIntent(IntendedOrientation_WorldSpace, MoverComp->GetWorldToGravityTransform(), CommonLegacySettings->bShouldRemainVertical);
	
	FJoltGroundMoveParams Params;

	if (CharacterInputs)
	{
		Params.MoveInputType = CharacterInputs->GetMoveInputType();

		const bool bMaintainInputMagnitude = true;
		Params.MoveInput = UJoltPlanarConstraintUtils::ConstrainDirectionToPlane(MoverComp->GetPlanarConstraint(), CharacterInputs->GetMoveInput_WorldSpace(), bMaintainInputMagnitude);
	}
	else
	{
		if (MoverComp->GetOwnerRole() == ROLE_SimulatedProxy)
		{
			Params.MoveInputType = EJoltMoveInputType::Velocity;

			const bool bMaintainInputMagnitude = true;
			Params.MoveInput = UJoltPlanarConstraintUtils::ConstrainDirectionToPlane(MoverComp->GetPlanarConstraint(), StartingSyncState->GetVelocity_WorldSpace_Quantized().GetSafeNormal(), bMaintainInputMagnitude);
		}
	}

	Params.OrientationIntent = IntendedOrientation_WorldSpace;
	Params.PriorVelocity = FVector::VectorPlaneProject(StartingSyncState->GetVelocity_WorldSpace_Quantized(), MovementNormal);
	Params.PriorOrientation = StartingSyncState->GetOrientation_WorldSpace_Quantized();
	Params.GroundNormal = MovementNormal;
	Params.TurningRate = CommonLegacySettings->TurningRate;
	Params.TurningBoost = CommonLegacySettings->TurningBoost;
	Params.MaxSpeed = CommonLegacySettings->MaxSpeed;
	Params.Acceleration = CommonLegacySettings->Acceleration;
	Params.Deceleration = CommonLegacySettings->Deceleration;
	Params.DeltaSeconds = DeltaSeconds;
	Params.WorldToGravityQuat = MoverComp->GetWorldToGravityTransform();
	Params.UpDirection = UpDirection;
	Params.bUseAccelerationForVelocityMove = CommonLegacySettings->bUseAccelerationForVelocityMove;
	
	if (Params.MoveInput.SizeSquared() > 0.f && !UJoltMovementUtils::IsExceedingMaxSpeed(Params.PriorVelocity, CommonLegacySettings->MaxSpeed))
	{
		Params.Friction = CommonLegacySettings->GroundFriction;
	}
	else
	{
		Params.Friction = CommonLegacySettings->bUseSeparateBrakingFriction ? CommonLegacySettings->BrakingFriction : CommonLegacySettings->GroundFriction;
		Params.Friction *= CommonLegacySettings->BrakingFrictionFactor;
	}
	
	OutProposedMove = UJoltGroundMovementUtils::ComputeControlledGroundMove(Params);
}

void UJoltFixedWalkingMode::SimulationTick_Implementation(const FJoltSimulationTickParams& Params, FJoltMoverTickEndData& OutputState)
{
	if (!CommonLegacySettings)
	{
		return;
	}

	UJoltMoverComponent* MoverComp = GetMoverComponent();
	const FJoltMoverTickStartData& StartState = Params.StartState;
	FJoltProposedMove ProposedMove = Params.ProposedMove;

	const FJoltCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.Collection.FindDataByType<FJoltCharacterDefaultInputs>();
	const FJoltUpdatedMotionState* StartingSyncState = StartState.SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>();
	const FJoltMoverTargetSyncState* StartingTargetState = StartState.SyncState.Collection.FindDataByType<FJoltMoverTargetSyncState>();
	check(StartingSyncState);

	FJoltUpdatedMotionState& OutputSyncState = OutputState.SyncState.Collection.FindOrAddMutableDataByType<FJoltUpdatedMotionState>();
	
	FJoltMoverTargetSyncState& OutputTargetState = OutputState.SyncState.Collection.FindOrAddMutableDataByType<FJoltMoverTargetSyncState>();
	OutputTargetState = *StartingTargetState;


	const float DeltaSeconds = Params.TimeStep.StepMs * 0.001f;

	const FVector OrigMoveDelta = ProposedMove.LinearVelocity * DeltaSeconds;

	const FVector StartLocation = StartingSyncState->GetLocation_WorldSpace();
	const FVector TargetLocation = StartLocation + OrigMoveDelta;


	FJoltMovementRecord MoveRecord;
	MoveRecord.SetDeltaSeconds(DeltaSeconds);

	FJoltFloorCheckResult CurrentFloor;
	UJoltMoverBlackboard* SimBlackboard = MoverComp->GetSimBlackboard_Mutable();

	FVector UpDirection = MoverComp->GetUpDirection();
	
	OutputSyncState.MoveDirectionIntent = (ProposedMove.bHasDirIntent ? ProposedMove.DirectionIntent : FVector::ZeroVector);

	const FRotator StartingOrient = StartingSyncState->GetOrientation_WorldSpace_Quantized();
	const FRotator TargetOrient = UJoltMovementUtils::ApplyAngularVelocityToRotator(StartingOrient, ProposedMove.AngularVelocityDegrees, DeltaSeconds);
	const bool bIsOrientationChanging = !StartingOrient.Equals(TargetOrient);

	const FQuat StartRotation = StartingOrient.Quaternion();

	FQuat TargetRotation = TargetOrient.Quaternion();
	if (CommonLegacySettings->bShouldRemainVertical)
	{
		TargetRotation = FRotationMatrix::MakeFromZX(UpDirection, TargetRotation.GetForwardVector()).ToQuat();
	}

	FVector LocationInProgress = StartLocation;
	FQuat   RotationInProgress = StartRotation;

	FHitResult MoveHitResult(1.f);
	
	FVector CurMoveDelta = OrigMoveDelta;
	
	const UJoltMoverComponent* MoverCompConst = GetMoverComponent();
	UPrimitiveComponent* UpdatedComponent = MoverCompConst ? MoverCompConst->GetUpdatedComponent<UPrimitiveComponent>() : nullptr;
	if (!UpdatedComponent) return;

	const FVector CurrentVelocity = StartingSyncState->GetVelocity_WorldSpace_Quantized();

	// --- 1) Planar target velocity from ProposedMove (fixes “walking stuck”) ---
	const FVector ProposedPlanarVelocity = ProposedMove.LinearVelocity - ProposedMove.LinearVelocity.ProjectOnToNormal(UpDirection);

	// Preserve existing vertical component for now; suspension will adjust it
	FVector TargetVelocity = ProposedPlanarVelocity + CurrentVelocity.ProjectOnToNormal(UpDirection);

	// --- 2) Compute capsule base height above plane Z=0 (or FloorPlaneZ) ---
	float CapsuleHalfHeightCm = 0.0f;
	float CapsuleRadiusCm = 0.0f;
	if (!TryGetCapsuleSizeCm(UpdatedComponent, CapsuleHalfHeightCm, CapsuleRadiusCm))
	{
	    // If your updated component is not a UCapsuleComponent, replace this with your own geometry source.
	    UE_LOG(LogJoltMover, Error, TEXT("Walking hover requires a capsule UpdatedComponent or a capsule size source."));
	    return;
	}

	// Base of capsule along UpDirection: location minus (halfHeight - radius)
	const FVector Location = StartingSyncState->GetLocation_WorldSpace();
	const FVector CapsuleBaseWS = Location - UpDirection * (CapsuleHalfHeightCm - CapsuleRadiusCm);

	// For plane Z = FloorPlaneZ, the “height” in cm is simply the world Z offset.
	// (If UpDirection is guaranteed (0,0,1), this is exact; otherwise use dot vs UpDirection and a plane point.)
	const float CapsuleBaseHeightCm = CapsuleBaseWS.Z;
	const float DesiredCapsuleBaseHeightCm = FloorPlaneZ + TargetHoverHeight;

	// Height error: positive => capsule is too low => push upward
	float HeightErrorCm = DesiredCapsuleBaseHeightCm - CapsuleBaseHeightCm;

	// Deadzone to avoid micro-jitter
	if (FMath::Abs(HeightErrorCm) <= HoverHeightTolerance)
	{
	    HeightErrorCm = 0.0f;
	}

	// --- 3) Suspension vertical correction (spring-damper in velocity space) ---
	const float CurrentUpSpeedCmPerSec = FVector::DotProduct(CurrentVelocity, UpDirection);

	// Desired acceleration along UpDirection
	float DesiredUpwardAccelerationCmPerSec2 = (SuspensionStiffness * HeightErrorCm) - (SuspensionDamping * CurrentUpSpeedCmPerSec);

	// Clamp accel
	DesiredUpwardAccelerationCmPerSec2 = FMath::Clamp(
	    DesiredUpwardAccelerationCmPerSec2,
	    -MaxDownwardAcceleration,
	    MaxUpwardAcceleration);

	// Convert to delta-v for this step
	float DeltaUpSpeedCmPerSec = DesiredUpwardAccelerationCmPerSec2 * DeltaSeconds;

	// Clamp delta-v per step (primary “no pop” control)
	DeltaUpSpeedCmPerSec = FMath::Clamp(
	    DeltaUpSpeedCmPerSec,
	    -MaxDownwardVelocityChangePerStep,
	    MaxUpwardVelocityChangePerStep);

	// --- 4) Pop suppression: cancel upward velocity while “supported” ---
	const bool bIsSupported = true; // flat floor, always supported unless you implement explicit jump disabling
	if (bIsSupported && (CurrentUpSpeedCmPerSec > CancelUpwardVelocityWhenSupportedThreshold))
	{
	    const float UpwardExcess = CurrentUpSpeedCmPerSec - CancelUpwardVelocityWhenSupportedThreshold;
	    const float CancelAmount = FMath::Min(UpwardExcess, MaxUpwardVelocityCancelPerStep);
	    DeltaUpSpeedCmPerSec -= CancelAmount;
	}

	// Apply vertical correction
	TargetVelocity += UpDirection * DeltaUpSpeedCmPerSec;

	// --- 5) Output target velocity and orientation ---
	OutputTargetState.UpdateTargetVelocity(TargetVelocity, ProposedMove.AngularVelocityDegrees);
	OutputState.MovementEndState.RemainingMs = 0.0f;
	OutputState.MovementEndState.NextModeName = Params.StartState.SyncState.MovementMode;
	OutputSyncState.MoveDirectionIntent = ProposedMove.bHasDirIntent ? ProposedMove.DirectionIntent : FVector::ZeroVector;
}

void UJoltFixedWalkingMode::OnRegistered(const FName ModeName)
{
	Super::OnRegistered(ModeName);
	
	CommonLegacySettings = GetMoverComponent()->FindSharedSettings<UJoltCommonLegacyMovementSettings>();
	ensureMsgf(CommonLegacySettings, TEXT("Failed to find instance of CommonLegacyMovementSettings on %s. Movement may not function properly."), *GetPathNameSafe(this));
}

void UJoltFixedWalkingMode::OnUnregistered()
{
	CommonLegacySettings = nullptr;
	Super::OnUnregistered();
}
