// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoveLibrary/JoltBasedMovementUtils.h"
#include "MoveLibrary/JoltMovementUtils.h"
#include "MoveLibrary/JoltFloorQueryUtils.h"
#include "Components/PrimitiveComponent.h"
#include "JoltMoverComponent.h"
#include "JoltMoverLog.h"
#include "Kismet/KismetMathLibrary.h"
#include "DefaultMovementSet/Settings/JoltCommonLegacyMovementSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltBasedMovementUtils)

void FJoltRelativeBaseInfo::Clear()
{
	MovementBase = nullptr;
	BoneName = NAME_None;
	Location = FVector::ZeroVector;
	Rotation = FQuat::Identity;
	ContactLocalPosition = FVector::ZeroVector;
}

bool FJoltRelativeBaseInfo::HasRelativeInfo() const
{
	return MovementBase != nullptr;
}

bool FJoltRelativeBaseInfo::UsesSameBase(const FJoltRelativeBaseInfo& Other) const
{
	return UsesSameBase(Other.MovementBase.Get(), Other.BoneName);
}

bool FJoltRelativeBaseInfo::UsesSameBase(const UPrimitiveComponent* OtherComp, FName OtherBoneName) const
{
	return HasRelativeInfo()
		&& (MovementBase == OtherComp)
		&& (BoneName == OtherBoneName);
}

void FJoltRelativeBaseInfo::SetFromFloorResult(const FJoltFloorCheckResult& FloorTestResult)
{
	bool bDidSucceed = false;

	if (FloorTestResult.bWalkableFloor)
	{
		MovementBase = FloorTestResult.HitResult.GetComponent();

		if (MovementBase.IsValid())
		{
			BoneName = FloorTestResult.HitResult.BoneName;

			if (UJoltBasedMovementUtils::GetMovementBaseTransform(MovementBase.Get(), BoneName, OUT Location, OUT Rotation) &&
				UJoltBasedMovementUtils::TransformWorldLocationToBased(MovementBase.Get(), BoneName, FloorTestResult.HitResult.ImpactPoint, OUT ContactLocalPosition))
			{
				bDidSucceed = true;
			}
		}
	}

	if (!bDidSucceed)
	{
		Clear();
	}
}

void FJoltRelativeBaseInfo::SetFromComponent(UPrimitiveComponent* InRelativeComp, FName InBoneName)
{
	bool bDidSucceed = false;

	MovementBase = InRelativeComp;

	if (MovementBase.IsValid())
	{
		BoneName = InBoneName;
		bDidSucceed = UJoltBasedMovementUtils::GetMovementBaseTransform(MovementBase.Get(), BoneName, /*out*/Location, /*out*/Rotation);
	}

	if (!bDidSucceed)
	{
		Clear();
	}
}


FString FJoltRelativeBaseInfo::ToString() const
{
	if (MovementBase.IsValid())
	{
		return FString::Printf(TEXT("Base: %s, Loc: %s, Rot: %s, LocalContact: %s"),
			*GetNameSafe(MovementBase->GetOwner()),
			*Location.ToCompactString(),
			*Rotation.Rotator().ToCompactString(),
			*ContactLocalPosition.ToCompactString());
	}

	return FString(TEXT("Base: NULL"));
}

bool UJoltBasedMovementUtils::IsADynamicBase(const UPrimitiveComponent* MovementBase)
{
	return (MovementBase && MovementBase->Mobility == EComponentMobility::Movable);
}

bool UJoltBasedMovementUtils::IsBaseSimulatingPhysics(const UPrimitiveComponent* MovementBase)
{
	bool bBaseIsSimulatingPhysics = false;
	const USceneComponent* AttachParent = MovementBase;
	while (!bBaseIsSimulatingPhysics && AttachParent)
	{
		bBaseIsSimulatingPhysics = AttachParent->IsSimulatingPhysics();
		AttachParent = AttachParent->GetAttachParent();
	}
	return bBaseIsSimulatingPhysics;
}


