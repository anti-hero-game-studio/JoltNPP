// Fill out your copyright notice in the Description page of Project Settings.


#include "DefaultMovementSet/Modes/Physics/JoltPhysicsFallingMode.h"
#include "Components/SkeletalMeshComponent.h"
#include "JoltMoverComponent.h"
#include "MoveLibrary/JoltAirMovementUtils.h"
#include "MoveLibrary/JoltAsyncMovementUtils.h"
#include "MoveLibrary/JoltBasedMovementUtils.h"
#include "MoveLibrary/JoltGroundMovementUtils.h"
#include "MoveLibrary/JoltFloorQueryUtils.h"
#include "MoveLibrary/JoltMovementUtils.h"
#include "DefaultMovementSet/Settings/JoltCommonLegacyMovementSettings.h"

#include "DrawDebugHelpers.h"
#include "Core/Singletons/JoltPhysicsWorldSubsystem.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltPhysicsFallingMode)

UJoltPhysicsFallingMode::UJoltPhysicsFallingMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bCancelVerticalSpeedOnLanding(true)
	, AirControlPercentage(0.4f)
	, FallingDeceleration(200.0f)
	, OverTerminalSpeedFallingDeceleration(800.0f)
	, TerminalMovementPlaneSpeed(1500.0f)
	, bShouldClampTerminalVerticalSpeed(true)
	, VerticalFallingDeceleration(4000.0f)
	, TerminalVerticalSpeed(2000.0f)
{
	SharedSettingsClasses.Add(UJoltCommonLegacyMovementSettings::StaticClass());

	GameplayTags.AddTag(JoltMover_IsInAir);
	GameplayTags.AddTag(JoltMover_IsFalling);
	GameplayTags.AddTag(JoltMover_SkipVerticalAnimRootMotion);	// allows combination of gravity falling and root motion
}


