// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JoltLayeredMove.h"
#include "NativeGameplayTags.h"
#include "RootMotionModifier.h"
#include "JoltRootMotionAttributeLayeredMove.generated.h"

#define UE_API JOLTMOVER_API

JOLTMOVER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(JoltMover_AnimRootMotion_MeshAttribute);

/** 
 * Root Motion Attribute Move: handles root motion from a mesh's custom attribute, ignoring scaling.
 * Currently only supports Independent ticking mode, and allows controlled movement while jumping/falling or when a SkipAnimRootMotion tag is active.
 */
USTRUCT(BlueprintType)
struct FJoltLayeredMove_RootMotionAttribute : public FJoltLayeredMoveBase
{
	GENERATED_BODY()

	UE_API FJoltLayeredMove_RootMotionAttribute();
	virtual ~FJoltLayeredMove_RootMotionAttribute() {}

	// If true, any root motion rotations will be projected onto the movement plane (in worldspace), relative to the "up" direction. Otherwise, they'll be taken as-is.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	bool bConstrainWorldRotToMovementPlane = true;

protected:
	// These member variables are NOT replicated. They are used if we rollback and resimulate when the root motion attribute is no longer in sync.
	bool bDidAttrHaveRootMotionForResim = false;
	FTransform LocalRootMotionForResim;
	FMotionWarpingUpdateContext WarpingContextForResim;

	// Generate a movement 
	UE_API virtual bool GenerateMove(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, const UJoltMoverComponent* MoverComp, UJoltMoverBlackboard* SimBlackboard, FJoltProposedMove& OutProposedMove) override;

	UE_API virtual bool HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const override;

	UE_API virtual FJoltLayeredMoveBase* Clone() const override;

	UE_API virtual void NetSerialize(FArchive& Ar) override;

	UE_API virtual UScriptStruct* GetScriptStruct() const override;

	UE_API virtual FString ToSimpleString() const override;

	UE_API virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;
};

template<>
struct TStructOpsTypeTraits< FJoltLayeredMove_RootMotionAttribute > : public TStructOpsTypeTraitsBase2< FJoltLayeredMove_RootMotionAttribute >
{
	enum
	{
		WithCopy = true
	};
};

#undef UE_API
