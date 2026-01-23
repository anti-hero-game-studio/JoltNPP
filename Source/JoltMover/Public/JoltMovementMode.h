// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "JoltMoverSimulationTypes.h"
#include "JoltMoverTypes.h"
#include "MoveLibrary/JoltMoverBlackboard.h"
#include "JoltMovementModeTransition.h"
#include "MoveLibrary/JoltFloorQueryUtils.h"
#include "UObject/Interface.h"
#include "Templates/SubclassOf.h"
#include "JoltMovementMode.generated.h"

#define UE_API JOLTMOVER_API


/**
 * UJoltMovementSettingsInterface: interface that must be implemented for any settings object to be shared between modes
 */
UINTERFACE(MinimalAPI, BlueprintType)
class UJoltMovementSettingsInterface : public UInterface
{
	GENERATED_BODY()
};

class IJoltMovementSettingsInterface
{
	GENERATED_BODY()

public:
	virtual FString GetDisplayName() const = 0;
};

UENUM(BlueprintType)
enum class EJoltMoverFrictionOverrideMode : uint8
{
	DoNotOverride,
	AlwaysOverrideToZero,
	OverrideToZeroWhenMoving,
};



/**
 * Base class for all movement modes, exposing simulation update methods for both C++ and blueprint extension
 */
UCLASS(MinimalAPI, Abstract, Within = JoltMoverComponent, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced)
class UJoltBaseMovementMode : public UObject
{
	GENERATED_BODY()

public:
	UE_API virtual UWorld* GetWorld() const override;
	
	UE_API virtual void OnRegistered(const FName ModeName);
	UE_API virtual void OnUnregistered();
	
	// These functions are called immediately when the state machine switches modes
	UE_API virtual void Activate();
	UE_API virtual void Deactivate();

	// These functions are called when the sync state is changed on the game thread
	// and a new mode is activated/deactivated
	UE_API virtual void Activate_External();
	UE_API virtual void Deactivate_External();
	
	UFUNCTION(BlueprintNativeEvent, meta = (DisplayName = "Generate Move", ForceAsFunction))
	UE_API void GenerateMove(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, UPARAM(ref) FJoltProposedMove& OutProposedMove) const;

	UFUNCTION(BlueprintNativeEvent, meta = (DisplayName = "Simulation Tick", ForceAsFunction))
	UE_API void SimulationTick(const FJoltSimulationTickParams& Params, UPARAM(ref) FJoltMoverTickEndData& OutputState);
	
	/** Gets the MoverComponent that owns this movement mode */
	UFUNCTION(BlueprintCallable, Category=Mover, meta=(DisplayName="Get Mover Component", ScriptName = GetMoverComponent))
	UE_API UJoltMoverComponent* K2_GetMoverComponent() const;

	/**
	 * Gets the outer mover component of the indicated type. Does not check on the type or the presence of the MoverComp outer. Safe to call on CDOs.
	 * Note: Since UJoltBaseMovementMode is declared "Within = MoverComponent", all instances of a mode except the CDO are guaranteed to have a valid MoverComponent outer.
	 */
	template<typename MoverT = UJoltMoverComponent UE_REQUIRES(std::is_base_of_v<MoverT, UJoltMoverComponent>)>
	MoverT* GetMoverComponent() const
	{
		return Cast<MoverT>(GetOuter());
	}

	/**
	 * Gets the outer mover component of the indicated type, checked for validity.
	 * Note: Since UJoltBaseMovementMode is declared "Within = MoverComponent", all instances of a mode except the CDO are guaranteed to have a valid MoverComponent outer.
	 */
	template<typename MoverT = UJoltMoverComponent UE_REQUIRES(std::is_base_of_v<MoverT, UJoltMoverComponent>)>
	MoverT& GetMoverComponentChecked() const
	{
		return *CastChecked<MoverT>(GetOuterUJoltMoverComponent());
	}

	/**
   	 * Check Movement Mode for a gameplay tag.
   	 *
   	 * @param TagToFind			Tag to check on the Mover systems
   	 * @param bExactMatch		If true, the tag has to be exactly present, if false then TagToFind will include it's parent tags while matching
   	 * 
   	 * @return True if the TagToFind was found
   	 */
	UE_API virtual bool HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const;

#if WITH_EDITOR
	UE_API virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif // WITH_EDITOR

	/** Settings object type that this mode depends on. May be shared with other movement modes. When the mode is added to a Mover Component, it will create a shared instance of this settings class. */
	UPROPERTY(EditDefaultsOnly, Category = Mover, meta = (MustImplement = "/Script/JoltMover.MovementSettingsInterface"))
	TArray<TSubclassOf<UObject>> SharedSettingsClasses;

	/** Transition checks for the current mode. Evaluated in order, stopping at the first successful transition check */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = Mover, meta = (FullyExpand = true))
	TArray<TObjectPtr<UJoltBaseMovementModeTransition>> Transitions;

	/** A list of gameplay tags associated with this movement mode */
	UPROPERTY(EditDefaultsOnly, Category = Mover)
	FGameplayTagContainer GameplayTags;

	/** 
	 * Whether this movement mode supports being part of an asynchronous movement simulation (running concurrently with the gameplay thread) 
	 * Specifically for the GenerateMove and SimulationTick functions
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Mover)
	bool bSupportsAsync = false;

protected:
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Activated", ScriptName = "OnActivated"))
	UE_API void K2_OnActivated();

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Deactivated", ScriptName = "OnDeactivated"))
	UE_API void K2_OnDeactivated();
	
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Registered", ScriptName = "OnRegistered"))
	UE_API void K2_OnRegistered(const FName ModeName);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Unregistered", ScriptName = "OnUnregistered"))
	UE_API void K2_OnUnregistered();
	
	
	void FloorCheck(const FVector& StartingLocation, const FVector& ProposedLinearVelocity, const float& DeltaTime, FJoltFloorCheckResult& Result) const;
	
	
};

/**
 * NullMovementMode: a default do-nothing mode used as a placeholder when no other mode is active
 */
 UCLASS(MinimalAPI, NotBlueprintable)
class UJoltNullMovementMode : public UJoltBaseMovementMode
{
	GENERATED_UCLASS_BODY()

public:
	UE_API virtual void SimulationTick_Implementation(const FJoltSimulationTickParams& Params, FJoltMoverTickEndData& OutputState) override;

	UE_API const static FName NullModeName;
};

#undef UE_API
