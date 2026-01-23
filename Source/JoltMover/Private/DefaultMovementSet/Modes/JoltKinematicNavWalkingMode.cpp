// Copyright Epic Games, Inc. All Rights Reserved.


#include "DefaultMovementSet/Modes/JoltKinematicNavWalkingMode.h"
#include "JoltMoverComponent.h"
#include "NavigationSystem.h"
#include "AI/NavigationSystemBase.h"
#include "AI/Navigation/NavigationDataInterface.h"
#include "AI/Navigation/PathFollowingAgentInterface.h"
#include "Components/ShapeComponent.h"
#include "DefaultMovementSet/NavJoltMoverComponent.h"
#include "DefaultMovementSet/Settings/JoltCommonLegacyMovementSettings.h"
#include "MoveLibrary/JoltFloorQueryUtils.h"
#include "MoveLibrary/JoltGroundMovementUtils.h"
#include "MoveLibrary/JoltModularMovement.h"
#include "MoveLibrary/JoltMovementUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltKinematicNavWalkingMode)


UJoltKinematicNavWalkingMode::UJoltKinematicNavWalkingMode()
	: bSweepWhileNavWalking(true)
	, bProjectNavMeshWalking(false)
	, NavMeshProjectionHeightScaleUp(0.67f)
	, NavMeshProjectionHeightScaleDown(1.0f)
	, NavMeshProjectionInterval(0.1f)
	, NavMeshProjectionInterpSpeed(12.f)
	, NavMeshProjectionTimer(0)
	, NavMoverComponent(nullptr)
	, NavDataInterface(nullptr)
	, bProjectNavMeshOnBothWorldChannels(true)
{
	SharedSettingsClasses.Add(UJoltCommonLegacyMovementSettings::StaticClass());
	
	GameplayTags.AddTag(JoltMover_IsOnGround);
	GameplayTags.AddTag(JoltMover_IsNavWalking);
}

