// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JoltSimpleWalkingMode.h"
#include "JoltSimpleSpringWalkingMode.generated.h"

#define UE_API JOLTMOVER_API

/**
 * A walking mode that uses a critically damped spring for translation and rotation.
 * The strength of the critically damped spring is set via smoothing times (separate for translation and rotation)
 */
UCLASS(BlueprintType)
class UJoltSimpleSpringWalkingMode : public UJoltSimpleWalkingMode
{
	GENERATED_BODY()

public:
	UE_API virtual void SimulationTick_Implementation(const FJoltSimulationTickParams& Params, FJoltMoverTickEndData& OutputState) override;
	UE_API virtual void GenerateWalkMove_Implementation(FJoltMoverTickStartData& StartState, float DeltaSeconds, const FVector& DesiredVelocity,
									 const FQuat& DesiredFacing, const FQuat& CurrentFacing, FVector& InOutAngularVelocityDegrees, FVector& InOutVelocity) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jolt Mover|Spring Settings", meta = (ClampMin = "0", UIMin = "0", ForceUnits = "s"))
	float VelocitySmoothingTime = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jolt Mover|Spring Settings", meta = (ClampMin = "0", UIMin = "0", ForceUnits = "s"))
	float FacingSmoothingTime = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jolt Mover|Spring Settings", meta = (ClampMin = "0", UIMin = "0", ForceUnits = "cm/s"))
	// Below this speed we set velocity to 0
	float VelocityDeadzoneThreshold = 0.1f;
};

#undef UE_API
