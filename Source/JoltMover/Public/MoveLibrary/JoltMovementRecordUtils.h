// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "JoltMovementRecordUtils.generated.h"

#define UE_API JOLTMOVER_API

struct FJoltMovementRecord;

/**
 * MovementRecordUtils: a collection of stateless static BP-accessible functions for movement record related operations
 */
UCLASS(MinimalAPI)
class UJoltMovementRecordUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Sets the delta time these moves took place over */
	UFUNCTION(BlueprintCallable, Category = Mover, DisplayName="Set Delta Seconds")
	static UE_API void K2_SetDeltaSeconds(UPARAM(Ref) FJoltMovementRecord& OutMovementRecord, float DeltaSeconds);

	/** Get all the move delta applied to the actor/record */
	UFUNCTION(BlueprintPure, Category = Mover, DisplayName="Get Total Move Delta")
	static UE_API const FVector& K2_GetTotalMoveDelta(const FJoltMovementRecord& MovementRecord);

	/** Get relevant move delta applied to the actor/record */
	UFUNCTION(BlueprintPure, Category = Mover, DisplayName="Get Relevant Move Delta")
	static UE_API const FVector& K2_GetRelevantMoveDelta(const FJoltMovementRecord& MovementRecord);

	/** Get relevant velocity applied to the actor/record */
	UFUNCTION(BlueprintPure, Category = Mover, DisplayName="Get Relevant Velocity")
	static UE_API FVector K2_GetRelevantVelocity(const FJoltMovementRecord& MovementRecord);
};

#undef UE_API
