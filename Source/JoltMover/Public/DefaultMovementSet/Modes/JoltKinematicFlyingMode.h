// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JoltMovementMode.h"
#include "JoltMoverDataModelTypes.h"
#include "JoltKinematicFlyingMode.generated.h"

#define UE_API JOLTMOVER_API

class UJoltCommonLegacyMovementSettings;


/**
 * FlyingMode: a default movement mode for moving through the air freely, but still interacting with blocking geometry. The
 * moving actor will remain upright vs the movement plane.
 */
UCLASS(MinimalAPI, Blueprintable, BlueprintType)
class UJoltKinematicFlyingMode : public UJoltBaseMovementMode
{
	GENERATED_UCLASS_BODY()


public:
	/**
	 * If true, the actor will 'float' above any walkable surfaces to maintain the same height as ground-based modes. 
	 * This can prevent pops when transitioning to ground-based movement, at the cost of performing floor checks while flying.
	 */
	UPROPERTY(Category = Mover, EditAnywhere, BlueprintReadWrite)
	bool bRespectDistanceOverWalkableSurfaces = false;

	UE_API virtual void GenerateMove_Implementation(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, FJoltProposedMove& OutProposedMove) const override;

	UE_API virtual void SimulationTick_Implementation(const FJoltSimulationTickParams& Params, FJoltMoverTickEndData& OutputState) override;

protected:
	UE_API virtual void OnRegistered(const FName ModeName) override;
	UE_API virtual void OnUnregistered() override;

	UE_API void CaptureFinalState(USceneComponent* UpdatedComponent, FJoltMovementRecord& Record, const FJoltUpdatedMotionState& StartSyncState, const FVector& AngularVelocityDegrees, FJoltUpdatedMotionState& OutputSyncState, const float DeltaSeconds) const;

	TObjectPtr<const UJoltCommonLegacyMovementSettings> CommonLegacySettings;
};

#undef UE_API
