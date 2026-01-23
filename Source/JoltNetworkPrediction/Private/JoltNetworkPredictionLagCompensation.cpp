// Fill out your copyright notice in the Description page of Project Settings.


#include "JoltNetworkPredictionLagCompensation.h"
#include "JoltNetworkPredictionWorldManager.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"


// Sets default values for this component's properties
UJoltNetworkPredictionLagCompensation::UJoltNetworkPredictionLagCompensation()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}

void UJoltNetworkPredictionLagCompensation::OnRegister()
{
	Super::OnRegister();
	RegisterWithSubsystem();
}

void UJoltNetworkPredictionLagCompensation::OnUnregister()
{
	UnregisterWithSubsystem();
	Super::OnUnregister();
}


void UJoltNetworkPredictionLagCompensation::RegisterWithSubsystem()
{
	UJoltNetworkPredictionWorldManager* NpManager = GetWorld()->GetSubsystem<UJoltNetworkPredictionWorldManager>();
	if (NpManager)
	{
		NpManager->RegisterRewindableComponent(this);
	}
}


void UJoltNetworkPredictionLagCompensation::UnregisterWithSubsystem()
{
	UJoltNetworkPredictionWorldManager* NpManager = GetWorld()->GetSubsystem<UJoltNetworkPredictionWorldManager>();
	if (NpManager)
	{
		NpManager->UnregisterRewindableComponent(this);
	}
}


bool UJoltNetworkPredictionLagCompensation::HasSimulation() const
{
	return GetOwnerRole() != ROLE_SimulatedProxy;
}

void UJoltNetworkPredictionLagCompensation::CaptureStateAndAddToHistory(const float& TimeStampMs)
{
	TSharedPtr<FNpLagCompensationData> State = GetLatestOrAddEntry(TimeStampMs);
	CaptureState(State);
	State->SimTimeMs = TimeStampMs;
	WriteToLatest(State);
}

void UJoltNetworkPredictionLagCompensation::CaptureState(TSharedPtr<FNpLagCompensationData>& StateToFill)
{
	StateToFill->Location = GetOwner()->GetActorLocation();
	StateToFill->Rotation = GetOwner()->GetActorQuat();
	if (UPrimitiveComponent* Comp = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()))
	{
		StateToFill->CollisionExtent = Comp->GetCollisionShape().GetExtent();
	}
	else
	{
		StateToFill->CollisionExtent = GetOwner()->GetSimpleCollisionCylinderExtent();
	}
	StateToFill->CanRewindFurther = true;
}

// Called every frame
void UJoltNetworkPredictionLagCompensation::TickComponent(float DeltaTime, ELevelTick TickType,
                                         FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

void UJoltNetworkPredictionLagCompensation::SetOwningActorState(const TSharedPtr<FNpLagCompensationData> TargetState)
{
	if (TargetState == nullptr)
	{
		return;
	}
	GetOwner()->SetActorLocation(TargetState->Location);
	GetOwner()->SetActorRotation(TargetState->Rotation);
	if (UPrimitiveComponent* OwnerCollision = GetOwner()->GetComponentByClass<UPrimitiveComponent>())
	{
		if (UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(OwnerCollision))
		{
			Capsule->SetCapsuleSize(TargetState->CollisionExtent.X, TargetState->CollisionExtent.Z);
			return;
		}
		if (UBoxComponent* BoxComp = Cast<UBoxComponent>(OwnerCollision))
		{
			BoxComp->SetBoxExtent(TargetState->CollisionExtent);
			return;
		}
		if (USphereComponent* SphereComp = Cast<USphereComponent>(OwnerCollision))
		{
			SphereComp->SetSphereRadius(TargetState->CollisionExtent.X);
		}
	}
	
}

void UJoltNetworkPredictionLagCompensation::CapturePreRewindState()
{
	const int32 LastIndex = History.Num() - 1;
	History.PreRewindData = TSharedPtr<FNpLagCompensationData>(History.GetAt(LastIndex)->Clone());
}

void UJoltNetworkPredictionLagCompensation::OnStartedRewind()
{
	History.bIsInRewind = true;
}

void UJoltNetworkPredictionLagCompensation::OnEndedRewind()
{
	History.bIsInRewind = false;
}

TSharedPtr<FNpLagCompensationData> UJoltNetworkPredictionLagCompensation::GetLatestOrAddEntry(const float& SimTimeMS)
{
	return History.GetLatestOrAddCopy(SimTimeMS);
}

void UJoltNetworkPredictionLagCompensation::WriteToLatest(const TSharedPtr<FNpLagCompensationData>& StateToOverride)
{
	History.WriteToLatestState(StateToOverride);
}

void UJoltNetworkPredictionLagCompensation::InitializeHistory(const int32& MaxSize)
{
	History = FNpLagCompensationHistory(RewindDataType, 128);
}

