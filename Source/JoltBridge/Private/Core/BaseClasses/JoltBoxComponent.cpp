// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/BaseClasses/JoltBoxComponent.h"

#include "JoltBridgeLogChannels.h"
#include "Core/Singletons/JoltPhysicsWorldSubsystem.h"


UJoltBoxComponent::UJoltBoxComponent(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
	
	SetGenerateOverlapEvents(ShapeOptions.bGenerateOverlapEventsInChaos);
}


void UJoltBoxComponent::InitializeComponent()
{
	Super::InitializeComponent();
	SetGenerateOverlapEvents(ShapeOptions.bGenerateOverlapEventsInChaos);
}

// Called when the game starts
void UJoltBoxComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...
	
}

bool UJoltBoxComponent::UpdateOverlapsImpl(const TOverlapArrayView* PendingOverlaps, bool bDoNotifies, const TOverlapArrayView* OverlapsAtEndLocation)
{
	if (!ShapeOptions.bGenerateOverlapEventsInChaos) return true;
	
	return Super::UpdateOverlapsImpl(PendingOverlaps, bDoNotifies, OverlapsAtEndLocation);
}


// Called every frame
void UJoltBoxComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                        FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

void UJoltBoxComponent::SetSimulatePhysics(const bool bSimulate)
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

bool UJoltBoxComponent::IsSimulatingPhysics(const FName BoneName) const
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

bool UJoltBoxComponent::IsAnySimulatingPhysics() const
{
	return Super::IsAnySimulatingPhysics();
}

bool UJoltBoxComponent::IsAnyRigidBodyAwake()
{
	return IsAnySimulatingPhysics();
}


ECollisionEnabled::Type UJoltBoxComponent::GetCollisionEnabled() const
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



ECollisionResponse UJoltBoxComponent::GetCollisionResponseToChannel(ECollisionChannel Channel) const
{
	const UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>();
	
	if (!Subsystem) return Super::GetCollisionResponseToChannel(Channel);
	
	return Subsystem->GetCollisionResponseContainer(this).GetResponse(Channel);
}

const FCollisionResponseContainer& UJoltBoxComponent::GetCollisionResponseToChannels() const
{
	const UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>();
	
	if (!Subsystem) return Super::GetCollisionResponseToChannels();
	
	return Subsystem->GetCollisionResponseContainer(this);
}