void UJoltPhysicsFallingMode::GenerateMove_Implementation(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, FJoltProposedMove& OutProposedMove) const
{
	const UJoltMoverComponent* MoverComp = GetMoverComponent();
	const FJoltCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.Collection.FindDataByType<FJoltCharacterDefaultInputs>();
	const FJoltUpdatedMotionState* StartingSyncState = StartState.SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>();
	check(StartingSyncState);

	if (!CommonLegacySettings.IsValid())
	{
		return;
	}

	const float DeltaSeconds = TimeStep.StepMs * 0.001f;

	FVector UpDirection = MoverComp->GetUpDirection();
	
	// We don't want velocity limits to take the falling velocity component into account, since it is handled 
	//   separately by the terminal velocity of the environment.
	const FVector StartVelocity = StartingSyncState->GetVelocity_WorldSpace_Quantized();
	const FVector StartHorizontalVelocity =  FVector::VectorPlaneProject(StartVelocity, UpDirection);

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

	Params.MoveInput *= AirControlPercentage;
	// Don't care about up axis input since falling - if up input matters that should probably be a different movement mode
	Params.MoveInput = FVector::VectorPlaneProject(Params.MoveInput, UpDirection);
	
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
	
	Params.OrientationIntent = IntendedOrientation_WorldSpace;
	Params.PriorVelocity = StartHorizontalVelocity;
	Params.PriorOrientation = StartingSyncState->GetOrientation_WorldSpace_Quantized();
	Params.DeltaSeconds = DeltaSeconds;
	Params.TurningRate = CommonLegacySettings->TurningRate;
	Params.TurningBoost = CommonLegacySettings->TurningBoost;
	Params.MaxSpeed = CommonLegacySettings->MaxSpeed;
	Params.Acceleration = CommonLegacySettings->Acceleration;
	Params.Deceleration = FallingDeceleration;
	Params.WorldToGravityQuat = MoverComp->GetWorldToGravityTransform();
	Params.bUseAccelerationForVelocityMove = CommonLegacySettings->bUseAccelerationForVelocityMove;

	// Check if any current velocity values are over our terminal velocity - if so limit the move input in that direction and apply OverTerminalVelocityFallingDeceleration
	if (Params.MoveInput.Dot(StartVelocity) > 0 && StartHorizontalVelocity.Size() >= TerminalMovementPlaneSpeed)
	{
		Params.Deceleration = OverTerminalSpeedFallingDeceleration;
	}
	
	UJoltMoverBlackboard* SimBlackboard = MoverComp->GetSimBlackboard_Mutable();
	FJoltFloorCheckResult LastFloorResult;
	// limit our moveinput based on the floor we're on
	if (SimBlackboard && SimBlackboard->TryGet(CommonBlackboard::LastFloorResult, LastFloorResult))
	{
		if (LastFloorResult.HitResult.IsValidBlockingHit() && LastFloorResult.HitResult.Normal.Dot(UpDirection) > UE::JoltMoverUtils::VERTICAL_SLOPE_NORMAL_MAX_DOT && !LastFloorResult.IsWalkableFloor())
		{
			// If acceleration is into the wall, limit contribution.
			if (FVector::DotProduct(Params.MoveInput, LastFloorResult.HitResult.Normal) < 0.f)
			{
				// Allow movement parallel to the wall, but not into it because that may push us up.
				const FVector FallingHitNormal = FVector::VectorPlaneProject( LastFloorResult.HitResult.Normal, -UpDirection).GetSafeNormal();
				Params.MoveInput = FVector::VectorPlaneProject(Params.MoveInput, FallingHitNormal);
			}
		}
	}
	
	OutProposedMove = UJoltAirMovementUtils::ComputeControlledFreeMove(Params);
	const FVector VelocityWithGravity = StartVelocity + UJoltMovementUtils::ComputeVelocityFromGravity(MoverComp->GetGravityAcceleration(), DeltaSeconds);

	//  If we are going faster than TerminalVerticalVelocity apply VerticalFallingDeceleration otherwise reset Z velocity to before we applied deceleration 
	if (VelocityWithGravity.GetAbs().Dot(UpDirection) > TerminalVerticalSpeed)
	{
		if (bShouldClampTerminalVerticalSpeed)
		{
			const float ClampedVerticalSpeed = FMath::Sign(VelocityWithGravity.Dot(UpDirection)) * TerminalVerticalSpeed;
			UJoltMovementUtils::SetGravityVerticalComponent(OutProposedMove.LinearVelocity, ClampedVerticalSpeed, UpDirection);
		}
		else
		{
			float DesiredDeceleration = FMath::Abs(TerminalVerticalSpeed - VelocityWithGravity.GetAbs().Dot(UpDirection)) / DeltaSeconds;
			float DecelerationToApply = FMath::Min(DesiredDeceleration, VerticalFallingDeceleration);
			DecelerationToApply = FMath::Sign(VelocityWithGravity.Dot(UpDirection)) * DecelerationToApply * DeltaSeconds;
			FVector MaxUpDirVelocity = VelocityWithGravity * UpDirection - (UpDirection * DecelerationToApply);
			
			UJoltMovementUtils::SetGravityVerticalComponent(OutProposedMove.LinearVelocity, MaxUpDirVelocity.Dot(UpDirection), UpDirection);
		}
	}
	else
	{
		UJoltMovementUtils::SetGravityVerticalComponent(OutProposedMove.LinearVelocity, VelocityWithGravity.Dot(UpDirection), UpDirection);
	}
	
	
	UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>();
	const UJoltCommonLegacyMovementSettings* SharedSettingsPtr = GetMoverComponent<UJoltMoverComponent>()->FindSharedSettings<UJoltCommonLegacyMovementSettings>();
	if (SimBlackboard && Subsystem && SharedSettingsPtr)
	{
		FJoltFloorCheckResult FloorResult;
		FloorCheck(StartingSyncState->GetLocation_WorldSpace(), OutProposedMove.LinearVelocity,TimeStep.StepMs * 0.001f, FloorResult);
		
		SimBlackboard->Set(CommonBlackboard::LastFloorResult, FloorResult);
	}
}