void UJoltKinematicNavWalkingMode::GenerateMove_Implementation(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, FJoltProposedMove& OutProposedMove) const
{
	const UJoltMoverComponent* MoverComp = GetMoverComponent();
	const FJoltCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.Collection.FindDataByType<FJoltCharacterDefaultInputs>();
	const FJoltUpdatedMotionState* StartingSyncState = StartState.SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>();
	check(StartingSyncState);

	const float DeltaSeconds = TimeStep.StepMs * 0.001f;
	FJoltFloorCheckResult LastFloorResult;
	FVector MovementNormal;
	FVector UpDirection = MoverComp->GetUpDirection();

	UJoltMoverBlackboard* SimBlackboard = MoverComp->GetSimBlackboard_Mutable();

	// Try to use the floor as the basis for the intended move direction (i.e. try to walk along slopes, rather than into them)
	if (SimBlackboard && SimBlackboard->TryGet(CommonBlackboard::LastFloorResult, LastFloorResult) && LastFloorResult.IsWalkableFloor())
	{
		MovementNormal = LastFloorResult.HitResult.ImpactNormal;
	}
	else
	{
		MovementNormal = MoverComp->GetUpDirection();
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
		Params.MoveInput = CharacterInputs->GetMoveInput_WorldSpace();
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

	if (TurnGenerator)
	{
		OutProposedMove.AngularVelocityDegrees = IJoltTurnGeneratorInterface::Execute_GetTurn(TurnGenerator, IntendedOrientation_WorldSpace, StartState, *StartingSyncState, TimeStep, OutProposedMove, SimBlackboard);
	}
}

void UJoltKinematicNavWalkingMode::SimulationTick_Implementation(const FJoltSimulationTickParams& Params, FJoltMoverTickEndData& OutputState)
{
	const UJoltMoverComponent* MoverComp = Cast<UJoltMoverComponent>(GetMoverComponent());
	if (!ensureMsgf(MoverComp, TEXT("Nav Walking Mode couldn't find a valid MoverComponent!")))
	{
		return;
	}
	
	const FJoltMoverTickStartData& StartState = Params.StartState;
	USceneComponent* UpdatedComponent = Params.MovingComps.UpdatedComponent.Get();
	UPrimitiveComponent* UpdatedPrimitive = Params.MovingComps.UpdatedPrimitive.Get();
	const FJoltProposedMove& ProposedMove = Params.ProposedMove;
	const FVector UpDirection = MoverComp->GetUpDirection();

	if (!UpdatedComponent || !UpdatedPrimitive)
	{
		return;
	}

	const FJoltCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.Collection.FindDataByType<FJoltCharacterDefaultInputs>();
	const FJoltUpdatedMotionState* StartingSyncState = StartState.SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>();
	check(StartingSyncState);

	FJoltUpdatedMotionState& OutputSyncState = OutputState.SyncState.Collection.FindOrAddMutableDataByType<FJoltUpdatedMotionState>();
	
	const float DeltaSeconds = Params.TimeStep.StepMs * 0.001f;
	const FVector OrigMoveDelta = ProposedMove.LinearVelocity * DeltaSeconds;

	TObjectPtr<AActor> OwnerActor = UpdatedComponent->GetOwner();
	check(OwnerActor);

	FJoltMovementRecord MoveRecord;
	MoveRecord.SetDeltaSeconds(DeltaSeconds);

	OutputSyncState.MoveDirectionIntent = (ProposedMove.bHasDirIntent ? ProposedMove.DirectionIntent : FVector::ZeroVector);

	const FRotator StartingOrient = StartingSyncState->GetOrientation_WorldSpace();
	const FRotator TargetOrient = UJoltMovementUtils::ApplyAngularVelocityToRotator(StartingOrient, ProposedMove.AngularVelocityDegrees, DeltaSeconds);
	const bool bIsOrientationChanging = !StartingOrient.Equals(TargetOrient);
	
	FQuat TargetOrientQuat = TargetOrient.Quaternion();
	if (CommonLegacySettings->bShouldRemainVertical)
	{
		TargetOrientQuat = FRotationMatrix::MakeFromZX(UpDirection, TargetOrientQuat.GetForwardVector()).ToQuat();
	}
	
	FVector StartingLocation = NavMoverComponent->GetFeetLocation();
	FVector AdjustedDest = StartingLocation + OrigMoveDelta;

	const bool bDeltaMoveNearlyZero = OrigMoveDelta.IsNearlyZero();
	FNavLocation DestNavLocation;

	float SimpleRadius = 0;
	float SimpleHalfHeight = 0;
	NavMoverComponent->GetSimpleCollisionCylinder(SimpleRadius, SimpleHalfHeight);
	
	if (!NavDataInterface.IsValid())
	{
		NavDataInterface = GetNavData();
	}
	
	bool bSameNavLocation = false;
	if (CachedNavLocation.NodeRef != INVALID_NAVNODEREF)
	{
		if (bProjectNavMeshWalking)
		{
			const float DistSq = UJoltMovementUtils::ProjectToGravityFloor(StartingLocation - CachedNavLocation.Location, UpDirection).SizeSquared();
			const float DistDot = FMath::Abs((StartingLocation - CachedNavLocation.Location).Dot(UpDirection));
			
			const float TotalCapsuleHeight = SimpleHalfHeight * 2.0f;
			const float ProjectionScale = (StartingLocation.Dot(UpDirection) > CachedNavLocation.Location.Dot(UpDirection)) ? NavMeshProjectionHeightScaleUp : NavMeshProjectionHeightScaleDown;
			const float DistThr = TotalCapsuleHeight * FMath::Max(0.f, ProjectionScale);

			bSameNavLocation = (DistSq <= UE_KINDA_SMALL_NUMBER) && (DistDot < DistThr);
		}
		else
		{
			bSameNavLocation = CachedNavLocation.Location.Equals(StartingLocation);
		}

		if (bDeltaMoveNearlyZero && bSameNavLocation)
		{
			if (NavDataInterface.IsValid())
			{
				if (!NavDataInterface->IsNodeRefValid(CachedNavLocation.NodeRef))
				{
					CachedNavLocation.NodeRef = INVALID_NAVNODEREF;
					bSameNavLocation = false;
				}
			}
		}
	}

	if (bDeltaMoveNearlyZero && bSameNavLocation)
	{
		DestNavLocation = CachedNavLocation;
		UE_LOG(LogJoltMover, VeryVerbose, TEXT("%s using cached navmesh location! (bProjectNavMeshWalking = %d)"), *GetNameSafe(GetMoverComponent()->GetOwner()), bProjectNavMeshWalking);
	}
	else
	{
		// Start the trace from the vertical location of the last valid trace.
		// Otherwise if we are projecting our location to the underlying geometry and it's far above or below the navmesh,
		// we'll follow that geometry's plane out of range of valid navigation.
		if (bSameNavLocation && bProjectNavMeshWalking)
		{
			UJoltMovementUtils::SetGravityVerticalComponent(AdjustedDest, CachedNavLocation.Location.Dot(UpDirection), UpDirection);
		}
		
		// Find the point on the NavMesh
		bool bFoundPointOnNavMesh = false;

		if (NavDataInterface.IsValid())
		{
			const IPathFollowingAgentInterface* PathFollowingAgent = NavMoverComponent->GetPathFollowingAgent();
			const bool bIsOnNavLink = PathFollowingAgent && PathFollowingAgent->IsFollowingNavLink();
			
			if (bSlideAlongNavMeshEdge && !bIsOnNavLink)
			{
				FNavLocation StartingNavFloorLocation;
				bool bHasValidCachedNavLocation = NavDataInterface->IsNodeRefValid(CachedNavLocation.NodeRef);

				// If we don't have a valid CachedNavLocation lets try finding the NavFloor where we're currently at and use that otherwise we can just use our CachedNavLocation
				if (!bHasValidCachedNavLocation)
				{
					bHasValidCachedNavLocation = FindNavFloor(StartingLocation, OUT StartingNavFloorLocation, NavDataInterface.Get());
				}
				else
				{
					StartingNavFloorLocation = CachedNavLocation;
				}

				if (bHasValidCachedNavLocation)
				{
					bFoundPointOnNavMesh = NavDataInterface->FindMoveAlongSurface(StartingNavFloorLocation, AdjustedDest, OUT DestNavLocation);

					if (bFoundPointOnNavMesh)
					{
						AdjustedDest = UJoltMovementUtils::ProjectToGravityFloor(DestNavLocation.Location, UpDirection) + UJoltMovementUtils::GetGravityVerticalComponent(AdjustedDest, UpDirection);
					}
				}
			}
			else
			{
				bFoundPointOnNavMesh = FindNavFloor(AdjustedDest, DestNavLocation, NavDataInterface.Get());
			}
		}


		if (!bFoundPointOnNavMesh)
		{
			// Can't find nav mesh at this location, so we need to do something else
			switch (BehaviorOffNavMesh)
			{
				default:	// fall through
				case EJoltOffNavMeshBehavior::SwitchToWalking:
					UE_LOG(LogJoltMover, Verbose, TEXT("%s could not find valid navigation data at location %s. Switching to walking mode."), *GetNameSafe(MoverComp->GetOwner()), *AdjustedDest.ToCompactString());
					OutputState.MovementEndState.NextModeName = DefaultModeNames::Walking;
					OutputState.MovementEndState.RemainingMs = Params.TimeStep.StepMs;
					MoveRecord.SetDeltaSeconds(0.0f);
					break;

				case EJoltOffNavMeshBehavior::MoveWithoutNavMesh:
					// allow the full move to occur 
					// TODO: Need to actually move the updated component and add the movement to the move record
					ensureMsgf(false, TEXT("NavWalkingMode does not yet support MoveWithoutNavMesh"));
					break;

				case EJoltOffNavMeshBehavior::DoNotMove:
					UE_LOG(LogJoltMover, Verbose, TEXT("%s could not find valid navigation data at location %s. Cannot move."), *GetNameSafe(MoverComp->GetOwner()), *AdjustedDest.ToCompactString());
					// nothing to be done
					break;

				case EJoltOffNavMeshBehavior::RotateOnly:
					FHitResult MoveHitResult;
					UJoltMovementUtils::TrySafeMoveUpdatedComponent(Params.MovingComps, FVector::ZeroVector, TargetOrientQuat, /*bSweep?*/ false, MoveHitResult, ETeleportType::None, MoveRecord);
					break;
			}

			CaptureFinalState(UpdatedComponent, MoveRecord, ProposedMove.AngularVelocityDegrees, OutputSyncState);
			return;
		}

		CachedNavLocation = DestNavLocation;
	}
	
	if (DestNavLocation.NodeRef != INVALID_NAVNODEREF)
	{
		FVector NewLocation = UJoltMovementUtils::ProjectToGravityFloor(AdjustedDest, UpDirection) + UJoltMovementUtils::GetGravityVerticalComponent(DestNavLocation.Location, UpDirection);
		if (bProjectNavMeshWalking)
		{
			const float TotalCapsuleHeight = SimpleHalfHeight * 2.0f;
			const float UpOffset = TotalCapsuleHeight * FMath::Max(0.f, NavMeshProjectionHeightScaleUp);
			const float DownOffset = TotalCapsuleHeight * FMath::Max(0.f, NavMeshProjectionHeightScaleDown);
			NewLocation = ProjectLocationFromNavMesh(DeltaSeconds, StartingLocation, NewLocation, UpOffset, DownOffset);
		}
		else
		{
			if (UJoltMoverBlackboard* SimBlackboard = MoverComp->GetSimBlackboard_Mutable())
			{
				const FJoltFloorCheckResult EmptyFloorCheckResult;
				SimBlackboard->Set(CommonBlackboard::LastFloorResult, EmptyFloorCheckResult);
			}	
		}

		FVector AdjustedDelta = NewLocation - StartingLocation;
		
		if (!AdjustedDelta.IsNearlyZero() || bIsOrientationChanging)
		{
			FHitResult MoveHitResult;
			UJoltMovementUtils::TrySafeMoveUpdatedComponent(Params.MovingComps, AdjustedDelta, TargetOrientQuat, bSweepWhileNavWalking, MoveHitResult, ETeleportType::None, MoveRecord);
		}
	}
	else
	{
		// Can't find nav destination, so revert to a different mode and let it process the intended movement
		OutputState.MovementEndState.NextModeName = CommonLegacySettings->AirMovementModeName;
		OutputState.MovementEndState.RemainingMs = Params.TimeStep.StepMs;
		MoveRecord.SetDeltaSeconds(0.0f);
	}

	CaptureFinalState(UpdatedComponent, MoveRecord, ProposedMove.AngularVelocityDegrees, OutputSyncState);
}

bool UJoltKinematicNavWalkingMode::FindNavFloor(const FVector& TestLocation, FNavLocation& OutNavFloorLocation, const INavigationDataInterface* NavData) const
{
	if (NavData == nullptr || NavMoverComponent == nullptr)
	{
		return false;
	}

	const FNavAgentProperties& AgentProps = NavMoverComponent->GetNavAgentPropertiesRef();
	const float SearchRadius = AgentProps.AgentRadius * 2.0f;
	const float SearchHeight = AgentProps.AgentHeight * AgentProps.NavWalkingSearchHeightScale;

	return NavData->ProjectPoint(TestLocation, OutNavFloorLocation, FVector(SearchRadius, SearchRadius, SearchHeight));
}

UObject* UJoltKinematicNavWalkingMode::GetTurnGenerator()
{
	return TurnGenerator;
}

void UJoltKinematicNavWalkingMode::SetTurnGeneratorClass(TSubclassOf<UObject> TurnGeneratorClass)
{
	if (TurnGeneratorClass)
	{
		TurnGenerator = NewObject<UObject>(this, TurnGeneratorClass);
	}
	else
	{
		TurnGenerator = nullptr; // Clearing the turn generator is valid - will go back to the default turn generation
	}
}

void UJoltKinematicNavWalkingMode::SetCollisionForNavWalking(bool bEnable)
{
	if (const UJoltMoverComponent* MoverComponent = GetMoverComponent())
	{
		if (UPrimitiveComponent* UpdatedCompAsPrimitive = Cast<UPrimitiveComponent>(MoverComponent->GetUpdatedComponent()))
		{
			if (bEnable)
			{
				CollideVsWorldStatic = UpdatedCompAsPrimitive->GetCollisionResponseToChannel(ECC_WorldStatic);
				CollideVsWorldDynamic = UpdatedCompAsPrimitive->GetCollisionResponseToChannel(ECC_WorldDynamic);

				// TODO NS: Eventually might want make the new collision response an option so they trigger certain things while still not colliding 
				UpdatedCompAsPrimitive->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Ignore);
				UpdatedCompAsPrimitive->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);

				if (UJoltKinematicNavWalkingMode* NavWalkingMode = Cast<UJoltKinematicNavWalkingMode>(MoverComponent->FindMovementMode(UJoltKinematicNavWalkingMode::StaticClass())))
				{
					if (UJoltMoverBlackboard* SimBlackboard = MoverComponent->GetSimBlackboard_Mutable())
					{
						const FJoltFloorCheckResult EmptyFloorCheckResult;
						SimBlackboard->Set(CommonBlackboard::LastFloorResult, EmptyFloorCheckResult);
					}

					// Stagger timed updates so many different characters spawned at the same time don't update on the same frame.
					// Initially we want an immediate update though, so set time to a negative randomized range.
					NavWalkingMode->NavMeshProjectionTimer = (NavWalkingMode->NavMeshProjectionInterval > 0.f) ? FMath::FRandRange(-NavWalkingMode->NavMeshProjectionInterval, 0.f) : 0.f;
				}
			}
			else
			{
				// Grabbing the original shape settings and reverting to our old collision responses
				if (const UShapeComponent* OriginalShapeComp = UJoltMovementUtils::GetOriginalComponentType<UShapeComponent>(MoverComponent->GetOwner()))
				{
					CollideVsWorldStatic = OriginalShapeComp->GetCollisionResponseToChannel(ECC_WorldStatic);
					CollideVsWorldDynamic = OriginalShapeComp->GetCollisionResponseToChannel(ECC_WorldDynamic);
				}

				UpdatedCompAsPrimitive->SetCollisionResponseToChannel(ECC_WorldStatic, CollideVsWorldStatic);
				UpdatedCompAsPrimitive->SetCollisionResponseToChannel(ECC_WorldDynamic, CollideVsWorldDynamic);
			}
		}
	}
}

