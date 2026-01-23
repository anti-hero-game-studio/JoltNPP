// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "JoltPhysicsGroundMovementUtils.generated.h"

#define UE_API JOLTMOVER_API

struct FJoltFloorCheckResult;
/**
 * 
 */
UCLASS(MinimalAPI)
class UJoltPhysicsGroundMovementUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	/** Computes the local velocity at the supplied position of the hit object in floor result */
	UFUNCTION(BlueprintCallable, Category = JoltMover, meta=(WorldContext = "WorldContextObject"))
	static UE_API FVector ComputeLocalGroundVelocity_Internal(const UObject* WorldContextObject, const FVector& Position, const FJoltFloorCheckResult& FloorResult);
	
};

#undef UE_API
