// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JoltMovementModifier.h"
#include "JoltStanceModifier.generated.h"

#define UE_API JOLTMOVER_API

class UCapsuleComponent;
class UCharacterJoltMoverComponent;

UENUM(BlueprintType)
enum class EStanceMode : uint8
{
	// Invalid default stance
	Invalid = 0,
	// Actor goes into crouch
	Crouch,
	// Actor goes into prone - not currently implemented
	Prone,
};

/**
 * Stances: Applies settings to the actor to make them go into different stances like crouch or prone(not implemented), affects actor maxacceleration and capsule height
 * Note: This modifier currently uses the CDO of the actor to reset values to "standing" values.
 *		 This modifier also assumes the actor is using a capsule as it's updated component for now
 */
USTRUCT(BlueprintType)
struct FStanceModifier : public FJoltMovementModifierBase
{
	GENERATED_BODY()

public:
	UE_API FStanceModifier();
	virtual ~FStanceModifier() override {}

	EStanceMode ActiveStance;
	
	UE_API virtual bool HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const override;
	
	/** Fired when this modifier is activated. */
	UE_API virtual void OnStart(UJoltMoverComponent* MoverComp, const FJoltMoverTimeStep& TimeStep, const FJoltMoverSyncState& SyncState, const FJoltMoverAuxStateContext& AuxState) override;
	
	/** Fired when this modifier is deactivated. */
	UE_API virtual void OnEnd(UJoltMoverComponent* MoverComp, const FJoltMoverTimeStep& TimeStep, const FJoltMoverSyncState& SyncState, const FJoltMoverAuxStateContext& AuxState) override;
	
	/** Fired just before a Substep */
	UE_API virtual void OnPreMovement(UJoltMoverComponent* MoverComp, const FJoltMoverTimeStep& TimeStep) override;

	/** Fired after a Substep */
	UE_API virtual void OnPostMovement(UJoltMoverComponent* MoverComp, const FJoltMoverTimeStep& TimeStep, const FJoltMoverSyncState& SyncState, const FJoltMoverAuxStateContext& AuxState) override;
	
	// @return newly allocated copy of this FJoltMovementModifier. Must be overridden by child classes
	UE_API virtual FJoltMovementModifierBase* Clone() const override;

	UE_API virtual void NetSerialize(FArchive& Ar) override;

	UE_API virtual UScriptStruct* GetScriptStruct() const override;

	UE_API virtual FString ToSimpleString() const override;

	UE_API virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;

	UE_API virtual bool CanExpand(const UCharacterJoltMoverComponent* MoverComp) const;
	
	// Whether expanding should be from the base of the capsule or not
	UE_API virtual bool ShouldExpandingMaintainBase(const UCharacterJoltMoverComponent* MoverComp) const;

protected:
	// Modifies the updated component casted to a capsule component
	UE_API virtual void AdjustCapsule(UJoltMoverComponent* MoverComp, float OldHalfHeight, float NewHalfHeight, float NewEyeHeight);

	// Applies any movement settings like acceleration or max speed changes
	UE_API void ApplyMovementSettings(UJoltMoverComponent* MoverComp);
	
	// Reverts any movement settings like acceleration or max speed changes
	UE_API void RevertMovementSettings(UJoltMoverComponent* MoverComp);
};

template<>
struct TStructOpsTypeTraits< FStanceModifier > : public TStructOpsTypeTraitsBase2< FStanceModifier >
{
	enum
	{
		WithCopy = true
	};
};

#undef UE_API