void UJoltKinematicNavWalkingMode::Activate()
{
	Super::Activate();
	SetCollisionForNavWalking(true);

	NavDataInterface = GetNavData();
}

void UJoltKinematicNavWalkingMode::Deactivate()
{
	SetCollisionForNavWalking(false);
	Super::Deactivate();
}

const INavigationDataInterface* UJoltKinematicNavWalkingMode::GetNavData() const
{
	ANavigationData* NavData = nullptr;
	
	if (const UWorld* World = GetWorld())
	{
		const UNavigationSystemV1* NavSys = Cast<UNavigationSystemV1>(World->GetNavigationSystem());
		if (NavSys && NavMoverComponent)
		{
			const FNavAgentProperties& AgentProps = NavMoverComponent->GetNavAgentPropertiesRef();
			NavData = NavSys->GetNavDataForProps(AgentProps, NavMoverComponent->GetNavLocation());
		}
	}
	
	return NavData;
}

void UJoltKinematicNavWalkingMode::FindBestNavMeshLocation(const FVector& TraceStart, const FVector& TraceEnd, const FVector& CurrentFeetLocation, const FVector& TargetNavLocation, FHitResult& OutHitResult) const
{
	// raycast to underlying mesh to allow us to more closely follow geometry
	// we use static objects here as a best approximation to accept only objects that
	// influence navmesh generation
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ProjectLocation), false);

	// blocked by world static and optionally world dynamic
	FCollisionResponseParams ResponseParams(ECR_Ignore);
	ResponseParams.CollisionResponse.SetResponse(ECC_WorldStatic, ECR_Overlap);
	ResponseParams.CollisionResponse.SetResponse(ECC_WorldDynamic, bProjectNavMeshOnBothWorldChannels ? ECR_Overlap : ECR_Ignore);

	TArray<FHitResult> MultiTraceHits;
	GetWorld()->LineTraceMultiByChannel(MultiTraceHits, TraceStart, TraceEnd, ECC_WorldStatic, Params, ResponseParams);

	struct FCompareFHitResultNavMeshTrace
	{
		explicit FCompareFHitResultNavMeshTrace(const FVector& inSourceLocation) : SourceLocation(inSourceLocation)
		{
		}

		FORCEINLINE bool operator()(const FHitResult& A, const FHitResult& B) const
		{
			const float ADistSqr = (SourceLocation - A.ImpactPoint).SizeSquared();
			const float BDistSqr = (SourceLocation - B.ImpactPoint).SizeSquared();

			return (ADistSqr < BDistSqr);
		}

		const FVector& SourceLocation;
	};

	struct FRemoveNotBlockingResponseNavMeshTrace
	{
		FRemoveNotBlockingResponseNavMeshTrace(bool bInCheckOnlyWorldStatic) : bCheckOnlyWorldStatic(bInCheckOnlyWorldStatic) {}

		FORCEINLINE bool operator()(const FHitResult& TestHit) const
		{
			UPrimitiveComponent* PrimComp = TestHit.GetComponent();
			const bool bBlockOnWorldStatic = PrimComp && (PrimComp->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block);
			const bool bBlockOnWorldDynamic = PrimComp && (PrimComp->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Block);

			return !bBlockOnWorldStatic && (!bBlockOnWorldDynamic || bCheckOnlyWorldStatic);
		}

		bool bCheckOnlyWorldStatic;
	};

	MultiTraceHits.RemoveAllSwap(FRemoveNotBlockingResponseNavMeshTrace(!bProjectNavMeshOnBothWorldChannels), EAllowShrinking::No);
	if (MultiTraceHits.Num() > 0)
	{
		// Sort the hits by the closest to our origin.
		MultiTraceHits.Sort(FCompareFHitResultNavMeshTrace(TargetNavLocation));

		// Cache the closest hit and treat it as a blocking hit (we used an overlap to get all the world static hits so we could sort them ourselves)
		OutHitResult = MultiTraceHits[0];
		OutHitResult.bBlockingHit = true;
	}
}