bool UJoltBasedMovementUtils::GetMovementBaseTransform(const UPrimitiveComponent* MovementBase, const FName BoneName, FVector& OutLocation, FQuat& OutQuat)
{
	if (MovementBase)
	{
		bool bBoneNameIsInvalid = false;

		if (BoneName != NAME_None)
		{
			// Check if this socket or bone exists (DoesSocketExist checks for either, as does requesting the transform).
			if (MovementBase->DoesSocketExist(BoneName))
			{
				MovementBase->GetSocketWorldLocationAndRotation(BoneName, OutLocation, OutQuat);
				return true;
			}

			bBoneNameIsInvalid = true;
			UE_LOG(LogJoltMover, Warning, TEXT("GetMovementBaseTransform(): Invalid bone or socket '%s' for PrimitiveComponent base %s. Using component's root transform instead."), *BoneName.ToString(), *GetPathNameSafe(MovementBase));
		}

		OutLocation = MovementBase->GetComponentLocation();
		OutQuat = MovementBase->GetComponentQuat();
		return !bBoneNameIsInvalid;
	}

	// nullptr MovementBase
	OutLocation = FVector::ZeroVector;
	OutQuat = FQuat::Identity;
	return false;
}


bool UJoltBasedMovementUtils::TransformBasedLocationToWorld(const UPrimitiveComponent* MovementBase, const FName BoneName, FVector LocalLocation, FVector& OutLocationWorldSpace)
{
	FVector BaseLocation;
	FQuat BaseQuat;
	
	if (GetMovementBaseTransform(MovementBase, BoneName, /*out*/ BaseLocation, /*out*/ BaseQuat))
	{ 
		TransformLocationToWorld(BaseLocation, BaseQuat, LocalLocation, OutLocationWorldSpace);
		return true;
	}
	
	return false;
}


bool UJoltBasedMovementUtils::TransformWorldLocationToBased(const UPrimitiveComponent* MovementBase, const FName BoneName, FVector WorldSpaceLocation, FVector& OutLocalLocation)
{
	FVector BaseLocation;
	FQuat BaseQuat;
	if (GetMovementBaseTransform(MovementBase, BoneName, /*out*/ BaseLocation, /*out*/ BaseQuat))
	{
		TransformLocationToLocal(BaseLocation, BaseQuat, WorldSpaceLocation, OutLocalLocation);
		return true;
	}

	return false;
}


bool UJoltBasedMovementUtils::TransformBasedDirectionToWorld(const UPrimitiveComponent* MovementBase, const FName BoneName, FVector LocalDirection, FVector& OutDirectionWorldSpace)
{
	FVector IgnoredLocation;
	FQuat BaseQuat;
	if (GetMovementBaseTransform(MovementBase, BoneName, /*out*/ IgnoredLocation, /*out*/ BaseQuat))
	{
		TransformDirectionToWorld(BaseQuat, LocalDirection, OutDirectionWorldSpace);
		return true;
	}

	return false;
}


bool UJoltBasedMovementUtils::TransformWorldDirectionToBased(const UPrimitiveComponent* MovementBase, const FName BoneName, FVector WorldSpaceDirection, FVector& OutLocalDirection)
{
	FVector IgnoredLocation;
	FQuat BaseQuat;
	if (GetMovementBaseTransform(MovementBase, BoneName, /*out*/ IgnoredLocation, /*out*/ BaseQuat))
	{
		TransformDirectionToLocal(BaseQuat, WorldSpaceDirection, OutLocalDirection);
		return true;
	}

	return false;
}


bool UJoltBasedMovementUtils::TransformBasedRotatorToWorld(const UPrimitiveComponent* MovementBase, const FName BoneName, FRotator LocalRotator, FRotator& OutWorldSpaceRotator)
{
	FVector IgnoredLocation;
	FQuat BaseQuat;
	
	if (GetMovementBaseTransform(MovementBase, BoneName, /*out*/ IgnoredLocation, /*out*/ BaseQuat))
	{
		TransformRotatorToWorld(BaseQuat, LocalRotator, OutWorldSpaceRotator);
		return true;
	}

	return false;
}


bool UJoltBasedMovementUtils::TransformWorldRotatorToBased(const UPrimitiveComponent* MovementBase, const FName BoneName, FRotator WorldSpaceRotator, FRotator& OutLocalRotator)
{
	FVector IgnoredLocation;
	FQuat BaseQuat;
	if (GetMovementBaseTransform(MovementBase, BoneName, /*out*/ IgnoredLocation, /*out*/ BaseQuat))
	{
		TransformRotatorToLocal(BaseQuat, WorldSpaceRotator, OutLocalRotator);
		return true;
	}
	return false;
}


