// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "JoltPhysicsCharacterMovementMode.h"
#include "JoltFixedWalkingMode.generated.h"

class UJoltCommonLegacyMovementSettings;
/**
 * A walking mode that assumes the world is Z up and the floor plane is z = 0
 */
UCLASS()
class JOLTMOVER_API UJoltFixedWalkingMode : public UJoltPhysicsCharacterMovementMode
{
	GENERATED_BODY()
	
public:
	
	virtual void GenerateMove_Implementation(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, FJoltProposedMove& OutProposedMove) const override;
	
	
	virtual void SimulationTick_Implementation(const FJoltSimulationTickParams& Params, FJoltMoverTickEndData& OutputState) override;
	
	
	virtual void OnRegistered(const FName ModeName) override; 
	virtual void OnUnregistered() override;
	
	
	
protected:
	// Hover support (flat floor plane: Z = 0)
	UPROPERTY(EditAnywhere, Category="Hover|Plane")
	float FloorPlaneZ = 0.0f; // cm (assume 0 for your case)

	UPROPERTY(EditAnywhere, Category="Hover|Plane")
	float TargetHoverHeight = 8.0f; // cm, desired capsule-base height above floor plane

	UPROPERTY(EditAnywhere, Category="Hover|Plane")
	float HoverHeightTolerance = 0.25f; // cm, deadzone to prevent micro jitter

	// Suspension controller (spring-damper in vertical axis)
	UPROPERTY(EditAnywhere, Category="Hover|Suspension")
	float SuspensionStiffness = 200.0f; // (cm/s^2) per cm of height error

	UPROPERTY(EditAnywhere, Category="Hover|Suspension")
	float SuspensionDamping = 12.0f; // (1/s) damping against vertical speed

	UPROPERTY(EditAnywhere, Category="Hover|Suspension")
	float MaxUpwardAcceleration = 6000.0f; // cm/s^2

	UPROPERTY(EditAnywhere, Category="Hover|Suspension")
	float MaxDownwardAcceleration = 9000.0f; // cm/s^2

	// Clamp how much vertical velocity can change per fixed step (prevents popping)
	UPROPERTY(EditAnywhere, Category="Hover|Suspension")
	float MaxUpwardVelocityChangePerStep = 200.0f; // cm/s

	UPROPERTY(EditAnywhere, Category="Hover|Suspension")
	float MaxDownwardVelocityChangePerStep = 300.0f; // cm/s

	// Pop suppression (explicitly cancel upward velocity while supported)
	UPROPERTY(EditAnywhere, Category="Hover|PopSuppression")
	float CancelUpwardVelocityWhenSupportedThreshold = 5.0f; // cm/s

	UPROPERTY(EditAnywhere, Category="Hover|PopSuppression")
	float MaxUpwardVelocityCancelPerStep = 250.0f; // cm/s

	// Optional: if you want to allow “jump” to bypass support rules
	UPROPERTY(EditAnywhere, Category="Hover|PopSuppression")
	float JumpSupportDisableTimeSeconds = 0.0f; // 0 = disabled
	
	
	TObjectPtr<const UJoltCommonLegacyMovementSettings> CommonLegacySettings;
	
};