FVector UJoltKinematicNavWalkingMode::ProjectLocationFromNavMesh(float DeltaSeconds, const FVector& CurrentFeetLocation, const FVector& TargetNavLocation, float UpOffset, float DownOffset)
{
	FVector NewLocation = TargetNavLocation;

	const float VerticalOffset = -(DownOffset + UpOffset);
	if (VerticalOffset > -UE_SMALL_NUMBER)
	{
		return NewLocation;
	}

	const UJoltMoverComponent* MoverComp = GetMoverComponent();
	const FVector UpDirection = MoverComp->GetUpDirection();
	
	const FVector TraceStart = TargetNavLocation + UpOffset * UpDirection;
	const FVector TraceEnd   = TargetNavLocation + DownOffset * -UpDirection;

	FJoltFloorCheckResult CachedFloorCheckResult;
	UJoltMoverBlackboard* SimBlackboard = MoverComp->GetSimBlackboard_Mutable();
	bool bHasValidFloorResult = SimBlackboard->TryGet(CommonBlackboard::LastFloorResult, CachedFloorCheckResult);
	FHitResult CachedProjectedNavMeshHitResult = CachedFloorCheckResult.HitResult;
	
	// We can skip this trace if we are checking at the same location as the last trace (ie, we haven't moved).
	const bool bCachedLocationStillValid = (CachedProjectedNavMeshHitResult.bBlockingHit &&
											CachedProjectedNavMeshHitResult.TraceStart == TraceStart &&
											CachedProjectedNavMeshHitResult.TraceEnd == TraceEnd);

	// Check periodically or if we have no information about our last floor result
	NavMeshProjectionTimer -= DeltaSeconds;
	if (NavMeshProjectionTimer <= 0.0f || !bHasValidFloorResult)
	{
		if (!bCachedLocationStillValid)
		{
			UE_LOG(LogJoltMover, VeryVerbose, TEXT("ProjectLocationFromNavMesh(): %s interval: %.3f [SKIP TRACE]"), *GetNameSafe(GetMoverComponent()->GetOwner()), NavMeshProjectionInterval);

			FHitResult HitResult;
			FindBestNavMeshLocation(TraceStart, TraceEnd, CurrentFeetLocation, TargetNavLocation, HitResult);

			// discard result if we were already inside something
			if (HitResult.bStartPenetrating || !HitResult.bBlockingHit)
			{
				CachedProjectedNavMeshHitResult.Reset();
				const FJoltFloorCheckResult EmptyFloorCheckResult;
				SimBlackboard->Set(CommonBlackboard::LastFloorResult, EmptyFloorCheckResult);
			}
			else
			{
				CachedProjectedNavMeshHitResult = HitResult;
				
				FJoltFloorCheckResult FloorCheckResult;
				FloorCheckResult.bBlockingHit = HitResult.bBlockingHit;
				FloorCheckResult.bLineTrace = true;
				FloorCheckResult.bWalkableFloor = true;
				FloorCheckResult.LineDist = FMath::Abs((CurrentFeetLocation - CachedProjectedNavMeshHitResult.ImpactPoint).Dot(UpDirection));
				FloorCheckResult.FloorDist = FloorCheckResult.LineDist; // This is usually set from a sweep trace but it doesn't really hurt setting it. 
				FloorCheckResult.HitResult = HitResult;
				SimBlackboard->Set(CommonBlackboard::LastFloorResult, FloorCheckResult);
			}
		}
		else
		{
			UE_LOG(LogJoltMover, VeryVerbose, TEXT("ProjectLocationFromNavMesh(): %s interval: %.3f [SKIP TRACE]"), *GetNameSafe(GetMoverComponent()->GetOwner()), NavMeshProjectionInterval);
		}

		// Wrap around to maintain same relative offset to tick time changes.
		// Prevents large framerate spikes from aligning multiple characters to the same frame (if they start staggered, they will now remain staggered).
		float ModTime = 0.f;
		if (NavMeshProjectionInterval > UE_SMALL_NUMBER)
		{
			ModTime = FMath::Fmod(-NavMeshProjectionTimer, NavMeshProjectionInterval);
		}

		NavMeshProjectionTimer = NavMeshProjectionInterval - ModTime;
	}
	
	// Project to last plane we found.
	if (CachedProjectedNavMeshHitResult.bBlockingHit)
	{
		if (bCachedLocationStillValid && FMath::IsNearlyEqual(CurrentFeetLocation.Dot(UpDirection), CachedProjectedNavMeshHitResult.ImpactPoint.Dot(UpDirection), (FVector::FReal)0.01f))
		{
			// Already at destination.
			UJoltMovementUtils::SetGravityVerticalComponent(NewLocation, CurrentFeetLocation.Dot(UpDirection), UpDirection);
		}
		else
		{
			const FVector ProjectedPoint = FMath::LinePlaneIntersection(TraceStart, TraceEnd, CachedProjectedNavMeshHitResult.ImpactPoint, CachedProjectedNavMeshHitResult.Normal);
			FVector::FReal ProjectedVertical = ProjectedPoint.Dot(UpDirection);
			
			// Limit to not be too far above or below NavMesh location
			const FVector::FReal VertTraceStart = TraceStart.Dot(UpDirection);
			const FVector::FReal VertTraceEnd = TraceEnd.Dot(UpDirection);
			const FVector::FReal TraceMin = FMath::Min(VertTraceStart, VertTraceEnd);
			const FVector::FReal TraceMax = FMath::Max(VertTraceStart, VertTraceEnd);
			ProjectedVertical = FMath::Clamp(ProjectedVertical, TraceMin, TraceMax);

			// Interp for smoother updates (less "pop" when trace hits something new). 0 interp speed is instant.
			const FVector::FReal InterpSpeed = FMath::Max<FVector::FReal>(0.f, NavMeshProjectionInterpSpeed);
			ProjectedVertical = FMath::FInterpTo(CurrentFeetLocation.Dot(UpDirection), ProjectedVertical, (FVector::FReal)DeltaSeconds, InterpSpeed);
			ProjectedVertical = FMath::Clamp(ProjectedVertical, TraceMin, TraceMax);
			
			// Final result
			UJoltMovementUtils::SetGravityVerticalComponent(NewLocation, ProjectedVertical, UpDirection);
		}
	}

	return NewLocation;
}

