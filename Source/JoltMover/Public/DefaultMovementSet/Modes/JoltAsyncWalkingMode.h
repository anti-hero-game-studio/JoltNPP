// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JoltMovementMode.h"
#include "MoveLibrary/JoltModularMovement.h"
#include "JoltKinematicWalkingMode.h"
#include "JoltAsyncWalkingMode.generated.h"

#define UE_API JOLTMOVER_API

class UJoltCommonLegacyMovementSettings;
struct FJoltFloorCheckResult;
struct FJoltRelativeBaseInfo;
struct FJoltMovementRecord;


/**
 * AsyncWalkingMode: a default movement mode for traversing surfaces and movement bases (walking, running, sneaking, etc.)
 * This mode simulates movement without actually modifying any scene component(s).
 */
UCLASS(MinimalAPI, Blueprintable, BlueprintType, Experimental)
class UJoltAsyncWalkingMode : public UJoltBaseMovementMode
{
	GENERATED_BODY()

public:
	UE_API UJoltAsyncWalkingMode(const FObjectInitializer& ObjectInitializer);
	
	UE_API virtual void GenerateMove_Implementation(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, FJoltProposedMove& OutProposedMove) const override;

	UE_API virtual void SimulationTick_Implementation(const FJoltSimulationTickParams& Params, FJoltMoverTickEndData& OutputState) override;

	// Returns the active turn generator. Note: you will need to cast the return value to the generator you expect to get, it can also be none
	UFUNCTION(BlueprintPure, Category=Mover)
	UE_API UObject* GetTurnGenerator();

	// Sets the active turn generator to use the class provided. Note: To set it back to the default implementation pass in none
	UFUNCTION(BlueprintCallable, Category=Mover)
	UE_API void SetTurnGeneratorClass(UPARAM(meta=(MustImplement="/Script/JoltMover.TurnGeneratorInterface", AllowAbstract="false")) TSubclassOf<UObject> TurnGeneratorClass);

protected:

	/** Choice of behavior for floor checks while not moving.  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	EJoltStaticFloorCheckPolicy FloorCheckPolicy = EJoltStaticFloorCheckPolicy::OnDynamicBaseOnly;

	/** Optional modular object for generating rotation towards desired orientation. If not specified, linear interpolation will be used. */
	UPROPERTY(EditAnywhere, Instanced, Category=Mover, meta=(ObjectMustImplement="/Script/JoltMover.TurnGeneratorInterface"))
	TObjectPtr<UObject> TurnGenerator;

	UE_API virtual void OnRegistered(const FName ModeName) override; 
	UE_API virtual void OnUnregistered() override;

	UE_API void CaptureFinalState(const FVector FinalLocation, const FRotator FinalRotation, bool bDidAttemptMovement, const FJoltFloorCheckResult& FloorResult, const FJoltMovementRecord& Record, const FVector& AngularVelocityDegrees, FJoltUpdatedMotionState& OutputSyncState) const;

	UE_API FJoltRelativeBaseInfo UpdateFloorAndBaseInfo(const FJoltFloorCheckResult& FloorResult) const;

	TWeakObjectPtr<const UJoltCommonLegacyMovementSettings> CommonLegacySettings;
};

#undef UE_API
