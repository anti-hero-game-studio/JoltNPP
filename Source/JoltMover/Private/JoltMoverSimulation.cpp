// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltMoverSimulation.h"

#include "MoveLibrary/JoltMoverBlackboard.h"
#include "MoveLibrary/JoltRollbackBlackboard.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMoverSimulation)

UJoltMoverSimulation::UJoltMoverSimulation()
{
	Blackboard = CreateDefaultSubobject<UJoltMoverBlackboard>(TEXT("JoltMoverSimulationBlackboard"));
}

const UJoltMoverBlackboard* UJoltMoverSimulation::GetBlackboard() const
{
	return Blackboard;
}

UJoltMoverBlackboard* UJoltMoverSimulation::GetBlackboard_Mutable()
{
	return Blackboard;
}

const UJoltRollbackBlackboard_InternalWrapper* UJoltMoverSimulation::GetRollbackBlackboard() const
{
	return RollbackBlackboard;
}

UJoltRollbackBlackboard_InternalWrapper* UJoltMoverSimulation::GetRollbackBlackboard_Mutable()
{
	return RollbackBlackboard;
}

void UJoltMoverSimulation::SetRollbackBlackboard(UJoltRollbackBlackboard_InternalWrapper* RollbackSimBlackboard)
{
	RollbackBlackboard = RollbackSimBlackboard;
}