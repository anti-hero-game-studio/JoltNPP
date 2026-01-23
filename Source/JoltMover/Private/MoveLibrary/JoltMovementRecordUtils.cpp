// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoveLibrary/JoltMovementRecordUtils.h"
#include "MoveLibrary/JoltMovementRecord.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMovementRecordUtils)

void UJoltMovementRecordUtils::K2_SetDeltaSeconds(FJoltMovementRecord& OutMovementRecord, float DeltaSeconds)
{
	OutMovementRecord.SetDeltaSeconds(DeltaSeconds);
}

const FVector& UJoltMovementRecordUtils::K2_GetTotalMoveDelta(const FJoltMovementRecord& MovementRecord)
{
	return MovementRecord.GetTotalMoveDelta();
}

const FVector& UJoltMovementRecordUtils::K2_GetRelevantMoveDelta(const FJoltMovementRecord& MovementRecord)
{
	return MovementRecord.GetRelevantMoveDelta();
}

FVector UJoltMovementRecordUtils::K2_GetRelevantVelocity(const FJoltMovementRecord& MovementRecord)
{
	return MovementRecord.GetRelevantVelocity();
}	
