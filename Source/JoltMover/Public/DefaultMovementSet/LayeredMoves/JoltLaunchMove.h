// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JoltLayeredMove.h"
#include "JoltLayeredMoveBase.h"
#include "JoltLaunchMove.generated.h"

#define UE_API JOLTMOVER_API

USTRUCT(Blueprintable)
struct FJoltLaunchMoveActivationParams : public FJoltLayeredMoveActivationParams
{
	GENERATED_BODY()

	/** Velocity to apply to the updated component. Could be additive or overriding depending on MixMode setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover, meta=(ForceUnits="cm/s"))
	FVector LaunchVelocity = FVector::ZeroVector;

	// Optional movement mode name to force the actor into before applying the impulse velocity.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	FName ForceMovementMode = NAME_None;
	
};

USTRUCT(Blueprintable)
struct FJoltLaunchMoveData : public FJoltLayeredMoveInstancedData
{
	GENERATED_BODY()

	//@todo DanH: This is boilerplate begging for a macro
	virtual FJoltLayeredMoveInstancedData* Clone() const override { return new FJoltLaunchMoveData(*this); }
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	
	/** Velocity to apply to the updated component. Could be additive or overriding depending on MixMode setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover, meta=(ForceUnits="cm/s"))
	FVector LaunchVelocity = FVector::ZeroVector;

	/** Optional movement mode name to force the actor into before applying the impulse velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	FName ForceMovementMode;
	
	virtual void ActivateFromContext(const FJoltLayeredMoveActivationParams* ActivationParams) override;
	
	virtual void NetSerialize(FArchive& Ar) override;
};

// TODO: Create data for this? Is it not needed?!
UCLASS()
class ULaunchMoveLogic : public UJoltLayeredMoveLogic
{
	GENERATED_BODY()

public:
	UE_API ULaunchMoveLogic();

	/** Velocity to apply to the updated component. Could be additive or overriding depending on MixMode setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover, meta=(ForceUnits="cm/s"))
	FVector LaunchVelocity = FVector::ZeroVector;

	//@todo DanH: Should forcing a mode be an option at the root UJoltLayeredMoveLogic?
	/** Optional movement mode name to force the actor into before applying the impulse velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	FName ForceMovementMode;
	
protected:
	UE_API virtual bool GenerateMove_Implementation(const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard, const FJoltMoverTickStartData& StartState, FJoltProposedMove& OutProposedMove) override;
};

/** Launch Move: provides an impulse velocity to the actor after (optionally) forcing them into a particular movement mode */
USTRUCT(BlueprintType)
struct FJoltLayeredMove_Launch : public FJoltLayeredMoveBase
{
	GENERATED_BODY()

	UE_API FJoltLayeredMove_Launch();
	virtual ~FJoltLayeredMove_Launch() {}

	// Velocity to apply to the actor. Could be additive or overriding depending on MixMode setting.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover, meta=(ForceUnits="cm/s"))
	FVector LaunchVelocity = FVector::ZeroVector;

	// Optional movement mode name to force the actor into before applying the impulse velocity.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	FName ForceMovementMode = NAME_None;

	// Generate a movement 
	UE_API virtual bool GenerateMove(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, const UJoltMoverComponent* MoverComp, UJoltMoverBlackboard* SimBlackboard, FJoltProposedMove& OutProposedMove) override;

	UE_API virtual FJoltLayeredMoveBase* Clone() const override;

	UE_API virtual void NetSerialize(FArchive& Ar) override;

	UE_API virtual UScriptStruct* GetScriptStruct() const override;

	UE_API virtual FString ToSimpleString() const override;

	UE_API virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;
};


template<>
struct TStructOpsTypeTraits< FJoltLayeredMove_Launch > : public TStructOpsTypeTraitsBase2< FJoltLayeredMove_Launch >
{
	enum
	{
		WithCopy = true
	};
};

#undef UE_API