void UJoltBasedMovementUtils::TransformLocationToWorld(FVector BasePos, FQuat BaseQuat, FVector LocalLocation, FVector& OutLocationWorldSpace)
{
	OutLocationWorldSpace = FTransform(BaseQuat, BasePos).TransformPositionNoScale(LocalLocation);
}

void UJoltBasedMovementUtils::TransformLocationToLocal(FVector BasePos, FQuat BaseQuat, FVector WorldSpaceLocation, FVector& OutLocalLocation)
{
	OutLocalLocation = FTransform(BaseQuat, BasePos).InverseTransformPositionNoScale(WorldSpaceLocation);
}

void UJoltBasedMovementUtils::TransformDirectionToWorld(FQuat BaseQuat, FVector LocalDirection, FVector& OutDirectionWorldSpace)
{
	OutDirectionWorldSpace = BaseQuat.RotateVector(LocalDirection);
}

void UJoltBasedMovementUtils::TransformDirectionToLocal(FQuat BaseQuat, FVector WorldSpaceDirection, FVector& OutLocalDirection)
{
	OutLocalDirection = BaseQuat.UnrotateVector(WorldSpaceDirection);
}

void UJoltBasedMovementUtils::TransformRotatorToWorld(FQuat BaseQuat, FRotator LocalRotator, FRotator& OutWorldSpaceRotator)
{
	FQuat LocalQuat(LocalRotator);
	OutWorldSpaceRotator = (BaseQuat * LocalQuat).Rotator();
}

void UJoltBasedMovementUtils::TransformRotatorToLocal(FQuat BaseQuat, FRotator WorldSpaceRotator, FRotator& OutLocalRotator)
{
	FQuat WorldQuat(WorldSpaceRotator);
	OutLocalRotator = (BaseQuat.Inverse() * WorldQuat).Rotator();
}

void UJoltBasedMovementUtils::AddTickDependency(FTickFunction& BasedObjectTick, UPrimitiveComponent* NewBase)
{
	if (NewBase && IsADynamicBase(NewBase))
	{
		if (NewBase->PrimaryComponentTick.bCanEverTick)
		{
			BasedObjectTick.AddPrerequisite(NewBase, NewBase->PrimaryComponentTick);
		}

		AActor* NewBaseOwner = NewBase->GetOwner();
		if (NewBaseOwner)
		{
			if (NewBaseOwner->PrimaryActorTick.bCanEverTick)
			{
				BasedObjectTick.AddPrerequisite(NewBaseOwner, NewBaseOwner->PrimaryActorTick);
			}

			// @TODO: We need to find a more efficient way of finding all ticking components in an actor.
			for (UActorComponent* Component : NewBaseOwner->GetComponents())
			{
				// Dont allow a based component (e.g. a particle system) to push us into a different tick group
				if (Component && Component->PrimaryComponentTick.bCanEverTick && Component->PrimaryComponentTick.TickGroup <= BasedObjectTick.TickGroup)
				{
					BasedObjectTick.AddPrerequisite(Component, Component->PrimaryComponentTick);
				}
			}
		}
	}
	else
	{
		UE_LOG(LogJoltMover, Warning, TEXT("Attempted to AddTickDependency on an invalid or non-dynamic base: %s"), *GetNameSafe(NewBase));
	}
}

void UJoltBasedMovementUtils::RemoveTickDependency(FTickFunction& BasedObjectTick, UPrimitiveComponent* OldBase)
{
	if (OldBase)
	{
		BasedObjectTick.RemovePrerequisite(OldBase, OldBase->PrimaryComponentTick);
		
		if (AActor* OldBaseOwner = OldBase->GetOwner())
		{
			BasedObjectTick.RemovePrerequisite(OldBaseOwner, OldBaseOwner->PrimaryActorTick);

			// @TODO: We need to find a more efficient way of finding all ticking components in an actor.
			for (UActorComponent* Component : OldBaseOwner->GetComponents())
			{
				if (Component && Component->PrimaryComponentTick.bCanEverTick)
				{
					BasedObjectTick.RemovePrerequisite(Component, Component->PrimaryComponentTick);
				}
			}
		}
	}
}


