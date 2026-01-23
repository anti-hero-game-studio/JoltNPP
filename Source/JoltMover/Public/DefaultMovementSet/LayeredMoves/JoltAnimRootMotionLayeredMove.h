// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "JoltLayeredMove.h"
#include "DefaultMovementSet/LayeredMoves/JoltMontageStateProvider.h"
#include "JoltAnimRootMotionLayeredMove.generated.h"

#define UE_API JOLTMOVER_API

class UAnimMontage;


/** Anim Root Motion Move: handles root motion from a montage played on the primary visual component (skeletal mesh). 
 * In this method, root motion is extracted independently from anim playback. The move will end itself if the animation
 * is interrupted on the mesh.
 */
USTRUCT(BlueprintType)
struct FJoltLayeredMove_AnimRootMotion : public FJoltLayeredMove_MontageStateProvider
{
	GENERATED_BODY()

	UE_API FJoltLayeredMove_AnimRootMotion();
	virtual ~FJoltLayeredMove_AnimRootMotion() {}

	UPROPERTY(BlueprintReadWrite, Category = Mover)
	FJoltMoverAnimMontageState MontageState;

	// Generate a movement 
	UE_API virtual bool GenerateMove(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, const UJoltMoverComponent* MoverComp, UJoltMoverBlackboard* SimBlackboard, FJoltProposedMove& OutProposedMove) override;

	UE_API virtual FJoltLayeredMoveBase* Clone() const override;

	UE_API virtual void NetSerialize(FArchive& Ar) override;

	UE_API virtual UScriptStruct* GetScriptStruct() const override;

	UE_API virtual FString ToSimpleString() const override;

	UE_API virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;

	// FJoltLayeredMove_MontageStateProvider
	UE_API virtual FJoltMoverAnimMontageState GetMontageState() const override;
};

template<>
struct TStructOpsTypeTraits< FJoltLayeredMove_AnimRootMotion > : public TStructOpsTypeTraitsBase2< FJoltLayeredMove_AnimRootMotion >
{
	enum
	{
		WithCopy = true
	};
};

#undef UE_API
