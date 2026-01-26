// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltMovementMode.h"
#include "JoltMovementModeTransition.h"
#include "JoltMoverComponent.h"
#include "JoltMoverLog.h"
#include "Engine/BlueprintGeneratedClass.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include "Core/Interfaces/JoltPrimitiveComponentInterface.h"
#include "Core/Singletons/JoltPhysicsWorldSubsystem.h"
#include "DefaultMovementSet/Settings/JoltCommonLegacyMovementSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMovementMode)

#define LOCTEXT_NAMESPACE "JoltMover"

UWorld* UJoltBaseMovementMode::GetWorld() const
{
#if WITH_EDITOR
	// In the editor, GetWorld() is called on the CDO as part of checking ImplementsGetWorld(). Only the CDO can exist without being outer'd to a MoverComponent.
	if (IsTemplate())
	{
		return nullptr;
	}
#endif
	return GetOuterUJoltMoverComponent()->GetWorld();
}


void UJoltBaseMovementMode::OnRegistered(const FName ModeName)
{
	for (TObjectPtr<UJoltBaseMovementModeTransition>& Transition : Transitions)
	{
		if (Transition)
		{
			Transition->OnRegistered();
		}
		else
		{
			UE_LOG(LogJoltMover, Error, TEXT("Invalid or missing transition object on mode of type %s of component %s for actor %s"), *GetPathNameSafe(this), *GetNameSafe(GetOuter()), *GetNameSafe(GetOutermost()));
		}
	}
	
	K2_OnRegistered(ModeName);
}

void UJoltBaseMovementMode::OnUnregistered()
{
	for (TObjectPtr<UJoltBaseMovementModeTransition>& Transition : Transitions)
	{
		if (Transition)
		{
			Transition->OnUnregistered();
		}
		else
		{
			UE_LOG(LogJoltMover, Error, TEXT("Invalid or missing transition object on mode of type %s of component %s for actor %s"), *GetPathNameSafe(this), *GetNameSafe(GetOuter()), *GetNameSafe(GetOutermost()));
		}
	}

	K2_OnUnregistered();
}

void UJoltBaseMovementMode::Activate()
{
	if (!bSupportsAsync)
	{
		K2_OnActivated();
	}
}

void UJoltBaseMovementMode::Deactivate()
{
	if (!bSupportsAsync)
	{
		K2_OnDeactivated();
	}
}

void UJoltBaseMovementMode::Activate_External()
{
	if (bSupportsAsync)
	{
		K2_OnActivated();
	}
}

void UJoltBaseMovementMode::Deactivate_External()
{
	if (bSupportsAsync)
	{
		K2_OnDeactivated();
	}
}

void UJoltBaseMovementMode::GenerateMove_Implementation(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, FJoltProposedMove& OutProposedMove) const
{
}

void UJoltBaseMovementMode::SimulationTick_Implementation(const FJoltSimulationTickParams& Params, FJoltMoverTickEndData& OutputState)
{
}

UJoltMoverComponent* UJoltBaseMovementMode::K2_GetMoverComponent() const
{
	return GetOuterUJoltMoverComponent();
}

#if WITH_EDITOR
EDataValidationResult UJoltBaseMovementMode::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = EDataValidationResult::Valid;
	for (UJoltBaseMovementModeTransition* Transition : Transitions)
	{
		if (!IsValid(Transition))
		{
			Context.AddError(FText::Format(LOCTEXT("InvalidTransitionOnModeError", "Invalid or missing transition object on mode of type {0}. Clean up the Transitions array."),
				FText::FromString(GetClass()->GetName())));

			Result = EDataValidationResult::Invalid;
		}
		else if (Transition->IsDataValid(Context) == EDataValidationResult::Invalid)
		{
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif // WITH_EDITOR


bool UJoltBaseMovementMode::HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const
{
	if (bExactMatch)
	{
		return GameplayTags.HasTagExact(TagToFind);
	}

	return GameplayTags.HasTag(TagToFind);
}

const FName UJoltNullMovementMode::NullModeName(TEXT("Null"));

UJoltNullMovementMode::UJoltNullMovementMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FVector UJoltBaseMovementMode::FloorCheck(const FVector& StartingLocation, const FVector& ProposedLinearVelocity, const float& DeltaTime, FJoltFloorCheckResult& Result) const
{
	Result.bBlockingHit = false;
	Result.bWalkableFloor = false;
	const UJoltMoverComponent* M = GetMoverComponent<UJoltMoverComponent>();
	if (!M) return ProposedLinearVelocity;
	
	UPrimitiveComponent* JoltBody = M->GetUpdatedComponent<UPrimitiveComponent>();
	if (!JoltBody) return ProposedLinearVelocity;
	UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>();
	if (!Subsystem) return ProposedLinearVelocity;
	const UJoltCommonLegacyMovementSettings* SharedSettingsPtr = M->FindSharedSettings<UJoltCommonLegacyMovementSettings>();
	if (!SharedSettingsPtr) return ProposedLinearVelocity;

	if (const IJoltPrimitiveComponentInterface* I = Cast<IJoltPrimitiveComponentInterface>(JoltBody))
	{
		const FVector& UpDir = M->GetUpDirection();
		const FVector& Dir = -UpDir;
		const FVector& End = StartingLocation + (Dir * I->GetGroundTraceDistance());
	
		int32 HitBodyId;
		const FHitResult Hit = Subsystem->SweepSphereSingleByChannel(I->GetShapeWidth(), StartingLocation, End, ECC_WorldStatic, {M->GetOwner()}, HitBodyId);
		const bool bWalkable = UJoltFloorQueryUtils::IsHitSurfaceWalkable(Hit, UpDir, SharedSettingsPtr->MaxWalkSlopeCosine);
		Result.bBlockingHit = Hit.bBlockingHit;
		Result.bWalkableFloor = bWalkable;
		Result.FloorDist = UpDir.Dot(StartingLocation - Hit.ImpactPoint);
		Result.HitResult = Hit;
	
		// Update the ground distance based on the slope. If you are on a slope the query might hit on an edge
		// rather than directly under the capsule. Also move back to the start location

		if (bWalkable)
		{
			const float ShapeHeight = I->GetShapeHeight();
			const float Distance = Hit.Distance;
			const float UpperLimit = ShapeHeight * (1.f - I->GetShapeStepHeightRatio()) * 0.5f;
			const float Middle = ShapeHeight + UpperLimit * I->GetShapeStepHeightRatio();
			const float DistanceToGo = Middle - Distance;
			
			return (UpDir * (DistanceToGo * DeltaTime)) + ProposedLinearVelocity;
			
		} 
		
		
		/*const float DP = Hit.ImpactNormal.Dot(UpDir);
		if (DP > UE_SMALL_NUMBER)
		{
			const JPH::Body* Rb = Subsystem->GetRigidBody(M->GetUpdatedComponent<UPrimitiveComponent>());
			if (!Rb) return;

			uint64 P = Rb->GetUserData();
				
			const FJoltUserData* D = reinterpret_cast<const FJoltUserData*>(P);
			if (!D) return;
				
			Result.FloorDist += 2.0f *  D->ShapeRadius * (1.0f - DP) / DP
				+ FVector::VectorPlaneProject(ProposedLinearVelocity* DeltaTime, Hit.ImpactNormal).Dot(UpDir);
		}*/
	}
	
	return ProposedLinearVelocity;
	
	
	
}

void UJoltNullMovementMode::SimulationTick_Implementation(const FJoltSimulationTickParams& Params, FJoltMoverTickEndData& OutputState)
{
}

#undef LOCTEXT_NAMESPACE
