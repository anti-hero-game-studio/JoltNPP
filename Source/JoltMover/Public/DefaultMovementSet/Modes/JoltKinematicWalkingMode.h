// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JoltMovementMode.h"
#include "MoveLibrary/JoltModularMovement.h"
#include "JoltKinematicWalkingMode.generated.h"

#define UE_API JOLTMOVER_API

class UJoltCommonLegacyMovementSettings;
struct FJoltFloorCheckResult;
struct FJoltRelativeBaseInfo;
struct FJoltMovementRecord;

// Behavior policy for performing floor checks in walking mode when no movement is occurring. 
UENUM(BlueprintType)
enum class EJoltStaticFloorCheckPolicy : uint8
{
	// Always perform floor checks, even when not moving. You may want this if static bases may disappear from underneath.
	Always = 0				UMETA(DisplayName = "Always"),

	// Only perform floor checks when not moving IF we're on a dynamic movement base
	OnDynamicBaseOnly = 1	UMETA(DisplayName = "OnDynamicBaseOnly"),
};



/**
 * WalkingMode: a default movement mode for traversing surfaces and movement bases (walking, running, sneaking, etc.)
 */
UCLASS(MinimalAPI, Blueprintable, BlueprintType)
class UJoltKinematicWalkingMode : public UJoltBaseMovementMode
{
	GENERATED_UCLASS_BODY()

public:
	
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

	UE_API void CaptureFinalState(USceneComponent* UpdatedComponent, bool bDidAttemptMovement, const FJoltFloorCheckResult& FloorResult, const FJoltMovementRecord& Record, const FVector& AngularVelocityDegrees, FJoltUpdatedMotionState& OutputSyncState) const;

	UE_API FJoltRelativeBaseInfo UpdateFloorAndBaseInfo(const FJoltFloorCheckResult& FloorResult) const;

	TObjectPtr<const UJoltCommonLegacyMovementSettings> CommonLegacySettings;
};

#undef UE_API
