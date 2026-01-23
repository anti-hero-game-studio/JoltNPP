// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "JoltNetworkPredictionLagCompensationData.h"
#include "Components/ActorComponent.h"
#include "JoltNetworkPredictionLagCompensation.generated.h"

/*
 * Actor component responsible for updating history of specific actor , capturing and filling state and setting actor state from history
 * by default has location , rotation , collision extent.
 * Supports a specific state stopping rewind, if it has boolCanRewindFurther set to false. this is used to not rewind a player further
 * that did a specific action on that frame, such as dashed or teleported etc..
 * HasSimulation() defines whether this actor will be updated on fixed tick or on finalize with interpolation time stamp.
 * by default return false for simulated proxies so they update based on interpolation state.
 * CaptureState() fills the history state with actor data , such as actor transform etc..
 * SetOwningActorState() Sets the actor state to data saved from history, such as setting actor location, rotation and collision size
 *
 * NOTE : To understand why this is here and not directly using state buffers in network prediction plugin,
 * check comment of GetSyncStateAtTime() in NetworkPredictionWorldManager.h
 */
UCLASS(ClassGroup=(NetworkPrediction), meta=(BlueprintSpawnableComponent))
class JOLTNETWORKPREDICTION_API UJoltNetworkPredictionLagCompensation : public UActorComponent
{
	GENERATED_BODY()
	friend class UJoltNetworkPredictionWorldManager;
public:
	// Sets default values for this component's properties
	UJoltNetworkPredictionLagCompensation();

	UPROPERTY(EditAnywhere, BlueprintReadWrite,NoClear, Category = Settings,meta=(MetaStruct="/Script/JoltNetworkPrediction.NpLagCompensationData"))
	UScriptStruct* RewindDataType = FNpLagCompensationData::StaticStruct();

protected:

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	
	UFUNCTION(BlueprintCallable,Category=LagCompensation)
	void RegisterWithSubsystem();
	
	UFUNCTION(BlueprintCallable,Category=LagCompensation)
	void UnregisterWithSubsystem();

	virtual bool HasSimulation() const;

	void CaptureStateAndAddToHistory(const float& TimeStampMs);

	virtual void CaptureState(TSharedPtr<FNpLagCompensationData>& StateToFill);

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
							   FActorComponentTickFunction* ThisTickFunction) override;

	virtual void SetOwningActorState(const TSharedPtr<FNpLagCompensationData> TargetState);

	const FNpLagCompensationHistory& GetLagCompensationHistory() const {return History;};

	// Can Override this function and capture state directly instead of using last captured state
	virtual void CapturePreRewindState();

	// Triggered when owning actor gets rewound to the past
	void OnStartedRewind();

	// Triggered when owning actor gets unwind back to present
	void OnEndedRewind();

	TSharedPtr<FNpLagCompensationData> GetLatestOrAddEntry(const float& SimTimeMS);

	void WriteToLatest(const TSharedPtr<FNpLagCompensationData>& StateToOverride);

private:

	FNpLagCompensationHistory History;
	void InitializeHistory(const int32& MaxSize);
};
