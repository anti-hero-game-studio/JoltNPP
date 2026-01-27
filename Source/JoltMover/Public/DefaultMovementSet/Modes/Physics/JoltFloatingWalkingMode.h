// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "JoltPhysicsCharacterMovementMode.h"
#include "JoltFloatingWalkingMode.generated.h"

class UJoltCommonLegacyMovementSettings;
/**
 * 
 */
UCLASS()
class JOLTMOVER_API UJoltFloatingWalkingMode : public UJoltPhysicsCharacterMovementMode
{
	GENERATED_BODY()
	
public:
	
	virtual void GenerateMove_Implementation(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, FJoltProposedMove& OutProposedMove) const override;
	virtual void SimulationTick_Implementation(const FJoltSimulationTickParams& Params, FJoltMoverTickEndData& OutputState) override;
	
	
	virtual void OnRegistered(const FName ModeName) override; 
	virtual void OnUnregistered() override;
	
protected:
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spring Settings")
	float RideHeight = 10.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spring Settings")
	float RideSpringStrength = 10.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spring Settings")
	float RideSpringDamper = 10.f;
	
	TWeakObjectPtr<const UJoltCommonLegacyMovementSettings> CommonLegacySettings;
	
};
