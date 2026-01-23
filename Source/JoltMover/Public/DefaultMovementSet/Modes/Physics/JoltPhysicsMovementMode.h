// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "JoltMovementMode.h"
#include "JoltPhysicsMovementMode.generated.h"

/**
 * 
 */
UCLASS(Abstract, Within = JoltMoverComponent, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced)
class JOLTMOVER_API UJoltPhysicsMovementMode : public UJoltBaseMovementMode
{
	GENERATED_BODY()
	
	
public:
	
	virtual float GetMaxSpeed() const;
	virtual void OverrideMaxSpeed(float Value);
	virtual void ClearMaxSpeedOverride();

	virtual float GetAcceleration() const;
	virtual void OverrideAcceleration(float Value);
	virtual void ClearAccelerationOverride();
	
	EJoltMoverFrictionOverrideMode GetFrictionOverrideMode() const {return FrictionOverrideMode; };
	
	
protected:
	
	// Allows the mode to override friction on collision with other physics bodies.
	UPROPERTY(EditAnywhere, Category = "Collision Settings")
	EJoltMoverFrictionOverrideMode FrictionOverrideMode = EJoltMoverFrictionOverrideMode::OverrideToZeroWhenMoving;

private:
	TOptional<float> MaxSpeedOverride;
	TOptional<float> AccelerationOverride;
};