void UJoltBasedMovementUtils::UpdateSimpleBasedMovement(UJoltMoverComponent* TargetMoverComp)
{
	if (!TargetMoverComp)
	{
		return;
	}

	UJoltMoverBlackboard* SimBlackboard = TargetMoverComp->GetSimBlackboard_Mutable();
	USceneComponent* UpdatedComponent = TargetMoverComp->UpdatedComponent;

	bool bIgnoreBaseRotation = false;

	if (const UJoltCommonLegacyMovementSettings* CommonSettings = TargetMoverComp->FindSharedSettings<UJoltCommonLegacyMovementSettings>())
	{
		bIgnoreBaseRotation = CommonSettings->bIgnoreBaseRotation;
	}

	bool bDidGetUpToDate = false;

	FJoltRelativeBaseInfo LastFoundBaseInfo;	// Last-found is the most recent capture during movement, likely set this sim frame
	FJoltRelativeBaseInfo LastAppliedBaseInfo;	// Last-applied is the one that our based movement is up to date with, likely set in the last sim frame
	FJoltRelativeBaseInfo CurrentBaseInfo;		// Current info is the current snapshot of the current base, with up-to-date transform that may be different than last-found.

	const bool bHasLastFoundInfo = SimBlackboard->TryGet(CommonBlackboard::LastFoundDynamicMovementBase, LastFoundBaseInfo);
	const bool bHasLastAppliedInfo = SimBlackboard->TryGet(CommonBlackboard::LastAppliedDynamicMovementBase, LastAppliedBaseInfo);
	if (bHasLastFoundInfo)
	{
		if (!bHasLastAppliedInfo || !LastFoundBaseInfo.UsesSameBase(LastAppliedBaseInfo))
		{
			LastAppliedBaseInfo = LastFoundBaseInfo;	// This is the first time we've checked this base, so start with the last-found capture
		}

		if (!ensureMsgf(LastFoundBaseInfo.HasRelativeInfo() && LastFoundBaseInfo.UsesSameBase(LastAppliedBaseInfo),
				TEXT("Attempting to update based movement with a missing or mismatched base. This may indicate a logic problem with detecting bases.")))
		{ 
			SimBlackboard->Invalidate(CommonBlackboard::LastFoundDynamicMovementBase);
			SimBlackboard->Invalidate(CommonBlackboard::LastAppliedDynamicMovementBase);
			return;
		}

		CurrentBaseInfo.SetFromComponent(LastFoundBaseInfo.MovementBase.Get(), LastFoundBaseInfo.BoneName);
		CurrentBaseInfo.ContactLocalPosition = LastFoundBaseInfo.ContactLocalPosition;

		FVector CurrentBaseLocation;
		FQuat CurrentBaseQuat;
			

		if (UJoltBasedMovementUtils::GetMovementBaseTransform(CurrentBaseInfo.MovementBase.Get(), CurrentBaseInfo.BoneName, OUT CurrentBaseLocation, OUT CurrentBaseQuat))
		{
			const bool bDidBaseRotationChange = !LastAppliedBaseInfo.Rotation.Equals(CurrentBaseQuat, UE_SMALL_NUMBER);
			const bool bDidBaseLocationChange = (LastAppliedBaseInfo.Location != CurrentBaseLocation);

			FQuat DeltaQuat = FQuat::Identity;
			FVector WorldDeltaLocation = FVector::ZeroVector;
			FQuat WorldTargetQuat = UpdatedComponent->GetComponentQuat();

			// Find change in rotation

			if (bDidBaseRotationChange && !bIgnoreBaseRotation)
			{
				DeltaQuat = CurrentBaseQuat * LastAppliedBaseInfo.Rotation.Inverse();
				WorldTargetQuat = DeltaQuat * WorldTargetQuat;

				FVector TargetForwVector = WorldTargetQuat.GetForwardVector();
				TargetForwVector = FVector::VectorPlaneProject(TargetForwVector, -TargetMoverComp->GetUpDirection());
				TargetForwVector.Normalize();

				FVector TargetRightVector = WorldTargetQuat.GetRightVector();
				TargetRightVector = FVector::VectorPlaneProject(TargetRightVector, -TargetMoverComp->GetUpDirection());
				TargetRightVector.Normalize();
					
				WorldTargetQuat = UKismetMathLibrary::MakeRotFromXY(TargetForwVector, TargetRightVector).Quaternion();
			}

			if (bDidBaseLocationChange || bDidBaseRotationChange)
			{
				// Calculate new transform matrix of base actor (ignoring scale).
				const FQuatRotationTranslationMatrix OldLocalToWorld(LastAppliedBaseInfo.Rotation, LastAppliedBaseInfo.Location);
				const FQuatRotationTranslationMatrix NewLocalToWorld(CurrentBaseQuat, CurrentBaseLocation);

				// Find change in location
				// NOTE that we are using the floor hit location, not the actor's root position which may be floating above the base
				const FVector NewWorldBaseContactPos = NewLocalToWorld.TransformPosition(CurrentBaseInfo.ContactLocalPosition);
				const FVector OldWorldBaseContactPos = OldLocalToWorld.TransformPosition(CurrentBaseInfo.ContactLocalPosition);
				WorldDeltaLocation = NewWorldBaseContactPos - OldWorldBaseContactPos;

				const FVector OldWorldLocation = UpdatedComponent->GetComponentLocation();
				EMoveComponentFlags MoveComponentFlags = MOVECOMP_IgnoreBases;
				const bool bSweep = true;
				FHitResult MoveHitResult;

				bool bDidMove = UJoltMovementUtils::TryMoveUpdatedComponent_Internal(FJoltMovingComponentSet(TargetMoverComp), WorldDeltaLocation, WorldTargetQuat, bSweep, MoveComponentFlags, &MoveHitResult, ETeleportType::None);
					
				const FVector NewWorldLocation = UpdatedComponent->GetComponentLocation();

				if ((NewWorldLocation - (OldWorldLocation + WorldDeltaLocation)).IsNearlyZero() == false)
				{
					// Find the remaining delta that wasn't achieved
					const FVector UnachievedWorldDelta = (OldWorldLocation + WorldDeltaLocation) - NewWorldLocation;

					// Convert the remaining delta to current base space
					FVector UnachievedLocalDelta;
					UJoltBasedMovementUtils::TransformLocationToLocal(CurrentBaseLocation, CurrentBaseQuat, UnachievedWorldDelta, OUT UnachievedLocalDelta);
						
					// Subtract the remaining delta to reflect the change in the contact position
					CurrentBaseInfo.ContactLocalPosition -= UnachievedLocalDelta;
				}

				// Propagate the movement changes to the backend's state, if supported

				// Note that this is occurring out-of-band with the movement simulation, in order to support based movement regardless of update order or
				// whether the movement base is also simulated through Mover.

				FJoltMoverSyncState PendingSimSyncState;
				if (TargetMoverComp->BackendLiaisonComp->ReadPendingSyncState(OUT PendingSimSyncState))
				{
					// Modify the PENDING sync state that has not yet been committed to simulation history nor replicated
					if (FJoltUpdatedMotionState* PendingMoverState = PendingSimSyncState.Collection.FindMutableDataByType<FJoltUpdatedMotionState>())
					{
						FTransform OldSyncTransformWs = PendingMoverState->GetTransform_WorldSpace();
						FTransform NewSyncTransformWs = UpdatedComponent->GetComponentTransform();

						PendingMoverState->SetTransforms_WorldSpace(
							NewSyncTransformWs.GetLocation(),
							NewSyncTransformWs.GetRotation().Rotator(),
							PendingMoverState->GetVelocity_WorldSpace(),	// keep same velocity and base
							PendingMoverState->GetAngularVelocityDegrees_WorldSpace(),
							PendingMoverState->GetMovementBase(), PendingMoverState->GetMovementBaseBoneName());

						TargetMoverComp->BackendLiaisonComp->WritePendingSyncState(PendingSimSyncState);	// writes pending Simulation state

						// If smoothing, modify presentation-related states as well so that the visual offset location stays anchored to the movement base
						if (TargetMoverComp->SmoothingMode != EJoltMoverSmoothingMode::None)
						{
							const FTransform OldToNewTransform = NewSyncTransformWs.GetRelativeTransform(OldSyncTransformWs);

							// Modify the PRESENTATION sync state that we're smoothing TO
							FJoltMoverSyncState PresentationSyncState;
							if (TargetMoverComp->BackendLiaisonComp->ReadPresentationSyncState(OUT PresentationSyncState))
							{
								if (FJoltUpdatedMotionState* PresentationMoverState = PresentationSyncState.Collection.FindMutableDataByType<FJoltUpdatedMotionState>())
								{
									OldSyncTransformWs = PresentationMoverState->GetTransform_WorldSpace();
									NewSyncTransformWs = OldToNewTransform * OldSyncTransformWs;

									PresentationMoverState->SetTransforms_WorldSpace(
										NewSyncTransformWs.GetLocation(),
										NewSyncTransformWs.GetRotation().Rotator(),
										PresentationMoverState->GetVelocity_WorldSpace(),	// keep same velocity and base
										PresentationMoverState->GetAngularVelocityDegrees_WorldSpace(),
										PresentationMoverState->GetMovementBase(), PresentationMoverState->GetMovementBaseBoneName());

									TargetMoverComp->BackendLiaisonComp->WritePresentationSyncState(PresentationSyncState);
								}
							}

							// Modify the PREV PRESENTATION sync state that we're smoothing FROM
							FJoltMoverSyncState PrevPresentationSyncState;
							if (TargetMoverComp->BackendLiaisonComp->ReadPrevPresentationSyncState(OUT PrevPresentationSyncState))
							{
								if (FJoltUpdatedMotionState* PrevPresentationMoverState = PrevPresentationSyncState.Collection.FindMutableDataByType<FJoltUpdatedMotionState>())
								{
									OldSyncTransformWs = PrevPresentationMoverState->GetTransform_WorldSpace();
									NewSyncTransformWs = OldToNewTransform * OldSyncTransformWs;

									PrevPresentationMoverState->SetTransforms_WorldSpace(
										NewSyncTransformWs.GetLocation(),
										NewSyncTransformWs.GetRotation().Rotator(),
										PrevPresentationMoverState->GetVelocity_WorldSpace(),	// keep same velocity and base
										PrevPresentationMoverState->GetAngularVelocityDegrees_WorldSpace(),
										PrevPresentationMoverState->GetMovementBase(), PrevPresentationMoverState->GetMovementBaseBoneName());

									TargetMoverComp->BackendLiaisonComp->WritePrevPresentationSyncState(PrevPresentationSyncState);	
								}
							}
						}
					}
				}
			}

			SimBlackboard->Set(CommonBlackboard::LastAppliedDynamicMovementBase, CurrentBaseInfo);
			bDidGetUpToDate = true;
		}
	}

	if (!bDidGetUpToDate)
	{
		SimBlackboard->Invalidate(CommonBlackboard::LastAppliedDynamicMovementBase);
	}
}



// FJoltMoverDynamicBasedMovementTickFunction ////////////////////////////////////

void FJoltMoverDynamicBasedMovementTickFunction::ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	FActorComponentTickFunction::ExecuteTickHelper(TargetMoverComp, /*bTickInEditor=*/ false, DeltaTime, TickType, [this](float DilatedTime)
		{
			UJoltBasedMovementUtils::UpdateSimpleBasedMovement(TargetMoverComp);
		});

	if (bAutoDisableAfterTick)
	{
		SetTickFunctionEnable(false);
	}
}
FString FJoltMoverDynamicBasedMovementTickFunction::DiagnosticMessage()
{
	return TargetMoverComp->GetFullName() + TEXT("[FJoltMoverDynamicBasedMovementTickFunction]");
}
FName FJoltMoverDynamicBasedMovementTickFunction::DiagnosticContext(bool bDetailed)
{
	if (bDetailed)
	{
		return FName(*FString::Printf(TEXT("UJoltMoverComponent/%s"), *GetFullNameSafe(TargetMoverComp)));
	}
	return FName(TEXT("FJoltMoverDynamicBasedMovementTickFunction"));
}

