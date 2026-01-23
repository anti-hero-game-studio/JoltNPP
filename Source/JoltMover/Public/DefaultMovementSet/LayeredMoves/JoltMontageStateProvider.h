// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "JoltLayeredMove.h"
#include "JoltMontageStateProvider.generated.h"

#define UE_API JOLTMOVER_API

class UAnimMontage;


/** Data about montages that is replicated to simulated clients */
USTRUCT(BlueprintType)
struct FJoltMoverAnimMontageState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = Mover)
	TObjectPtr<UAnimMontage> Montage;

	// Montage position when started (in unscaled seconds). 
	UPROPERTY(BlueprintReadWrite, Category = Mover)
	float StartingMontagePosition = 0.0f;

	// Rate at which this montage is intended to play
	UPROPERTY(BlueprintReadWrite, Category = Mover)
	float PlayRate = 1.0f;

	// Current position (during playback only)
	UPROPERTY(BlueprintReadOnly, Category = Mover)
	float CurrentPosition = 0.0f;

	void Reset();
	void NetSerialize(FArchive& Ar);
};



/** Note this will become obsolete once layered move logic is represented by a uobject, allowing use of interface classes.  */
USTRUCT(BlueprintType)
struct FJoltLayeredMove_MontageStateProvider : public FJoltLayeredMoveBase
{
	GENERATED_BODY()

	virtual FJoltMoverAnimMontageState GetMontageState() const
	{
		checkNoEntry();
		return FJoltMoverAnimMontageState();
	}
};

#undef UE_API
