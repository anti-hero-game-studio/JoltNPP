// 2025 Yohoho Productions /  Sirkai


#include "JoltNetworkPredictionPlayerControllerComponent.h"

#include "JoltNetworkPredictionWorldManager.h"


// Sets default values for this component's properties
UJoltNetworkPredictionPlayerControllerComponent::UJoltNetworkPredictionPlayerControllerComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);

	// ...
}


// Called when the game starts
void UJoltNetworkPredictionPlayerControllerComponent::BeginPlay()
{
	Super::BeginPlay();
	// ...
	
}

void UJoltNetworkPredictionPlayerControllerComponent::OnRegister()
{
	Super::OnRegister();

	UJoltNetworkPredictionWorldManager* Manager = GetWorld()->GetSubsystem<UJoltNetworkPredictionWorldManager>();
	APlayerController* OwningPlayerController = Cast<APlayerController>(GetOwner());
	if (OwningPlayerController && Manager)
	{
		Manager->RegisterRPCHandler(this);
	}
}

void UJoltNetworkPredictionPlayerControllerComponent::OnUnregister()
{
	Super::OnUnregister();
	UJoltNetworkPredictionWorldManager* Manager = GetWorld()->GetSubsystem<UJoltNetworkPredictionWorldManager>();
	APlayerController* OwningPlayerController = Cast<APlayerController>(GetOwner());
	if (OwningPlayerController && Manager)
	{
		Manager->UnRegisterRPCHandler(this);
	}
}


// Called every frame
void UJoltNetworkPredictionPlayerControllerComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                                                 FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// ...
}

void UJoltNetworkPredictionPlayerControllerComponent::GetLifetimeReplicatedProps(
	TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UJoltNetworkPredictionPlayerControllerComponent, TimeDilation);
}

void UJoltNetworkPredictionPlayerControllerComponent::SendServerRpc(const int32& Frame)
{
	Server_ReceivedInput(Frame,InterpolationTimeMS,InputsToSend);
	InputsToSend.Reset();
}

void UJoltNetworkPredictionPlayerControllerComponent::SendAckedFrames(const FJoltSerializedAckedFrames& AckedFrames)
{
	Server_ReceivedAckedFrames(AckedFrames);
}

void UJoltNetworkPredictionPlayerControllerComponent::Server_ReceivedAckedFrames_Implementation(
	const FJoltSerializedAckedFrames& AckedFrames)
{
	UJoltNetworkPredictionWorldManager* Manager = GetWorld()->GetSubsystem<UJoltNetworkPredictionWorldManager>();
	if (Manager)
	{
		Manager->OnReceivedAckedData(AckedFrames,this);
	}
}

void UJoltNetworkPredictionPlayerControllerComponent::Server_ReceivedInput_Implementation(const int32& Frame,const float& InInterpolationTime,
                                                                                      const TArray<FJoltSimulationReplicatedInput>& Inputs)
{
	UJoltNetworkPredictionWorldManager* Manager = GetWorld()->GetSubsystem<UJoltNetworkPredictionWorldManager>();

	/*for (const FJoltSimulationReplicatedInput& Input : Inputs)
	{
		if (TFunction<void(
		const int32&, const float&, const FJoltSimulationReplicatedInput&,
		UJoltNetworkPredictionPlayerControllerComponent*, const FJoltFixedTickState& TickState)>* Receiver = InputReceivers.BoundReceivers.Find(Input.ID))
		{
			(*Receiver)(Frame, InterpolationTime, Input,Manager->GetFixedTickState());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("No handler registered for Input ID %d"), Input.ID);
		}
	}*/
	if (Manager)
	{
		Manager->OnInputReceived(Frame,InInterpolationTime,Inputs,this);
	}
}


UNetConnection* UJoltNetworkPredictionPlayerControllerComponent::GetNetConnection() const
{
	if (APlayerController* OwningPlayerController = Cast<APlayerController>(GetOwner()))
	{
		return OwningPlayerController->NetConnection;
	}
	return nullptr;
}

void UJoltNetworkPredictionPlayerControllerComponent::UpdateTimeDilation(const float& InTimeDilation)
{
	if (GetOwner()->GetLocalRole() != ROLE_Authority)
	{
		return;
	}
	TimeDilation.UpdateTimeDilation(InTimeDilation);
}

void UJoltNetworkPredictionPlayerControllerComponent::OnRep_TimeDilation()
{
	if (GetOwner()->GetLocalRole() == ROLE_Authority)
	{
		return;
	}
	UJoltNetworkPredictionWorldManager* Manager = GetWorld()->GetSubsystem<UJoltNetworkPredictionWorldManager>();
	if (Manager)
	{
		Manager->SetTimeDilation(TimeDilation);
	}
}


void UJoltNetworkPredictionPlayerControllerComponent::AdvanceLastConsumedFrame(const int32& MaxBufferSize)
{
	if (LastReceivedFrame == INDEX_NONE)
	{
		return;
	}
	if (LastConsumedFrame >= LastReceivedFrame)
	{
		//ToDo Log input starvation.
		LastConsumedFrame = FMath::Max(LastReceivedFrame - 2,0);
		return;
	}
	if (LastReceivedFrame - LastConsumedFrame > FMath::Max(2,MaxBufferSize))
	{
		//ToDo Log Buffer Overflow
		LastConsumedFrame = LastReceivedFrame - 7;
		return;
	}
	LastConsumedFrame++;
}

void UJoltNetworkPredictionPlayerControllerComponent::AddInputToSend(const int32& ID, const uint32& DataSize,
	const TArray<uint8>& Data)
{
	InputsToSend.Add(FJoltSimulationReplicatedInput(ID, DataSize,Data));
}

