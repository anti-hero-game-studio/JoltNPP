// Fill out your copyright notice in the Description page of Project Settings.


#include "DefaultMovementSet/Modes/Physics/JoltFloatingWalkingMode.h"
#include "DefaultMovementSet/Settings/JoltCommonLegacyMovementSettings.h"
#include "JoltMoverComponent.h"
#include "Core/Interfaces/JoltPrimitiveComponentInterface.h"
#include "Core/Singletons/JoltPhysicsWorldSubsystem.h"
#include "MoveLibrary/JoltGroundMovementUtils.h"
#include "MoveLibrary/JoltMovementUtils.h"

void UJoltFloatingWalkingMode::OnRegistered(const FName ModeName)
{
	Super::OnRegistered(ModeName);

	CommonLegacySettings = GetMoverComponent()->FindSharedSettings<UJoltCommonLegacyMovementSettings>();
	ensureMsgf(CommonLegacySettings.IsValid(), TEXT("Failed to find instance of CommonLegacyMovementSettings on %s. Movement may not function properly."), *GetPathNameSafe(this));
}

void UJoltFloatingWalkingMode::OnUnregistered()
{
	CommonLegacySettings = nullptr;

	Super::OnUnregistered();
}


void UJoltFloatingWalkingMode::GenerateMove_Implementation(const FJoltMoverTickStartData& StartState,
	const FJoltMoverTimeStep& TimeStep, FJoltProposedMove& OutProposedMove) const
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
	FJoltFloorCheckResult LastFloorResult;
	FVector MovementNormal;

	UJoltMoverBlackboard* SimBlackboard = MoverComp->GetSimBlackboard_Mutable();
	FVector UpDirection = MoverComp->GetUpDirection();

	// Try to use the floor as the basis for the intended move direction (i.e. try to walk along slopes, rather than into them)
	if (SimBlackboard && SimBlackboard->TryGet(CommonBlackboard::LastFloorResult, LastFloorResult) && LastFloorResult.IsWalkableFloor())
	{
		MovementNormal = LastFloorResult.HitResult.ImpactNormal;
	}
	else
	{
		MovementNormal = UpDirection;
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
	
	FJoltGroundMoveParams Params;

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

	Params.OrientationIntent = IntendedOrientation_WorldSpace;
	Params.PriorVelocity = FVector::VectorPlaneProject(StartingSyncState->GetVelocity_WorldSpace(), MovementNormal);
	Params.PriorOrientation = StartingSyncState->GetOrientation_WorldSpace();
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

void UJoltFloatingWalkingMode::SimulationTick_Implementation(const FJoltSimulationTickParams& Params, FJoltMoverTickEndData& OutputState)
{
	
	if (!CommonLegacySettings.IsValid())
	{
		return;
	}

	UJoltMoverComponent* MoverComp = GetMoverComponent();
	if (!MoverComp) return;
	
	const FJoltMoverTickStartData& StartState = Params.StartState;
	
	UPrimitiveComponent* P = MoverComp->GetJoltPhysicsBodyComponent();
	if (!P) return;
	
	IJoltPrimitiveComponentInterface* I = Cast<IJoltPrimitiveComponentInterface>(P);	
	if (!I) return;

	FJoltProposedMove ProposedMove = Params.ProposedMove;
	const FJoltCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.Collection.FindDataByType<FJoltCharacterDefaultInputs>();
	const FJoltUpdatedMotionState* StartingSyncState = StartState.SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>();
	check(StartingSyncState);

	FJoltUpdatedMotionState& OutputSyncState = OutputState.SyncState.Collection.FindOrAddMutableDataByType<FJoltUpdatedMotionState>();


	const float DeltaSeconds = Params.TimeStep.StepMs * 0.001f;


	if (UJoltPhysicsWorldSubsystem* S = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>())
	{
		
		JPH::Body* MyBody = S->GetRigidBody(P);
		if (!MyBody) return;
		
		FTransform T;
		FVector V,A,F;
		S->GetPhysicsState(P,T, V, A, F);
		const FVector Start = T.GetLocation();
		const FVector End = Start + (-MoverComp->GetUpDirection() * 200.f);
		int32 HitBodyId;
		const FHitResult& Hit = S->LineTraceSingleByChannel(Start, End, ECC_WorldStatic, {MoverComp->GetOwner()}, HitBodyId );


		if (Hit.bBlockingHit)
		{
			FVector Velocity = StartingSyncState->GetVelocity_WorldSpace_Quantized();
			FVector RayDir = -MoverComp->GetUpDirection();
			
			FVector OtherVelocity = FVector::Zero();
			JPH::Body* Body = S->GetRigidBody(Hit);
			if (Body)
			{
				OtherVelocity = S->GetVelocity(Body->GetID());
			}
			
			float RayDirectionalVelocity = RayDir.Dot(Velocity);
			float OtherDirectionalVelocity = RayDir.Dot(OtherVelocity);
			
			float RelativeVelocity = RayDirectionalVelocity - OtherDirectionalVelocity;
			float X = Hit.Distance - RideHeight;
			
			float SpringForce = (X * RideSpringStrength) - (RelativeVelocity * RideSpringDamper);
			
			const FVector VelocityWithSpring = RayDir * SpringForce;
			
			
			
			OutputSyncState.SetLinearAndAngularVelocity_WorldSpace(ProposedMove.LinearVelocity + VelocityWithSpring, ProposedMove.AngularVelocityDegrees);
		}
		else
		{
			OutputState.MovementEndState.NextModeName = CommonLegacySettings->AirMovementModeName;
			OutputState.MovementEndState.RemainingMs = Params.TimeStep.StepMs - (Params.TimeStep.StepMs * Hit.Time);
		}
	}
	else
	{
		OutputSyncState = *StartingSyncState;
	}
	
	
}
