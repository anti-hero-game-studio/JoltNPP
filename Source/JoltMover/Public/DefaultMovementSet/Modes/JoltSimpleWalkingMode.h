// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DefaultMovementSet/Modes/JoltKinematicWalkingMode.h"
#include "JoltSimpleWalkingMode.generated.h"

#define UE_API JOLTMOVER_API

/**
 * Basic walking mode that implements the ground based walking
 */
UCLASS(BlueprintType, Abstract)
class UJoltSimpleWalkingMode : public UJoltKinematicWalkingMode
{
	GENERATED_BODY()

public:

	UE_API virtual void GenerateMove_Implementation(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, FJoltProposedMove& OutProposedMove) const override;

	// Override this to make a simple walking mode
	UFUNCTION(BlueprintNativeEvent, meta = (DisplayName = "Generate Simple Walk Move"))
	UE_API void GenerateWalkMove(UPARAM(ref) FJoltMoverTickStartData& StartState, float DeltaSeconds, const FVector& DesiredVelocity,
	                                     const FQuat& DesiredFacing, const FQuat& CurrentFacing, UPARAM(ref) FVector& InOutAngularVelocityDegrees, UPARAM(ref) FVector& InOutVelocity);

	// If this value is greater or equal to 0, this will override the max speed read from the common legacy shared walk settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jolt Mover|Walking Settings", meta = (ForceUnits = "cm/s"))
	float MaxSpeedOverride = -1.0f;
};

#undef UE_API