void UJoltPhysicsFallingMode::SimulationTick_Implementation(const FJoltSimulationTickParams& Params, FJoltMoverTickEndData& OutputState)
{
	if (!CommonLegacySettings.IsValid())
	{
		return;
	}

	
	UJoltMoverComponent* MoverComponent = GetMoverComponent();
	const FJoltMoverTickStartData& StartState = Params.StartState;
	const UPrimitiveComponent* UpdatedComponent = Cast<UPrimitiveComponent>(Params.MovingComps.UpdatedComponent.Get());
	
	if (!UpdatedComponent) return;
	if (!MoverComponent) return;
	
	const UJoltMoverBlackboard* SimBlackboard = MoverComponent->GetSimBlackboard();
	if (!SimBlackboard) return;

	const FJoltProposedMove ProposedMove = Params.ProposedMove;

	const FJoltCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.Collection.FindDataByType<FJoltCharacterDefaultInputs>();
	const FJoltUpdatedMotionState* StartingSyncState = StartState.SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>();
	check(StartingSyncState);
	
	
	FJoltMoverTargetSyncState& OutputSyncState = OutputState.SyncState.Collection.FindOrAddMutableDataByType<FJoltMoverTargetSyncState>();
	
	FJoltFloorCheckResult FloorResult;
	if (!SimBlackboard->TryGet(CommonBlackboard::LastFloorResult, FloorResult))
	{
		FloorCheck(StartingSyncState->GetLocation_WorldSpace(), ProposedMove.LinearVelocity,Params.TimeStep.StepMs * 0.001f, FloorResult);
	}

	
	if (FloorResult.bBlockingHit)
	{
		// We are grounded and need to switch movement modes
		OutputState.MovementEndState.RemainingMs = 0.0f;
		OutputState.MovementEndState.NextModeName = DefaultModeNames::Walking;
		OutputSyncState.UpdateTargetVelocity(StartingSyncState->GetVelocity_WorldSpace_Quantized(), StartingSyncState->GetAngularVelocityDegrees_WorldSpace_Quantized());
		return;
	}

	
	const float DeltaSeconds = Params.TimeStep.StepMs * 0.001f;

	UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>();
	if (!Subsystem) return;
	
	// The physics simulation applies Z-only gravity acceleration via physics volumes, so we need to account for it here 
	const FVector TargetVel = ProposedMove.LinearVelocity - MoverComponent->GetGravityAcceleration() * FVector::UpVector;
	const FVector DeltaLinearVelocity = (TargetVel - StartingSyncState->GetVelocity_WorldSpace_Quantized()).GetClampedToMaxSize(TerminalVerticalSpeed) * DeltaSeconds;
	const FVector DeltaAngularVelocity = (ProposedMove.AngularVelocityDegrees - StartingSyncState->GetAngularVelocityDegrees_WorldSpace_Quantized()) * DeltaSeconds;

	OutputState.MovementEndState.RemainingMs = 0.0f;
	OutputState.MovementEndState.NextModeName = Params.StartState.SyncState.MovementMode;
	OutputSyncState.UpdateTargetVelocity(DeltaLinearVelocity, DeltaAngularVelocity);
}


void UJoltPhysicsFallingMode::OnRegistered(const FName ModeName)
{
	Super::OnRegistered(ModeName);

	CommonLegacySettings = GetMoverComponent()->FindSharedSettings<UJoltCommonLegacyMovementSettings>();
	ensureMsgf(CommonLegacySettings.IsValid(), TEXT("Failed to find instance of CommonLegacyMovementSettings on %s. Movement may not function properly."), *GetPathNameSafe(this));
}


void UJoltPhysicsFallingMode::OnUnregistered()
{
	CommonLegacySettings = nullptr;

	Super::OnUnregistered();
}
