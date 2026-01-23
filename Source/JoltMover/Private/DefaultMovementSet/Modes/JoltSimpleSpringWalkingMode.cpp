// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultMovementSet/Modes/JoltSimpleSpringWalkingMode.h"
#include "DefaultMovementSet/Modes/JoltSimpleSpringState.h"
#include "JoltMoverComponent.h"
#include "DefaultMovementSet/Settings/JoltCommonLegacyMovementSettings.h"
#include "Animation/SpringMath.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltSimpleSpringWalkingMode)

void UJoltSimpleSpringWalkingMode::SimulationTick_Implementation(const FJoltSimulationTickParams& Params, FJoltMoverTickEndData& OutputState)
{
	Super::SimulationTick_Implementation(Params, OutputState);

	// We've already updated the spring state during GenerateMove, and just need to copy it into the output simulation state
	if (const FJoltSimpleSpringState* InSpringState = Params.StartState.SyncState.Collection.FindDataByType<FJoltSimpleSpringState>())
	{
		FJoltSimpleSpringState& OutputSpringState = OutputState.SyncState.Collection.FindOrAddMutableDataByType<FJoltSimpleSpringState>();
		OutputSpringState = *InSpringState;
	}
}

void UJoltSimpleSpringWalkingMode::GenerateWalkMove_Implementation(FJoltMoverTickStartData& StartState, float DeltaSeconds, const FVector& DesiredVelocity,
	const FQuat& DesiredFacing, const FQuat& CurrentFacing, FVector& InOutAngularVelocityDegrees, FVector& InOutVelocity)
{
	FJoltSimpleSpringState& SpringState = StartState.SyncState.Collection.FindOrAddMutableDataByType<FJoltSimpleSpringState>();

	// Linear //
	
	SpringMath::CriticalSpringDamper(InOutVelocity, SpringState.CurrentAccel, DesiredVelocity, VelocitySmoothingTime, DeltaSeconds);
	
	// Angular //
	
	FVector CurrentAngularVelocityRad = FMath::DegreesToRadians(InOutAngularVelocityDegrees);
	FQuat UpdatedFacing = CurrentFacing;
	SpringMath::CriticalSpringDamperQuat(UpdatedFacing, CurrentAngularVelocityRad, DesiredFacing, FacingSmoothingTime, DeltaSeconds);
	InOutAngularVelocityDegrees = FMath::RadiansToDegrees(CurrentAngularVelocityRad);
}