void UJoltKinematicNavWalkingMode::OnRegistered(const FName ModeName)
{
	Super::OnRegistered(ModeName);

	UJoltMoverComponent* MoverComponent = GetMoverComponent();
	CommonLegacySettings = MoverComponent->FindSharedSettings<UJoltCommonLegacyMovementSettings>();
	ensureMsgf(CommonLegacySettings, TEXT("Failed to find instance of CommonLegacyMovementSettings on %s. Movement may not function properly."), *GetPathNameSafe(this));
	
	if (const AActor* MoverCompOwner = MoverComponent->GetOwner())
	{
		NavMoverComponent = MoverCompOwner->FindComponentByClass<UNavJoltMoverComponent>();
	}
	
	if (!NavMoverComponent)
	{
		UE_LOG(LogJoltMover, Warning, TEXT("NavWalkingMode on %s could not find a valid NavMoverComponent and will not function properly."), *GetNameSafe(GetMoverComponent()->GetOwner()));
	}
}

void UJoltKinematicNavWalkingMode::OnUnregistered()
{
	CommonLegacySettings = nullptr;
	NavDataInterface = nullptr;

	Super::OnUnregistered();
}

void UJoltKinematicNavWalkingMode::CaptureFinalState(USceneComponent* UpdatedComponent, const FJoltMovementRecord& Record, const FVector& AngularVelocityDegrees, FJoltUpdatedMotionState& OutputSyncState) const
{
	UJoltMoverBlackboard* SimBlackboard = GetMoverComponent()->GetSimBlackboard_Mutable();

	SimBlackboard->Invalidate(CommonBlackboard::LastFoundDynamicMovementBase);

	OutputSyncState.SetTransforms_WorldSpace(UpdatedComponent->GetComponentLocation(),
		UpdatedComponent->GetComponentRotation(),
		Record.GetRelevantVelocity(),
		AngularVelocityDegrees,
		nullptr);	// no movement base

	UpdatedComponent->ComponentVelocity = OutputSyncState.GetVelocity_WorldSpace();
}



