// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultMovementSet/NavJoltMoverComponent.h"
#include "AI/NavigationSystemBase.h"
#include "AI/Navigation/PathFollowingAgentInterface.h"
#include "Components/CapsuleComponent.h"
#include "DefaultMovementSet/InstantMovementEffects/JoltBasicInstantMovementEffects.h"
#include "DefaultMovementSet/Settings/JoltCommonLegacyMovementSettings.h"
#include "MoveLibrary/JoltMovementUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavJoltMoverComponent)

UNavJoltMoverComponent::UNavJoltMoverComponent()
{
	bWantsInitializeComponent = true;
	bAutoActivate = true;
}

void UNavJoltMoverComponent::InitializeComponent()
{
	Super::InitializeComponent();

	if (const AActor* MovementCompOwner = GetOwner())
	{
		MoverComponent = MovementCompOwner->FindComponentByClass<UJoltMoverComponent>();
	}
	
	if (!MoverComponent.IsValid())
	{
		UE_LOG(LogJoltMover, Warning, TEXT("NavMoverComponent on %s could not find a valid MoverComponent and will not function properly."), *GetNameSafe(GetOwner()));
	}
}

void UNavJoltMoverComponent::BeginPlay()
{
	Super::BeginPlay();

	if (MoverComponent.IsValid() && MoverComponent->GetUpdatedComponent())
	{
		UpdateNavAgent(*MoverComponent->GetUpdatedComponent());
	}
	else
	{
		UpdateNavAgent(*GetOwner());
	}
}

float UNavJoltMoverComponent::GetMaxSpeedForNavMovement() const
{
	float MaxSpeed = 0.0f;
	
	if (MoverComponent.IsValid())
	{
		if (const UJoltCommonLegacyMovementSettings* MovementSettings = MoverComponent.Get()->FindSharedSettings_Mutable<UJoltCommonLegacyMovementSettings>())
		{
			MaxSpeed = MovementSettings->MaxSpeed;
		}
	}

	return MaxSpeed;
}

void UNavJoltMoverComponent::StopMovementImmediately()
{
	if (MoverComponent.IsValid())
	{
		TSharedPtr<FJoltApplyVelocityEffect> VelocityEffect = MakeShared<FJoltApplyVelocityEffect>();
		MoverComponent->QueueInstantMovementEffect(VelocityEffect);
	}
	
	CachedNavMoveInputIntent = FVector::ZeroVector;
	CachedNavMoveInputVelocity = FVector::ZeroVector;
}

bool UNavJoltMoverComponent::ConsumeNavMovementData(FVector& OutMoveInputIntent, FVector& OutMoveInputVelocity)
{
	const bool bHasFrameAdvanced = GFrameCounter > GameFrameNavMovementConsumed;
	const bool bNoNewRequests = GameFrameNavMovementConsumed > GameFrameNavMovementRequested;
	bool bHasNavMovement = false;
	
	if (bHasFrameAdvanced && bNoNewRequests)
	{
		CachedNavMoveInputIntent = FVector::ZeroVector;
		CachedNavMoveInputVelocity = FVector::ZeroVector;
	}
	else
	{
		OutMoveInputIntent = CachedNavMoveInputIntent;
		OutMoveInputVelocity = CachedNavMoveInputVelocity;
		bHasNavMovement = true;
		
		UE_LOG(LogJoltMover, VeryVerbose, TEXT("Applying %s as NavMoveInputIntent."), *CachedNavMoveInputIntent.ToString());
		UE_LOG(LogJoltMover, VeryVerbose, TEXT("Applying %s as NavMoveInputVelocity."), *CachedNavMoveInputVelocity.ToString());
	}

	GameFrameNavMovementConsumed = GFrameCounter;

	return bHasNavMovement;
}

FVector UNavJoltMoverComponent::GetLocation() const
{
	if (MoverComponent.IsValid())
	{
		if (const USceneComponent* UpdatedComponent = MoverComponent->GetUpdatedComponent())
		{
			return UpdatedComponent->GetComponentLocation();
		}
	}
	
	return FVector(FLT_MAX);
}

FVector UNavJoltMoverComponent::GetFeetLocation() const
{
	if (MoverComponent.IsValid())
	{
		if (const USceneComponent* UpdatedComponent = MoverComponent->GetUpdatedComponent())
		{
			return UpdatedComponent->GetComponentLocation() - FVector(0,0,UpdatedComponent->Bounds.BoxExtent.Z);
		}
	}

	return FNavigationSystem::InvalidLocation;
}

FVector UNavJoltMoverComponent::GetFeetLocationAt(FVector ComponentLocation) const
{
	if (MoverComponent.IsValid())
	{
		if (const USceneComponent* UpdatedComponent = MoverComponent->GetUpdatedComponent())
		{
			return ComponentLocation - FVector(0, 0, UpdatedComponent->Bounds.BoxExtent.Z);
		}
	}
	
	return FNavigationSystem::InvalidLocation;
}

FBasedPosition UNavJoltMoverComponent::GetFeetLocationBased() const
{
	FBasedPosition BasedPosition(NULL, GetFeetLocation());
	
	if (MoverComponent.IsValid())
	{
		if (const UJoltMoverBlackboard* Blackboard = MoverComponent->GetSimBlackboard())
		{
			FJoltRelativeBaseInfo MovementBaseInfo;
			if (Blackboard->TryGet(CommonBlackboard::LastFoundDynamicMovementBase, MovementBaseInfo)) 
			{
				BasedPosition.Base = MovementBaseInfo.MovementBase->GetOwner();
				BasedPosition.Position = MovementBaseInfo.Location;
				BasedPosition.CachedBaseLocation = MovementBaseInfo.ContactLocalPosition;
				BasedPosition.CachedBaseRotation = MovementBaseInfo.Rotation.Rotator();
			}
		}
	}

	return BasedPosition;
}

