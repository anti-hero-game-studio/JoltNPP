// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/BaseClasses/JoltCapsuleComponent.h"

#include "JoltBridgeLogChannels.h"
#include "Core/Singletons/JoltPhysicsWorldSubsystem.h"


UJoltCapsuleComponent::UJoltCapsuleComponent(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
	CapsuleHalfHeight = ColliderHeight;
	CapsuleRadius = ColliderRadius;
	SetGenerateOverlapEvents(JoltPhysicsBodySettings.bGenerateOverlapEventsInChaos);
}


void UJoltCapsuleComponent::InitializeComponent()
{
	Super::InitializeComponent();
	SetGenerateOverlapEvents(JoltPhysicsBodySettings.bGenerateOverlapEventsInChaos);
}

void UJoltCapsuleComponent::PostInitProperties()
{
	Super::PostInitProperties();
}

void UJoltCapsuleComponent::SetSimulatePhysics(const bool bSimulate)
{
	// If we are simulating physics, and we don't have a rigid body create it. If we do activate it.
	const UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>();
	
	if (!Subsystem) return;
	
	if (Mobility != EComponentMobility::Movable)
	{
		UE_LOG(LogJoltBridge, Error, TEXT("You are attempting to activate physics on a body not marked as movable"));
		return;
	}

	if (Subsystem->HasRigidBodyBeenCreated(this))
	{
		// Activate
		Subsystem->SetRigidBodyActiveState(this, bSimulate);
	}
	
}

bool UJoltCapsuleComponent::IsSimulatingPhysics(const FName BoneName) const
{
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return Super::IsSimulatingPhysics(BoneName);
	}
	
	const UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>();
	
	if (!Subsystem) return false;
	
	if (Mobility != EComponentMobility::Movable)
	{
		return false;
	}
	return Subsystem->IsCollisionBodyActive(this);
}

bool UJoltCapsuleComponent::IsAnySimulatingPhysics() const
{
	return Super::IsAnySimulatingPhysics();
}

bool UJoltCapsuleComponent::IsAnyRigidBodyAwake()
{
	return IsAnySimulatingPhysics();
}

ECollisionEnabled::Type UJoltCapsuleComponent::GetCollisionEnabled() const
{
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return Super::GetCollisionEnabled();
	}
	
	const UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>();
	
	if (!Subsystem) return Super::GetCollisionEnabled();
	
	if (!Subsystem->IsBodyValid(this))
	{
		return ECollisionEnabled::Type::NoCollision;
	}

	if (Subsystem->HasRigidBodyBeenCreated(this) && Subsystem->HasSensorBodyBeenCreated(this))
	{
		return ECollisionEnabled::Type::QueryAndPhysics;
	}
	
	if (Subsystem->HasSensorBodyBeenCreated(this) && !Subsystem->HasRigidBodyBeenCreated(this))
	{
		return ECollisionEnabled::Type::QueryOnly;
	}
	
	if (!Subsystem->HasSensorBodyBeenCreated(this) && Subsystem->HasRigidBodyBeenCreated(this))
	{
		return ECollisionEnabled::Type::PhysicsOnly;
	}
	
	
	return Super::GetCollisionEnabled();
}

ECollisionResponse UJoltCapsuleComponent::GetCollisionResponseToChannel(ECollisionChannel Channel) const
{
	const UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>();
	
	if (!Subsystem) return Super::GetCollisionResponseToChannel(Channel);
	
	return Subsystem->GetCollisionResponseContainer(this).GetResponse(Channel);
}

const FCollisionResponseContainer& UJoltCapsuleComponent::GetCollisionResponseToChannels() const
{
	const UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>();
	
	if (!Subsystem) return Super::GetCollisionResponseToChannels();
	
	return Subsystem->GetCollisionResponseContainer(this);
}



// Called when the game starts
void UJoltCapsuleComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...
	
}

bool UJoltCapsuleComponent::UpdateOverlapsImpl(const TOverlapArrayView* PendingOverlaps, bool bDoNotifies,
	const TOverlapArrayView* OverlapsAtEndLocation)
{
	if (!JoltPhysicsBodySettings.bGenerateOverlapEventsInChaos) return true;
	
	return Super::UpdateOverlapsImpl(PendingOverlaps, bDoNotifies, OverlapsAtEndLocation);
}


// Called every frame
void UJoltCapsuleComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

float UJoltCapsuleComponent::GetGroundTraceDistance() const
{
	const float Length = CapsuleHalfHeight * (1 - StepHeightRatio) * 0.5f + CapsuleHalfHeight * StepHeightRatio;
	return Length * GetRelativeScale3D().Z;
}

float UJoltCapsuleComponent::GetShapeHeight() const
{
	return GetScaledCapsuleHalfHeight();
}

float UJoltCapsuleComponent::GetShapeWidth() const
{
	return GetScaledCapsuleRadius();
}

void UJoltCapsuleComponent::WakeRigidBody(FName BoneName)
{
	if (UJoltPhysicsWorldSubsystem* S = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>())
	{
		S->WakeBody(this);
	}
}

#if WITH_EDITOR
void UJoltCapsuleComponent::RecalculateCollider()
{
	
	if (bUseFloatingShape)
	{
		CapsuleHalfHeight = ColliderHeight * (1 - StepHeightRatio);
		NewRelativeLocation = ColliderOffset+ FVector(0.f, 0.f, StepHeightRatio * ColliderHeight / 2.f);
		SetRelativeLocation(NewRelativeLocation);
		CapsuleRadius = ColliderRadius;

		if (CapsuleHalfHeight < CapsuleRadius)
		{
			CapsuleRadius = CapsuleHalfHeight;
		}
	}
	else
	{
		CapsuleHalfHeight = ColliderHeight;
		CapsuleRadius = ColliderRadius;
		if (CapsuleHalfHeight < CapsuleRadius)
		{
			CapsuleRadius = CapsuleHalfHeight;
		}
	}
	
	
	
	MarkRenderStateDirty();
	
	
}

void UJoltCapsuleComponent::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(ThisClass, StepHeightRatio))
	{
		RecalculateCollider();
	}
	else if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(ThisClass, ColliderHeight))
	{
		RecalculateCollider();
	}
	else if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(ThisClass, ColliderRadius))
	{
		RecalculateCollider();
	}
	else if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(ThisClass, ColliderOffset))
	{
		RecalculateCollider();
	}
	
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif
