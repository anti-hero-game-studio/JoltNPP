// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "JoltPhysicsMovementMode.h"
#include "UObject/Object.h"
#include "JoltPhysicsCharacterMovementMode.generated.h"



/**
 * 
 */
UCLASS()
class JOLTMOVER_API UJoltPhysicsCharacterMovementMode : public UJoltPhysicsMovementMode
{
	GENERATED_BODY()
	
public:
	
	virtual float GetTargetHeight() const
	{
		return TargetHeight;
	}

	virtual float GetGroundQueryRadius() const
	{
		return QueryRadius;
	}

	virtual float GetMaxWalkSlopeCosine() const;

	virtual bool ShouldCharacterRemainUpright() const
	{
		return bShouldCharacterRemainUpright;
	}
	

protected:
	void SetTargetHeightOverride(float InTargetHeight);
	void ClearTargetHeightOverride();

	void SetQueryRadiusOverride(float InQueryRadius);
	void ClearQueryRadiusOverride();
	
	
	// Maximum force the character can apply to reach the motion target
	UPROPERTY(EditAnywhere, Category = "Constraint Settings", meta = (ClampMin = "0", UIMin = "0", ForceUnits = "Newtons"))
	float RadialForceLimit = 1500.0f;

	// Maximum torque the character can apply to rotate in air about the vertical axis
	UPROPERTY(EditAnywhere, Category = "Constraint Settings", meta = (ClampMin = "0", UIMin = "0", ForceUnits = "NewtonMeters"))
	float TwistTorqueLimit = 1000.0f;

	// Maximum torque the character can apply to remain upright
	UPROPERTY(EditAnywhere, Category = "Constraint Settings", meta = (ClampMin = "0", UIMin = "0", ForceUnits = "NewtonMeters"))
	float SwingTorqueLimit = 3000.0f;

	// Controls whether the character capsule is forced to remain upright
	UPROPERTY(EditAnywhere, Category = "Constraint Settings")
	bool bShouldCharacterRemainUpright = true;

	/**
	 * Optional override target height for the character (the desired distance from the center of the capsule to the floor).
	 * If left blank, the -Z offset of the owning character's skeletal mesh comp will be used automatically.
	 */
	UPROPERTY(EditAnywhere, Category = "Constraint Settings", meta = (ClampMin = "0", UIMin = "0", ForceUnits = "cm"))
	TOptional<float> TargetHeightOverride;

	// Radius used for ground queries
	UPROPERTY(EditAnywhere, Category = "Query Settings", meta = (ClampMin = "0", UIMin = "0", ForceUnits = "cm"))
	TOptional<float> QueryRadiusOverride;

	
private:
	float TargetHeight = 95.0f;
	float QueryRadius = 30.0f;

};
