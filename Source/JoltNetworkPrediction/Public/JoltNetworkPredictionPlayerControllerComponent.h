// 2025 Yohoho Productions /  Sirkai

#pragma once

#include "CoreMinimal.h"
#include "JoltNetworkPredictionReplicationProxy.h"
#include "JoltNetworkPredictionTickState.h"
#include "Components/ActorComponent.h"
#include "JoltNetworkPredictionPlayerControllerComponent.generated.h"

//


struct FJoltSerializedAckedFrames;
struct FJoltAckedFrames;
/*
 * This class is responsible for Handling the input for simulation associated with specific player controller
 * together , along with controlling data that should be unified per client not per simulation , such Last Received , Last consumed etc..
 * This should be added to the player controller class , "ToDo : if not it will be added as default class at runtime??"
 */

struct FJoltInputReceivers
{
	TMap<int32, TFunction<void(const int32&, const float&,
		const FJoltSimulationReplicatedInput&,const FJoltFixedTickState&)>> BoundReceivers;
};
UCLASS(ClassGroup=(NetworkPrediction), meta=(BlueprintSpawnableComponent))
class JOLTNETWORKPREDICTION_API UJoltNetworkPredictionPlayerControllerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UJoltNetworkPredictionPlayerControllerComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	//-------------- Input Handling --------------- //
	void SendServerRpc(const int32& Frame);
	UFUNCTION(Server,Unreliable)
	void Server_ReceivedInput(const int32& Frame,const float& InInterpolationTime,const TArray<FJoltSimulationReplicatedInput>& Inputs);
	int32 LastReceivedFrame = INDEX_NONE;
	int32 LastConsumedFrame = INDEX_NONE;
	float InterpolationTimeMS = 0.0f;
	void AdvanceLastConsumedFrame(const int32& MaxBufferSize);
	void AddInputToSend(const int32& ID, const uint32& DataSize , const TArray<uint8>& Data);

	// Register by ID directly
	void RegisterInputReceiver(int32 ID, TFunction<void(
		const int32&, const float&, const FJoltSimulationReplicatedInput&,
		const FJoltFixedTickState&)> Receiver)
	{
		InputReceivers.BoundReceivers.Add(ID, MoveTemp(Receiver));
	}
	void UnregisterInputReceiver(int32 ID)
	{
		InputReceivers.BoundReceivers.Remove(ID);
	}

	bool IsInputReceiverRegistered(const int32& ID) const {return InputReceivers.BoundReceivers.Contains(ID);}
	//-------------- End Input Handling --------------- //
	
	//-------------- Time Dilation --------------- //
	void UpdateTimeDilation(const float& InTimeDilation);

	//-------------- Delta Serialization --------------- //
	void SendAckedFrames(const FJoltSerializedAckedFrames& AckedFrames);
	UFUNCTION(Server,Unreliable)
	void Server_ReceivedAckedFrames(const FJoltSerializedAckedFrames& AckedFrames);
	//-------------- End Delta Serialization --------------- //

	UNetConnection* GetNetConnection() const;

	
private:
	// Each simulation that has input to send, will add its ID and its input data packed to this array which will be sent
	// by the RPC and received then unpacked on the server.
	TArray<FJoltSimulationReplicatedInput> InputsToSend;

	UPROPERTY(ReplicatedUsing=OnRep_TimeDilation)
	FJoltSimTimeDilation TimeDilation;
	
	UFUNCTION()
	void OnRep_TimeDilation();
	FJoltInputReceivers InputReceivers;
};
