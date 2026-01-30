
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JoltMoverComponent.h"
#include "JoltBridgeMain.h"	
#include "JoltCharacter.h"
#include "DefaultMovementSet/LayeredMoves/JoltMontageStateProvider.h"
#include "MovementModifiers/JoltStanceModifier.h"
#include "CharacterJoltMoverComponent.generated.h"

#define UE_API JOLTMOVER_API

class IJoltPrimitiveComponentInterface;
/**
 * Fires when a stance is changed, if stance handling is enabled (see @SetHandleStanceChanges)
 * Note: If a stance was just Activated it will fire with an invalid OldStance
 *		 If a stance was just Deactivated it will fire with an invalid NewStance
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FJoltMover_OnStanceChanged, EStanceMode, OldStance, EStanceMode, NewStance);



/** 
* Character Mover Component: this is a specialization of the core Mover Component that is set up with a 
* classic character in mind. Defaults and extended functionality, such as jumping and simple montage replication, 
* are intended to support features similar to UE's ACharacter actor type.
*/
UCLASS(MinimalAPI, BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class UCharacterJoltMoverComponent : public UJoltMoverComponent
{
	GENERATED_BODY()
	
public:
	UE_API UCharacterJoltMoverComponent();
	
	UE_API virtual void BeginPlay() override;


	// Returns whether this component is tasked with handling jump input or not
	UFUNCTION(BlueprintGetter)
	UE_API bool GetHandleJump() const;

	// If true, this component will handle default character inputs for jumping
	UFUNCTION(BlueprintSetter)
	UE_API void SetHandleJump(bool bInHandleJump);

	// Returns whether this component is tasked with handling character stance changes, including crouching
	UFUNCTION(BlueprintGetter)
	UE_API bool GetHandleStanceChanges() const;

	// If true, this component will process stancing changes and crouching inputs
	UFUNCTION(BlueprintSetter)
	UE_API void SetHandleStanceChanges(bool bInHandleStanceChanges);

	/** Returns true if currently crouching */ 
	UFUNCTION(BlueprintCallable, Category = Mover)
	UE_API virtual bool IsCrouching() const;

	/** Returns true if currently flying (moving through a non-fluid volume without resting on the ground) */
	UFUNCTION(BlueprintPure, Category = Mover)
	UE_API virtual bool IsFlying() const;
	
	// Is this actor in a falling state? Note that this includes upwards motion induced by jumping.
	UFUNCTION(BlueprintPure, Category = Mover)
	UE_API virtual bool IsFalling() const;

	// Is this actor in a airborne state? (e.g. Flying, Falling)
	UFUNCTION(BlueprintPure, Category = Mover)
	UE_API virtual bool IsAirborne() const;

	// Is this actor in a grounded state? (e.g. Walking)
	UFUNCTION(BlueprintPure, Category = Mover)
	UE_API virtual bool IsOnGround() const;

	// Is this actor in a Swimming state? (e.g. Swimming)
	UFUNCTION(BlueprintPure, Category = Mover)
	UE_API virtual bool IsSwimming() const;
	
	// Is this actor sliding on an unwalkable slope
	UFUNCTION(BlueprintPure, Category = Mover)
	UE_API virtual bool IsSlopeSliding() const;

	// Can this Actor jump?
	UFUNCTION(BlueprintPure, Category = Mover)
	UE_API virtual bool CanActorJump() const;

	// Perform jump on actor
	UFUNCTION(BlueprintCallable, Category = Mover)
	UE_API virtual bool Jump();
	
	// Whether this actor can currently crouch or not 
	UFUNCTION(BlueprintCallable, Category = Mover)
	UE_API virtual bool CanCrouch();
	
	// Perform crouch on actor
	UFUNCTION(BlueprintCallable, Category = Mover)
	UE_API virtual void Crouch();

	// Perform uncrouch on actor
	UFUNCTION(BlueprintCallable, Category = Mover)
	UE_API virtual void UnCrouch();
	
	UE_API virtual void SetLinearVelocity(const FVector Velocity) override;
	UE_API virtual void SetAngularVelocity(const FVector Velocity) override;
	UE_API virtual void SetTargetOrientation(const FRotator Rotation) override;
	UE_API virtual void PostPhysicsTick(FJoltMoverTickEndData& SimOutput) override;
	UE_API virtual void RestoreFrame(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState, const FJoltMoverTimeStep& NewBaseTimeStep) override;

	// Broadcast when this actor changes stances.
	UPROPERTY(BlueprintAssignable, Category = Mover)
	FJoltMover_OnStanceChanged OnStanceChanged;
	
protected:
	UFUNCTION()
	UE_API virtual void OnMoverPreSimulationTick(const FJoltMoverTimeStep& TimeStep, const FJoltMoverInputCmdContext& InputCmd);

	UFUNCTION()
	UE_API virtual void OnMoverPostFinalize(const FJoltMoverSyncState& SyncState, const FJoltMoverAuxStateContext& AuxState);

	UE_API virtual void OnHandlerSettingChanged();

	UE_API virtual void UpdateSyncedMontageState(const FJoltMoverTimeStep& TimeStep, const FJoltMoverSyncState& SyncState, const FJoltMoverAuxStateContext& AuxState);

	// ID used to keep track of the modifier responsible for crouching
	FJoltMovementModifierHandle StanceModifierHandle;

	/** If true, try to crouch (or keep crouching) on next update. If false, try to stop crouching on next update. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Jolt Mover|Crouch")
	uint8 bWantsToCrouch : 1 = 0;

	// Whether this component should directly handle jumping or not 
	UPROPERTY(EditAnywhere, BlueprintGetter = GetHandleJump, BlueprintSetter = SetHandleJump, Category = "Jolt Mover|Character")
	uint8 bHandleJump : 1 = 1;

	// Whether this component should directly handle stance changes, including crouching input
	UPROPERTY(EditAnywhere, BlueprintGetter = GetHandleStanceChanges, BlueprintSetter = SetHandleStanceChanges, Category = "Jolt Mover|Character")
	uint8 bHandleStanceChanges : 1 = 1;
	
	// Whether this component should directly handle stance changes, including crouching input
	UPROPERTY(EditAnywhere, Category = "Jolt Mover|Character")
	uint8 bUseJoltVirtualCharacter : 1 = 0;
	
	UPROPERTY(EditDefaultsOnly, Category="Jolt Mover|Physics Settings|Mass")
	float DefaultPushStrength = 100.0f;
	
	UPROPERTY(EditDefaultsOnly, Category="Jolt Mover|Physics Settings|Collision")
	uint8 bCanEverBeMoved : 1 = 1;
	
	UPROPERTY(EditDefaultsOnly, Category="Jolt Mover|Physics Settings|Collision")
	uint8 bCanEverApplyForcesToRigidBodies : 1 = 1;
	
	UPROPERTY(EditDefaultsOnly, Category="Jolt Mover|Physics Settings|Collision")
	uint8 MaxCollisionQueryIterations = 5;
	
	UPROPERTY(EditDefaultsOnly, Category="Jolt Mover|Physics Settings|Collision")
	uint8 MaxConstraintIterations = 15;
	
	UPROPERTY(EditDefaultsOnly, Category="Jolt Mover|Physics Settings|Collision")
	uint8 MaxConcurrentCollisions = 255;
	
	UPROPERTY(EditDefaultsOnly, Category="Jolt Mover|Physics Settings|Collision")
	float PredictiveContactQueryDistance = 10.f;
	
	UPROPERTY(EditDefaultsOnly, Category="Jolt Mover|Physics Settings|Collision")
	float CollisionTolerance = 1.0e-3f;
	
	UPROPERTY(EditDefaultsOnly, Category="Jolt Mover|Physics Settings|Collision")
	float CharacterPadding = 0.02f;
	
	UPROPERTY(EditDefaultsOnly, Category="Jolt Mover|Physics Settings|Collision")
	float PenetrationRecoverySpeed = 1.0f;

	/** Current state of replicated montage playback from an active movement mechanism (layered move, etc.) */
	UPROPERTY(Transient)
	FJoltMoverAnimMontageState SyncedMontageState;
	
	uint32 CharacterId = 0;
	JPH::CharacterVirtual* VirtualCharacter = nullptr;
	
	
#pragma region JOLT PHYSICS
	/* By default, this is true because the virtual shape does not register with the physics scene.
	 * This means that traces against it will fail. This being true allows for those collisions to work.
	 * If your character won't be traced against you can turn this off to save on performance.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Jolt Mover|Physics Settings")
	uint8 bNeedsJoltRigidBodyShape : 1 = 1;
	
	/*With jolt we need to create a shape for the crouch state. 
	 *This allows for some performance gains by not having dynamic shapes in memory. 
	 * Turning this off will bypass the creation of the crouch shape.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Jolt Mover|Physics Settings")
	uint8 bCanEverCrouch : 1 = 1;
	
#pragma endregion
	
protected:
	
	
	virtual void InitializeJoltCharacter();
	virtual void InitializeWithJolt() override;
	
	JPH::Ref<JPH::Shape> MakeNextCharacterShape(IJoltPrimitiveComponentInterface* Info);
	
public:
	
	
};



#undef UE_API