void UNavJoltMoverComponent::UpdateNavAgent(const UObject& ObjectToUpdateFrom)
{
	if (!NavMovementProperties.bUpdateNavAgentWithOwnersCollision)
	{
		return;
	}
	
	if (const UCapsuleComponent* CapsuleComponent = Cast<UCapsuleComponent>(&ObjectToUpdateFrom))
	{
		NavAgentProps.AgentRadius = CapsuleComponent->GetScaledCapsuleRadius();
		NavAgentProps.AgentHeight = CapsuleComponent->GetScaledCapsuleHalfHeight() * 2.f;;
	}
	else if (const AActor* ObjectAsActor = Cast<AActor>(&ObjectToUpdateFrom))
	{
		ensureMsgf(&ObjectToUpdateFrom == GetOwner(), TEXT("Object passed to UpdateNavAgent should be the owner actor of the Nav Movement Component"));
		// Can't call GetSimpleCollisionCylinder(), because no components will be registered.
		float BoundRadius, BoundHalfHeight;	
		ObjectAsActor->GetSimpleCollisionCylinder(BoundRadius, BoundHalfHeight);
		NavAgentProps.AgentRadius = BoundRadius;
		NavAgentProps.AgentHeight = BoundHalfHeight * 2.f;
	}
}

void UNavJoltMoverComponent::RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed)
{
	if (MoveVelocity.SizeSquared() < UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	GameFrameNavMovementRequested = GFrameCounter;
	
	if (IsFalling())
	{
		const FVector FallVelocity = MoveVelocity.GetClampedToMaxSize(GetMaxSpeedForNavMovement());
		// TODO: NS - we may eventually need something to help with air control and pathfinding
		//PerformAirControlForPathFollowing(FallVelocity, FallVelocity.Z);
		CachedNavMoveInputVelocity = FallVelocity;
		return;
	}

	CachedNavMoveInputVelocity = MoveVelocity;
	
	if (IsMovingOnGround())
	{
		const FPlane MovementPlane(FVector::ZeroVector, FVector::UpVector);
		CachedNavMoveInputVelocity = UJoltMovementUtils::ConstrainToPlane(CachedNavMoveInputVelocity, MovementPlane, true);
	}
}

void UNavJoltMoverComponent::RequestPathMove(const FVector& MoveInput)
{
	FVector AdjustedMoveInput(MoveInput);

	// preserve magnitude when moving on ground/falling and requested input has Z component
	// see ConstrainInputAcceleration for details
	if (MoveInput.Z != 0.f && (IsMovingOnGround() || IsFalling()))
	{
		const float Mag = MoveInput.Size();
		AdjustedMoveInput = MoveInput.GetSafeNormal2D() * Mag;
	}

	GameFrameNavMovementRequested = GFrameCounter;
	CachedNavMoveInputIntent = AdjustedMoveInput.GetSafeNormal();
}

bool UNavJoltMoverComponent::CanStopPathFollowing() const
{
	return true;
}

void UNavJoltMoverComponent::SetPathFollowingAgent(IPathFollowingAgentInterface* InPathFollowingAgent)
{
	PathFollowingComp = InPathFollowingAgent;
}

IPathFollowingAgentInterface* UNavJoltMoverComponent::GetPathFollowingAgent()
{
	return PathFollowingComp.Get();
}

const IPathFollowingAgentInterface* UNavJoltMoverComponent::GetPathFollowingAgent() const
{
	return PathFollowingComp.Get();
}

const FNavAgentProperties& UNavJoltMoverComponent::GetNavAgentPropertiesRef() const
{
	return NavAgentProps;
}

FNavAgentProperties& UNavJoltMoverComponent::GetNavAgentPropertiesRef()
{
	return NavAgentProps;
}

void UNavJoltMoverComponent::ResetMoveState()
{
	MovementState = NavAgentProps;
}

bool UNavJoltMoverComponent::CanStartPathFollowing() const
{
	return true;
}

bool UNavJoltMoverComponent::IsCrouching() const
{
	return MoverComponent.IsValid() ? MoverComponent->HasGameplayTag(JoltMover_IsCrouching, true) : false;
}

bool UNavJoltMoverComponent::IsFalling() const
{
	return MoverComponent.IsValid() ? MoverComponent->HasGameplayTag(JoltMover_IsFalling, true) : false;
}

bool UNavJoltMoverComponent::IsMovingOnGround() const
{
	return MoverComponent.IsValid() ? MoverComponent->HasGameplayTag(JoltMover_IsOnGround, true) : false;
}

bool UNavJoltMoverComponent::IsSwimming() const
{
	return MoverComponent.IsValid() ? MoverComponent->HasGameplayTag(JoltMover_IsSwimming, true) : false;
}

bool UNavJoltMoverComponent::IsFlying() const
{
	return MoverComponent.IsValid() ? MoverComponent->HasGameplayTag(JoltMover_IsFlying, true) : false;
}

void UNavJoltMoverComponent::GetSimpleCollisionCylinder(float& CollisionRadius, float& CollisionHalfHeight) const
{
	GetOwner()->GetSimpleCollisionCylinder(CollisionRadius, CollisionHalfHeight);
}

FVector UNavJoltMoverComponent::GetSimpleCollisionCylinderExtent() const
{
	return GetOwner()->GetSimpleCollisionCylinderExtent();
}

FVector UNavJoltMoverComponent::GetForwardVector() const
{
	return GetOwner()->GetActorForwardVector();
}

FVector UNavJoltMoverComponent::GetVelocityForNavMovement() const
{
	return MoverComponent.IsValid() ? MoverComponent->GetVelocity() : FVector::ZeroVector;
}
