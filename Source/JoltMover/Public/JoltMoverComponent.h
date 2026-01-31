// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "JoltMoverDataModelTypes.h"

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "Components/ActorComponent.h"
#include "MotionWarpingAdapter.h"
#include "JoltMovementMode.h"
#include "JoltMoverTypes.h"
#include "JoltLayeredMove.h"
#include "JoltLayeredMoveBase.h"
#include "MoveLibrary/JoltBasedMovementUtils.h"
#include "MoveLibrary/JoltConstrainedMoveUtils.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "Engine/HitResult.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "JoltMovementModifier.h"
#include "Backends/JoltMoverBackendLiaison.h"
#include "UObject/WeakInterfacePtr.h"
#include "Templates/SharedPointer.h"
#include "JoltBridgeMain.h"
#include "JoltMoverComponent.generated.h"

struct FJoltMoverTimeStep;
struct FJoltInstantMovementEffect;
struct FJoltMoverSimulationEventData;
class UJoltMovementModeStateMachine;
class UJoltMovementMixer;
class UJoltRollbackBlackboard;
class UJoltRollbackBlackboard_InternalWrapper;

namespace JoltMoverComponentConstants
{
	extern const FVector DefaultGravityAccel;		// Fallback gravity if not determined by the component or world (cm/s^2)
	extern const FVector DefaultUpDir;				// Fallback up direction if not determined by the component or world (normalized)
}

// Fired just before a simulation tick, regardless of being a re-simulated frame or not.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FJoltMover_OnPreSimTick, const FJoltMoverTimeStep&, TimeStep, const FJoltMoverInputCmdContext&, InputCmd);

// Fired during a simulation tick, after the input is processed but before the actual move calculation.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FJoltMover_OnPreMovement, const FJoltMoverTimeStep&, TimeStep, const FJoltMoverInputCmdContext&, InputCmd, const FJoltMoverSyncState&, SyncState, const FJoltMoverAuxStateContext&, AuxState);

// Fired during a simulation tick, after movement has occurred but before the state is finalized, allowing changes to the output state.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FJoltMover_OnPostMovement, const FJoltMoverTimeStep&, TimeStep, FJoltMoverSyncState&, SyncState, FJoltMoverAuxStateContext&, AuxState);

// Fired after a simulation tick, regardless of being a re-simulated frame or not.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FJoltMover_OnPostSimTick, const FJoltMoverTimeStep&, TimeStep);

// Fired after a rollback. First param is the time step we've rolled back to. Second param is when we rolled back from, and represents a later frame that is no longer valid.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FJoltMover_OnPostSimRollback, const FJoltMoverTimeStep&, CurrentTimeStep, const FJoltMoverTimeStep&, ExpungedTimeStep);

// Fired after changing movement modes. First param is the name of the previous movement mode. Second is the name of the new movement mode. 
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FJoltMover_OnMovementModeChanged, const FName&, PreviousMovementModeName, const FName&, NewMovementModeName);

// Fired when a teleport has succeeded
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FJoltMover_OnTeleportSucceeded, const FVector&, FromLocation, const FQuat&, FromRotation, const FVector&, ToLocation, const FQuat&, ToRotation);

// Fired when a teleport has failed
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FJoltMover_OnTeleportFailed, const FVector&, FromLocation, const FQuat&, FromRotation, const FVector&, ToLocation, const FQuat&, ToRotation, ETeleportFailureReason, TeleportFailureReason);

// Fired after a transition has been triggered.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FJoltMover_OnTransitionTriggered, const UJoltBaseMovementModeTransition*, ModeTransition);

// Fired after a frame has been finalized. This may be a resimulation or not. No changes to state are possible. Guaranteed to be on the game thread.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FJoltMover_OnPostFinalize, const FJoltMoverSyncState&, SyncState, const FJoltMoverAuxStateContext&, AuxState);

// Fired after proposed movement has been generated (i.e. after movement modes and layered moves have generated movement and mixed together).
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FJoltMover_ProcessGeneratedMovement, const FJoltMoverTickStartData&, StartState, const FJoltMoverTimeStep&, TimeStep, FJoltProposedMove&, OutProposedMove);

// Fired when a new event has been received from the simulation. This is a C++ only dispatch of the generic event. To dispatch the event to BP, prefer exposing the concrete event
// via a dedicated dynamic delegate (like OnMovementModeChanged).
DECLARE_MULTICAST_DELEGATE_OneParam(FJoltMover_OnPostSimEventReceived, const FJoltMoverSimulationEventData&);

/**
 * 
 */
UCLASS(MinimalAPI, Blueprintable, BlueprintType, meta = (BlueprintSpawnableComponent))
class UJoltMoverComponent : public UActorComponent
{
	GENERATED_BODY()


public:	
	JOLTMOVER_API UJoltMoverComponent();

	JOLTMOVER_API virtual void InitializeComponent() override;
	JOLTMOVER_API virtual void UninitializeComponent() override;
	JOLTMOVER_API virtual void OnRegister() override;
	JOLTMOVER_API virtual void RegisterComponentTickFunctions(bool bRegister) override;
	JOLTMOVER_API virtual void PostLoad() override;
	JOLTMOVER_API virtual void OnModifyContacts();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	// Broadcast before each simulation tick.
	// Note - Guaranteed to run on the game thread (even in async simulation).
	UPROPERTY(BlueprintAssignable, Category = Mover)
	FJoltMover_OnPreSimTick OnPreSimulationTick;

	// Broadcast at the end of a simulation tick after movement has occurred, but allowing additions/modifications to the state. Not assignable as a BP event due to it having mutable parameters.
	UPROPERTY()
	FJoltMover_OnPostMovement OnPostMovement;

	// Broadcast after each simulation tick and the state is finalized
	UPROPERTY(BlueprintAssignable, Category = Mover)
	FJoltMover_OnPostSimTick OnPostSimulationTick;

	// Broadcast when a rollback has occurred, just before the next simulation tick occurs
	UPROPERTY(BlueprintAssignable, Category = Mover)
	FJoltMover_OnPostSimRollback OnPostSimulationRollback;

	// Broadcast when a MovementMode has changed. Happens during a simulation tick if the mode changed that tick or when SetModeImmediately is used to change modes.
	UPROPERTY(BlueprintAssignable, Category = Mover)
	FJoltMover_OnMovementModeChanged OnMovementModeChanged;

	// Broadcast when a teleport has succeeded
	UPROPERTY(BlueprintAssignable, Category = Mover)
	FJoltMover_OnTeleportSucceeded OnTeleportSucceeded;

	// Broadcast when a teleport has failed
	UPROPERTY(BlueprintAssignable, Category = Mover)
	FJoltMover_OnTeleportFailed OnTeleportFailed;

	// Broadcast when a Transition has been triggered.
	UPROPERTY(BlueprintAssignable, Category = Mover)
	FJoltMover_OnTransitionTriggered OnMovementTransitionTriggered;

	// Broadcast after each finalized simulation frame, after the state is finalized. (Game thread only)
	UPROPERTY(BlueprintAssignable, Category = Mover)
	FJoltMover_OnPostFinalize OnPostFinalize;

	// Fired when a new event has been received from the simulation
	// This happens after the event had been processed at the mover component level, which may
	// have caused a dedicated delegate, e.g. OnMovementModeChanged, to fire prior to this broadcast.
	FJoltMover_OnPostSimEventReceived OnPostSimEventReceived;

	/**
	 * Broadcast after proposed movement has been generated. After movement modes and layered moves have generated movement and mixed together.
	 * This allows for final modifications to proposed movement before it's executed.
	 */
	FJoltMover_ProcessGeneratedMovement ProcessGeneratedMovement;
	
	uint8 bIsClientUsingSmoothing : 1 = 0;

	// Binds event for processing movement after it has been generated. Allows for final modifications to proposed movement before it's executed.
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API void BindProcessGeneratedMovement(FJoltMover_ProcessGeneratedMovement ProcessGeneratedMovementEvent);
	// Clears current bound event for processing movement after it has been generated.
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API void UnbindProcessGeneratedMovement();
	
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API virtual void SetLinearVelocity(const FVector Velocity);
	
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API virtual void SetAngularVelocity(const FVector Velocity);
	
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API virtual void SetTargetOrientation(const FRotator Rotation);
	
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API virtual void SetTargetPosition(const FVector Position);
	
	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Objects", DisplayName="Set Physics State", meta=(DevelopmentOnly))
	JOLTMOVER_API void RewindStateBackToPreviousFrame(const int32 FrameDelta);

	
	// Callbacks
	UFUNCTION()
	virtual void OnBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult) { }

	// --------------------------------------------------------------------------------
	// NP Driver
	// --------------------------------------------------------------------------------

	// Get latest local input prior to simulation step. Called by backend system on owner's instance (autonomous or authority).
	JOLTMOVER_API virtual void ProduceInput(const int32 DeltaTimeMS, FJoltMoverInputCmdContext* Cmd);

	// Restore a previous frame prior to resimulating. Called by backend system. NewBaseTimeStep represents the current time and frame we'll simulate next.
	JOLTMOVER_API virtual void RestoreFrame(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState, const FJoltMoverTimeStep& NewBaseTimeStep);

	// Take output for simulation. Called by backend system.
	JOLTMOVER_API virtual void FinalizeFrame(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState);

	// Take output for simulation when no simulation state changes have occurred. Called by backend system.
	JOLTMOVER_API virtual void FinalizeUnchangedFrame();

	// Take smoothed simulation state. Called by backend system, if supported.
	JOLTMOVER_API virtual void FinalizeSmoothingFrame(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState);

	// This is an opportunity to run code on the code on the simproxy in interpolated mode - currently used to help activate and deactivate modifiers on the simproxy in interpolated mode
	JOLTMOVER_API virtual void TickInterpolatedSimProxy(const FJoltMoverTimeStep& TimeStep, const FJoltMoverInputCmdContext& InputCmd, UJoltMoverComponent* MoverComp, const FJoltMoverSyncState& CachedSyncState, const FJoltMoverSyncState& SyncState, const FJoltMoverAuxStateContext& AuxState);
	
	// Seed initial values based on component's state. Called by backend system.
	JOLTMOVER_API virtual void InitializeSimulationState(FJoltMoverSyncState* OutSync, FJoltMoverAuxStateContext* OutAux);

	// Primary movement simulation update. Given an starting state and timestep, produce a new state. Called by backend system.
	JOLTMOVER_API virtual void SimulationTick(const FJoltMoverTimeStep& InTimeStep, const FJoltMoverTickStartData& SimInput, OUT FJoltMoverTickEndData& SimOutput);
	
	// Primary movement simulation update. Given an starting state and timestep, produce a new state. Called by backend system.
	JOLTMOVER_API virtual void PostPhysicsTick(const FJoltMoverTimeStep& TimeStep, OUT FJoltMoverTickEndData& SimOutput);

	// Specifies which supporting back end class should drive this Mover actor
	UPROPERTY(EditDefaultsOnly, Category = Mover, meta = (MustImplement = "/Script/JoltMover.MoverBackendLiaisonInterface"))
	TSubclassOf<UActorComponent> BackendClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = Mover, meta=(FullyExpand=true))
	TMap<FName, TObjectPtr<UJoltBaseMovementMode>> MovementModes;

	// Name of the first mode to start in when simulation begins. Must have a mapping in MovementModes. Only used during initialization.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Mover, meta=(GetOptions=GetStartingMovementModeNames))
	FName StartingMovementMode = NAME_None;

	// Transition checks that are always evaluated regardless of mode. Evaluated in order, stopping at the first successful transition check. Mode-owned transitions take precedence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category=Mover)
	TArray<TObjectPtr<UJoltBaseMovementModeTransition>> Transitions;

	/** List of types that should always be present in this actor's sync state */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Mover)
	TArray<FJoltMoverDataPersistence> PersistentSyncStateDataTypes;

	/** Optional object for producing input cmds. Typically set at BeginPlay time. If not specified, defaulted input will be used.
	*   Note that any other actor component implementing MoverInputProducerInterface on this component's owner will also be able
	*   to produce input commands if bGatherInputFromAllInputProducerComponents is true. @see bGatherInputFromAllInputProducerComponents
	*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Mover, meta = (ObjectMustImplement = "/Script/JoltMover.MoverInputProducerInterface"))
	TObjectPtr<UObject> InputProducer;

	/** If true, any actor component implementing MoverInputProducerInterface on this component's owner will be able to produce input commands */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Mover)
	bool bGatherInputFromAllInputProducerComponents = true;
	
	/** If true, any input commands will be ignored */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Mover)
	bool bIgnoreAnyInputProducer = false;

	/* All MoverInputProducerInterface objects producing input for this mover component. If bGatherInputFromAllInputProducerComponents
	*  is true, all components implementing MoverInputProducerInterface on this component's owner will be added to 
	*  this array at BeginPlay time, and IJoltMoverInputProducerInterface::ProduceInput will be called on each within UJoltMoverComponent::ProduceInput.
	*  The order shouldn't matter, as this is for input commands independent of each other, driving different movement modes.
	*  If order is important, set bGatherInputFromAllInputProducerComponents to false and implement a dedicated input component instead,
	*  gathering input from different sources in a custom order and set it as the InputProducer.
	*/
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> InputProducers;

	/** Optional object for mixing proposed moves.Typically set at BeginPlay time. If not specified, UDefaultMovementMixer will be used. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Mover)
	TObjectPtr<UJoltMovementMixer> MovementMixer;

	const TArray<TObjectPtr<UJoltLayeredMoveLogic>>* GetRegisteredMoves() const;
	
	/** Registers layered move logic */
	template <typename MoveT UE_REQUIRES(std::is_base_of_v<UJoltLayeredMoveLogic, MoveT>)>
	void RegisterMove(TSubclassOf<MoveT> MoveClass = MoveT::StaticClass())
	{
		K2_RegisterMove(MoveClass);
	}

	/** Registers layered move logic */
	UFUNCTION(BlueprintCallable, Category = Mover, DisplayName = "Register Move")
	void K2_RegisterMove(TSubclassOf<UJoltLayeredMoveLogic> MoveClass);

	/** Registers an array of layered move logic classes */
	UFUNCTION(BlueprintCallable, Category = Mover, DisplayName = "Register Moves")
	void K2_RegisterMoves(TArray<TSubclassOf<UJoltLayeredMoveLogic>> MoveClasses);
	
	/** Unregisters layered move logic */
	template <typename MoveT UE_REQUIRES(std::is_base_of_v<UJoltLayeredMoveLogic, MoveT>)>
	void UnregisterMove(TSubclassOf<MoveT> MoveClass = MoveT::StaticClass())
	{
		K2_UnregisterMove(MoveClass);
	}

	/** Unregisters layered move logic */
	UFUNCTION(BlueprintCallable, Category = Mover, DisplayName = "Unregister Move")
	void K2_UnregisterMove(TSubclassOf<UJoltLayeredMoveLogic> MoveClass);
	
	template <typename MoveT, typename ActivationParamsT UE_REQUIRES(std::is_base_of_v<UJoltLayeredMoveLogic, MoveT> && std::is_base_of_v<typename MoveT::MoveDataType::ActivationParamsType, ActivationParamsT>)>
	bool QueueLayeredMoveActivationWithContext(const ActivationParamsT& ActivationParams, TSubclassOf<MoveT> MoveClass = MoveT::StaticClass())
	{
		return MakeAndQueueLayeredMove(MoveClass, &ActivationParams);
	}
	
	/**
	 * Queues a layered move for activation.
	 * Takes a Activation Context which provides context to set Layered Move Data.
	 * Make sure Activation Context type matches layered Move Data
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = Mover, meta = (CustomStructureParam = "MoveAsRawData", AllowAbstract = "false"), DisplayName = "Queue Layered Move Activation With Context")
	bool K2_QueueLayeredMoveActivationWithContext(TSubclassOf<UJoltLayeredMoveLogic> MoveLogicClass, UPARAM(DisplayName="Layered Move Activation Context") const int32& MoveAsRawData);
	DECLARE_FUNCTION(execK2_QueueLayeredMoveActivationWithContext);

	/**
 	 * Queues a layered move for activation.
 	 * Takes NO Activation Context meaning the layered move will be activated using default Move Data.
 	 * Note: Changing Move Data is still possible in the layered move logic itself
 	 * See QueueLayeredMoveActivationWithContext for activating a layered move with context
 	 */
	UFUNCTION(BlueprintCallable, Category = Mover, meta = (AllowAbstract = "false"))
	bool QueueLayeredMoveActivation(TSubclassOf<UJoltLayeredMoveLogic> MoveLogicClass);
	
	/**
	 * Queue a layered move to start during the next simulation frame. This will clone whatever move you pass in, so you'll need to fully set it up before queuing.
	 * @param LayeredMove			The move to queue, which must be a LayeredMoveBase sub-type. 
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = Mover, meta = (CustomStructureParam = "MoveAsRawData", AllowAbstract = "false", DisplayName = "Queue Layered Move"))
	JOLTMOVER_API void K2_QueueLayeredMove(UPARAM(DisplayName="Layered Move") const int32& MoveAsRawData);
	DECLARE_FUNCTION(execK2_QueueLayeredMove);

	// Queue a layered move to start during the next simulation frame
	JOLTMOVER_API void QueueLayeredMove(TSharedPtr<FJoltLayeredMoveBase> Move);
	
	/**
 	 * Queue a Movement Modifier to start during the next simulation frame. This will clone whatever move you pass in, so you'll need to fully set it up before queuing.
 	 * @param MovementModifier The modifier to queue, which must be a LayeredMoveBase sub-type.
 	 * @return Returns a Modifier handle that can be used to query or cancel the movement modifier
 	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = Mover, meta = (CustomStructureParam = "MoveAsRawData", AllowAbstract = "false", DisplayName = "Queue Movement Modifier"))
	JOLTMOVER_API FJoltMovementModifierHandle K2_QueueMovementModifier(UPARAM(DisplayName="Movement Modifier") const int32& MoveAsRawData);
	DECLARE_FUNCTION(execK2_QueueMovementModifier);

	// Queue a Movement Modifier to start during the next simulation frame.
	JOLTMOVER_API FJoltMovementModifierHandle QueueMovementModifier(TSharedPtr<FJoltMovementModifierBase> Modifier);
	
	/**
	 * Cancel any active or queued Modifiers with the handle passed in.
	 */
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API void CancelModifierFromHandle(FJoltMovementModifierHandle ModifierHandle);

	/**
	 * Cancel any active or queued movement features (layered moves, modifiers, etc.) that have a matching gameplay tag. Does not affect the active movement mode.
	 */
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API void CancelFeaturesWithTag(FGameplayTag TagToCancel, bool bRequireExactMatch=false);

	/**
	 * Queue an Instant Movement Effect to start at the end of this frame or start of the next subtick - whichever happens first. This will clone whatever move you pass in, so you'll need to fully set it up before queuing.
	 * @param InstantMovementEffect			The effect to queue, which must be a FJoltInstantMovementEffect sub-type. 
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = Mover, meta = (CustomStructureParam = "EffectAsRawData", AllowAbstract = "false", DisplayName = "Queue Instant Movement Effect"))
	JOLTMOVER_API void K2_QueueInstantMovementEffect(UPARAM(DisplayName="Instant Movement Effect") const int32& EffectAsRawData);
	DECLARE_FUNCTION(execK2_QueueInstantMovementEffect);

	/**
	 * Schedule an Instant Movement Effect to be applied as early as possible while ensuring it gets executed on the same frame on all networked end points.
	 * This adds a delay to the application of the effect, tunable in the NetworkPhysicsSettingsComponent. @see UNetworkPhysicsSettingsComponent, @see FNetworkPhysicsSettings, @see EventSchedulingMinDelaySeconds
	 * This will clone whatever move you pass in, so you'll need to fully set it up before queuing.
	 * @param InstantMovementEffect			The effect to queue, which must be a FJoltInstantMovementEffect sub-type. 
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = Mover, meta = (CustomStructureParam = "EffectAsRawData", AllowAbstract = "false", DisplayName = "Schedule Instant Movement Effect"))
	JOLTMOVER_API void K2_ScheduleInstantMovementEffect(UPARAM(DisplayName="Instant Movement Effect") const int32& EffectAsRawData);
	DECLARE_FUNCTION(execK2_ScheduleInstantMovementEffect);

	/**
	 *  Queue a Instant Movement Effect to take place at the end of this frame or start of the next subtick - whichever happens first
	 *  @param InstantMovementEffect			The effect to queue, which must be a FJoltInstantMovementEffect sub - type.
	 */ 
	JOLTMOVER_API void QueueInstantMovementEffect(TSharedPtr<FJoltInstantMovementEffect> InstantMovementEffect);
	/** 
	 * Queue a scheduled Instant Movement Effect to take place after delay (tunable in the NetworkPhysicsSettingsComponent)
	 * ensuring it gets executed on the same frame on all networked end points. @see UNetworkPhysicsSettingsComponent, @see FNetworkPhysicsSettings, @see EventSchedulingMinDelaySeconds
	 * @param InstantMovementEffect			The effect to queue, which must be a FJoltInstantMovementEffect sub - type.
	 */
	JOLTMOVER_API void ScheduleInstantMovementEffect(TSharedPtr<FJoltInstantMovementEffect> InstantMovementEffect);

	// Get the queued instant movement effects. This is mostly for internal use, general users should abstain from calling it.
	JOLTMOVER_API const TArray<FJoltScheduledInstantMovementEffect>& GetQueuedInstantMovementEffects() const;
	// Clears the queued instant movement effects. This is mostly for internal use, general users should abstain from calling it.
	JOLTMOVER_API void ClearQueuedInstantMovementEffects();
	// Queue an instant movement effect in async mode. Do not use on the game thread.
	void QueueInstantMovementEffect_Internal(const FJoltMoverTimeStep& TimeStep, TSharedPtr<FJoltInstantMovementEffect> InstantMovementEffect);

protected:
	// Queue a scheduled instant movement effect. Thread safe, can be used outside the game thread.
	void QueueInstantMovementEffect(const FJoltScheduledInstantMovementEffect& ScheduledInstantMovementEffect);

public:	
	// Queue a movement mode change to occur during the next simulation frame. If bShouldReenter is true, then a mode change will occur even if already in that mode.
	UFUNCTION(BlueprintCallable, Category = Mover, DisplayName="Queue Next Movement Mode")
	JOLTMOVER_API void QueueNextMode(FName DesiredModeName, bool bShouldReenter=false);

	// Add a movement mode to available movement modes. Returns true if the movement mode was added successfully. Returns the mode that was made.
	UFUNCTION(BlueprintCallable, Category = Mover, meta=(DeterminesOutputType="JoltMovementMode"))
	JOLTMOVER_API UJoltBaseMovementMode* AddMovementModeFromClass(FName ModeName, UPARAM(meta = (AllowAbstract = "false"))TSubclassOf<UJoltBaseMovementMode> MovementMode);

	// Add a movement mode to available movement modes. Returns true if the movement mode was added successfully
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API bool AddMovementModeFromObject(FName ModeName, UJoltBaseMovementMode* MovementMode);
	
	// Removes a movement mode from available movement modes. Returns number of modes removed from the available movement modes.
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API bool RemoveMovementMode(FName ModeName);
	
public:
	// Set gravity override, as a directional acceleration in worldspace.  Gravity on Earth would be {x=0,y=0,z=-980}
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API void SetGravityOverride(bool bOverrideGravity, FVector GravityAcceleration=FVector::ZeroVector);
	
	// Get the current acceleration due to gravity (cm/s^2) in worldspace
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = Mover)
	JOLTMOVER_API FVector GetGravityAcceleration() const;

	/** Returns a quaternion transforming from world to gravity space. */
	FQuat GetWorldToGravityTransform() const { return WorldToGravityTransform; }

	/** Returns a quaternion transforming from gravity to world space. */
	FQuat GetGravityToWorldTransform() const { return GravityToWorldTransform; }

	// Set UpDirection override. This is a fixed direction that overrides the gravity-derived up direction.
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API void SetUpDirectionOverride(bool bOverrideUpDirection, FVector UpDirection=FVector::UpVector);

	// Get the normalized direction considered "up" in worldspace. Typically aligned with gravity, and typically determines the plane an actor tries to move along.
	UFUNCTION(BlueprintPure = false, Category = Mover)
	JOLTMOVER_API FVector GetUpDirection() const;

	// Access the planar constraint that may be limiting movement direction
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = Mover)
	JOLTMOVER_API const FJoltPlanarConstraint& GetPlanarConstraint() const;

	// Sets planar constraint that can limit movement direction
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API void SetPlanarConstraint(const FJoltPlanarConstraint& InConstraint);

	// Sets BaseVisualComponentTransform used for cases where we want to move the visual component away from the root component. See @BaseVisualComponentTransform
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API void SetBaseVisualComponentTransform (const FTransform& ComponentTransform);

	// Gets BaseVisualComponentTransform used for cases where we want to move the visual component away from the root component. See @BaseVisualComponentTransform
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API FTransform GetBaseVisualComponentTransform() const;

	/** Sets whether this mover component can use grouped movement updates, which improve performance but can cause attachments to update later than expected */
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API void SetUseDeferredGroupMovement(bool bEnable);

	/** Returns true if this component is actually using grouped movement updates, which checks the flag and any global settings */
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API bool IsUsingDeferredGroupMovement() const;
	
public:

	/**
	 *  Converts a local root motion transform to worldspace. 
	 * @param AlternateActorToWorld   allows specification of a different actor root transform, for cases when root motion isn't directly being applied to this actor (async simulations)
	 * @param OptionalWarpingContext   allows specification of a warping context, for use with root motion that is asynchronous from the actor (async simulations)
	 */
	JOLTMOVER_API virtual FTransform ConvertLocalRootMotionToWorld(const FTransform& LocalRootMotionTransform, float DeltaSeconds, const FTransform* AlternateActorToWorld=nullptr, const FMotionWarpingUpdateContext* OptionalWarpingContext=nullptr) const;

	/** delegates used when converting local root motion to worldspace, allowing external systems to influence it (such as motion warping) */
	FOnWarpLocalspaceRootMotionWithContext ProcessLocalRootMotionDelegate;
	FOnWarpWorldspaceRootMotionWithContext ProcessWorldRootMotionDelegate;

public:	// Queries

	// Get the transform of the root component that our Mover simulation is moving
	JOLTMOVER_API FTransform GetUpdatedComponentTransform() const;

	// Sets which component we're using as the root of our movement
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API void SetUpdatedComponent(USceneComponent* NewUpdatedComponent);
	
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API void SetJoltPhysicsComponent(UPrimitiveComponent* NewPhysicsComponent);

	// Access the root component of the actor that our Mover simulation is moving
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API USceneComponent* GetUpdatedComponent() const;

	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API UPrimitiveComponent* GetUpdatedPrimitive() const;

	// Typed accessor to root moving component
	template<class T>
	T* GetUpdatedComponent() const
	{
		static_assert(TPointerIsConvertibleFromTo<T, const USceneComponent>::Value, "'T' template parameter to GetUpdatedComponent must be derived from USceneComponent");
		return Cast<T>(GetUpdatedComponent());
	}
	
	// Access the root component of the actor that our Mover simulation is moving
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API UPrimitiveComponent* GetJoltPhysicsBodyComponent() const;

	// Access the primary visual component of the actor
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API USceneComponent* GetPrimaryVisualComponent() const;

	// Typed accessor to primary visual component
	template<class T>
	T* GetPrimaryVisualComponent() const
	{
		return Cast<T>(GetPrimaryVisualComponent());
	}

	// Sets this Mover actor's primary visual component. Must be a descendant of the updated component that acts as our movement root. 
	UFUNCTION(BlueprintCallable, Category=Mover)
	JOLTMOVER_API void SetPrimaryVisualComponent(USceneComponent* SceneComponent);

	// Get the current velocity (units per second, worldspace)
	UFUNCTION(BlueprintPure, Category = Mover)
	JOLTMOVER_API FVector GetVelocity() const;

	// Get the intended movement direction in worldspace with magnitude (range 0-1)
	UFUNCTION(BlueprintPure, Category = Mover)
	JOLTMOVER_API FVector GetMovementIntent() const;

	// Get the orientation that the actor is moving towards
	UFUNCTION(BlueprintPure, Category = Mover)
	JOLTMOVER_API FRotator GetTargetOrientation() const;

	/** Get a sampling of where the actor is projected to be in the future, based on a current state. Note that this is projecting ideal movement without doing full simulation and collision. */
	UE_DEPRECATED(5.5, "Use GetPredictedTrajectory instead.")
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = Mover)
	JOLTMOVER_API TArray<FJoltTrajectorySampleInfo> GetFutureTrajectory(float FutureSeconds, float SamplesPerSecond);

	/** Get a sampling of where the actor is projected to be in the future, based on a current state. Note that this is projecting ideal movement without doing full simulation and collision.
	 * The first sample info of the returned array corresponds to the current state of the mover. */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = Mover)
	JOLTMOVER_API TArray<FJoltTrajectorySampleInfo> GetPredictedTrajectory(FJoltMoverPredictTrajectoryParams PredictionParams);	

	// Get the current movement mode name
	UFUNCTION(BlueprintPure, Category = Mover)
	JOLTMOVER_API FName GetMovementModeName() const;
	
	// Get the current movement mode 
	UFUNCTION(BlueprintPure, Category = Mover)
	JOLTMOVER_API const UJoltBaseMovementMode* GetMovementMode() const;

	// Get the current movement base. Null if there isn't one.
	UFUNCTION(BlueprintPure, Category = Mover)
	JOLTMOVER_API UPrimitiveComponent* GetMovementBase() const;

	// Get the current movement base bone, NAME_None if there isn't one.
	UFUNCTION(BlueprintPure, Category = Mover)
	JOLTMOVER_API FName GetMovementBaseBoneName() const;

	// Signals whether we have a sync state saved yet. If not, most queries will not be meaningful.
	UE_DEPRECATED(5.6, "HasValidCachedState has been deprecated, and is not needed since we no longer wait until movement simulation begins before providing a valid sync state.")
	UFUNCTION(BlueprintPure, Category = Mover, meta=(DeprecatedFunction, DeprecationMessage="HasValidCachedState has been deprecated, and is not needed since we no longer wait until movement simulation begins before providing a valid sync state."))
	JOLTMOVER_API bool HasValidCachedState() const;

	// Access the most recent captured sync state.
	UFUNCTION(BlueprintPure, Category = Mover)
	JOLTMOVER_API const FJoltMoverSyncState& GetSyncState() const;

	// Signals whether we have input data saved yet. If not, input queries will not be meaningful.
	UE_DEPRECATED(5.6, "HasValidCachedInputCmd has been deprecated, and is not needed since we no longer wait until movement simulation begins before providing a valid input cmd.")
	UFUNCTION(BlueprintPure, Category = Mover, meta = (DeprecatedFunction, DeprecationMessage = "HasValidCachedInputCmd has been deprecated, and is not needed since we no longer wait until movement simulation begins before providing a valid input cmd."))
	JOLTMOVER_API bool HasValidCachedInputCmd() const;

	// Access the most recently-used inputs.
	UFUNCTION(BlueprintPure, Category = Mover)
	JOLTMOVER_API const FJoltMoverInputCmdContext& GetLastInputCmd() const;

	// Get the most recent TimeStep
	UFUNCTION(BlueprintPure, Category = Mover)
	JOLTMOVER_API const FJoltMoverTimeStep& GetLastTimeStep() const;

	// Access the most recent floor check hit result.
	UFUNCTION(BlueprintPure, Category = Mover)
	JOLTMOVER_API virtual bool TryGetFloorCheckHitResult(FHitResult& OutHitResult) const;

	// Access the read-only version of the Mover's Blackboard
	UFUNCTION(BlueprintPure, Category=Mover)
	JOLTMOVER_API const UJoltMoverBlackboard* GetSimBlackboard() const;

	JOLTMOVER_API UJoltMoverBlackboard* GetSimBlackboard_Mutable() const;


	UJoltRollbackBlackboard* GetRollbackBlackboard() const { return RollbackBlackboard.Get(); }
	UJoltRollbackBlackboard_InternalWrapper* GetRollbackBlackboard_Internal() const { return RollbackBlackboard_InternalWrapper.Get(); }

	/** Find settings object by type. Returns null if there is none of that type */
	const IJoltMovementSettingsInterface* FindSharedSettings(const UClass* ByType) const { return FindSharedSettings_Mutable(ByType); }
	template<typename SettingsT = IJoltMovementSettingsInterface UE_REQUIRES(std::is_base_of_v<IJoltMovementSettingsInterface, SettingsT>)>
	const SettingsT* FindSharedSettings() const { return Cast<const SettingsT>(FindSharedSettings(SettingsT::StaticClass())); }

	/** Find mutable settings object by type. Returns null if there is none of that type */
	JOLTMOVER_API IJoltMovementSettingsInterface* FindSharedSettings_Mutable(const UClass* ByType) const;
	template<typename SettingsT = IJoltMovementSettingsInterface UE_REQUIRES(std::is_base_of_v<IJoltMovementSettingsInterface, SettingsT>)>
	SettingsT* FindSharedSettings_Mutable() const { return Cast<SettingsT>(FindSharedSettings_Mutable(SettingsT::StaticClass())); }

	/** Find mutable settings object by type. Returns null if there is none of that type */
	UFUNCTION(BlueprintPure, Category = Mover,  meta=(DeterminesOutputType="SharedSetting", DisplayName="Find Shared Settings Mutable"))
	JOLTMOVER_API UObject* FindSharedSettings_Mutable_BP(UPARAM(meta = (MustImplement = "MovementSettingsInterface")) TSubclassOf<UObject> SharedSetting) const;

	/** Find settings object by type. Returns null if there is none of that type */
	UFUNCTION(BlueprintPure, Category = Mover,  meta=(DeterminesOutputType="SharedSetting", DisplayName="Find Shared Settings"))
	JOLTMOVER_API const UObject* FindSharedSettings_BP(UPARAM(meta = (MustImplement = "MovementSettingsInterface")) TSubclassOf<UObject> SharedSetting) const;

	/** Gets the currently active movement mode, provided it is of the given type. Returns nullptr if there is no active mode yet, or if it's of a different type. */
	template<typename ModeT = UJoltBaseMovementMode UE_REQUIRES(std::is_base_of_v<UJoltBaseMovementMode, ModeT>)>
	const ModeT* GetActiveMode(bool bRequireExactClass = false) const { return Cast<ModeT>(GetActiveModeInternal(ModeT::StaticClass(), bRequireExactClass)); }

	/** Gets the currently active movement mode, provided it is of the given type. Returns nullptr if there is no active mode yet, or if it's of a different type. */
	template<typename ModeT = UJoltBaseMovementMode UE_REQUIRES(std::is_base_of_v<UJoltBaseMovementMode, ModeT>)>
	ModeT* GetActiveMode_Mutable(bool bRequireExactClass = false) const { return Cast<ModeT>(GetActiveModeInternal(ModeT::StaticClass(), bRequireExactClass)); }

	/** Find the first movement mode on this component with the given type, optionally of the given type exactly. Returns null if there is none of that type */
	template<typename ModeT = UJoltBaseMovementMode UE_REQUIRES(std::is_base_of_v<UJoltBaseMovementMode, ModeT>)>
	ModeT* FindMode_Mutable(bool bRequireExactClass = false) const { return Cast<ModeT>(FindMode_Mutable(ModeT::StaticClass(), bRequireExactClass)); }
	JOLTMOVER_API UJoltBaseMovementMode* FindMode_Mutable(TSubclassOf<UJoltBaseMovementMode> ModeType, bool bRequireExactClass = false) const;

	/** Find the movement mode on this component the given name and type, optionally of the given type exactly. Returns null if there is no mode by that name, or if it's of a different type. */
	template<typename ModeT = UJoltBaseMovementMode UE_REQUIRES(std::is_base_of_v<UJoltBaseMovementMode, ModeT>)>
	ModeT* FindMode_Mutable(FName MovementModeName, bool bRequireExactClass = false) const { return Cast<ModeT>(FindMode_Mutable(ModeT::StaticClass(), MovementModeName, bRequireExactClass)); }
	JOLTMOVER_API UJoltBaseMovementMode* FindMode_Mutable(TSubclassOf<UJoltBaseMovementMode> ModeType, FName ModeName, bool bRequireExactClass = false) const;
	
	UFUNCTION(BlueprintPure, Category = Mover,  meta=(DeterminesOutputType="JoltMovementMode"))
	JOLTMOVER_API UJoltBaseMovementMode* FindMovementMode(TSubclassOf<UJoltBaseMovementMode> MovementMode) const;

	UFUNCTION(BlueprintPure, Category = Mover)
	JOLTMOVER_API UJoltBaseMovementMode* FindMovementModeByName(FName MovementModeName) const;
	
	/**
	 * Retrieves an active layered move, by writing to a target instance if it is the matching type. Note: Writing to the struct returned will not modify the active struct.
	 * @param DidSucceed			Flag indicating whether data was actually written to target struct instance
	 * @param TargetAsRawBytes		The data struct instance to write to, which must be a FJoltLayeredMoveBase sub-type
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = Mover, meta = (CustomStructureParam = "TargetAsRawBytes", AllowAbstract = "false", DisplayName = "Find Active Layered Move"))
	JOLTMOVER_API void K2_FindActiveLayeredMove(bool& DidSucceed, UPARAM(DisplayName = "Out Layered Move") int32& TargetAsRawBytes) const;
	DECLARE_FUNCTION(execK2_FindActiveLayeredMove);

	// Find an active layered move by type. Returns null if one wasn't found 
	JOLTMOVER_API const FJoltLayeredMoveBase* FindActiveLayeredMoveByType(const UScriptStruct* DataStructType) const;

	/** Find a layered move of a specific type in this components active layered moves. If not found, null will be returned. */
	template <typename MoveT = FJoltLayeredMoveBase UE_REQUIRES(std::is_base_of_v<FJoltLayeredMoveBase, MoveT>)>
	const MoveT* FindActiveLayeredMoveByType() const { return static_cast<const MoveT*>(FindActiveLayeredMoveByType(MoveT::StaticStruct())); }
	
	/**
	 * Retrieves Movement modifier by writing to a target instance if it is the matching type. Note: Writing to the struct returned will not modify the active struct.
	 * @param ModifierHandle		Handle of the modifier we're trying to cancel
	 * @param bFoundModifier		Flag indicating whether modifier was found and data was actually written to target struct instance
	 * @param TargetAsRawBytes		The data struct instance to write to, which must be a FJoltMovementModifierBase sub-type
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = Mover, meta = (CustomStructureParam = "TargetAsRawBytes", AllowAbstract = "false", DisplayName = "Find Movement Modifier"))
	JOLTMOVER_API void K2_FindMovementModifier(FJoltMovementModifierHandle ModifierHandle, bool& bFoundModifier, UPARAM(DisplayName = "Out Movement Modifier") int32& TargetAsRawBytes) const;
	DECLARE_FUNCTION(execK2_FindMovementModifier);

	// Checks if the modifier handle passed in is active or queued on this mover component
	UFUNCTION(BlueprintPure, Category = Mover)
	JOLTMOVER_API bool IsModifierActiveOrQueued(const FJoltMovementModifierHandle& ModifierHandle) const;
	
	// Find movement modifier by it's handle. Returns nullptr if the modifier couldn't be found
	JOLTMOVER_API const FJoltMovementModifierBase* FindMovementModifier(const FJoltMovementModifierHandle& ModifierHandle) const;

	// Find movement modifier by type (returns the first modifier it finds). Returns nullptr if the modifier couldn't be found
	JOLTMOVER_API const FJoltMovementModifierBase* FindMovementModifierByType(const UScriptStruct* DataStructType) const;
	
	/** Find a movement modifier of a specific type in this components movement modifiers. If not found, null will be returned. */
	template <typename ModifierT = FJoltMovementModifierBase UE_REQUIRES(std::is_base_of_v<FJoltMovementModifierBase, ModifierT>)>
	const ModifierT* FindMovementModifierByType() const { return static_cast<const ModifierT*>(FindMovementModifierByType(ModifierT::StaticStruct())); }
	
	/**
 	 * Check Mover systems for a gameplay tag.
 	 *
 	 * @param TagToFind			Tag to check on the Mover systems
 	 * @param bExactMatch		If true, the tag has to be exactly present, if false then TagToFind will include it's parent tags while matching
 	 * 
 	 * @return True if the TagToFind was found
 	 */
	UFUNCTION(BlueprintPure, Category = Mover, meta = (Keywords = "HasTag"))
	JOLTMOVER_API bool HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const;

	/**
	 * Check Mover systems for a gameplay tag. Use the given state, as well as any loose tags on the MoverComponent.
	 *
	 * @param TagToFind			Tag to check on the MoverComponent or state
	 * @param bExactMatch		If true, the tag has to be exactly present, if false then TagToFind will include it's parent tags while matching
	 *
	 * @return True if the TagToFind was found
	 */
	UFUNCTION(BlueprintPure, Category = Mover, meta = (Keywords = "HasTag"))
	JOLTMOVER_API bool HasGameplayTagInState(const FJoltMoverSyncState& SyncState, FGameplayTag TagToFind, bool bExactMatch) const;

	/**
  	 * Adds a gameplay tag to this Mover Component.
  	 * Note: Duplicate tags will not be added
  	 * @param TagToAdd			Tag to add to the Mover Component
  	 */
	UFUNCTION(BlueprintCallable, Category = Mover, meta = (Keywords = "Add Tag"))
	JOLTMOVER_API void AddGameplayTag(FGameplayTag TagToAdd);

	/**
   	 * Adds a series of gameplay tags to this Mover Component
   	 * Note: Duplicate tags will not be added
   	 * @param TagsToAdd			Tags to add/append to the Mover Component
   	 */
	UFUNCTION(BlueprintCallable, Category = Mover, meta = (Keywords = "Add Tag"))
	JOLTMOVER_API void AddGameplayTags(const FGameplayTagContainer& TagsToAdd);
	
	/**
   	 * Removes a gameplay tag from this Mover Component
   	 * @param TagToRemove			Tag to remove from the Mover Component
   	 */
	UFUNCTION(BlueprintCallable, Category = Mover, meta = (Keywords = "Remove Tag"))
	JOLTMOVER_API void RemoveGameplayTag(FGameplayTag TagToRemove);

	/**
	 * Removes gameplay tags from this Mover Component
	 * @param TagsToRemove			Tags to remove from the Mover Component
	 */
	UFUNCTION(BlueprintCallable, Category = Mover, meta = (Keywords = "Remove Tag"))
	JOLTMOVER_API void RemoveGameplayTags(const FGameplayTagContainer& TagsToRemove);
	
protected:

	// Called before each simulation tick. Broadcasts OnPreSimulationTick delegate.
	void PreSimulationTick(const FJoltMoverTimeStep& TimeStep, const FJoltMoverInputCmdContext& InputCmd);
	
	/** Makes this component and owner actor reflect the state of a particular frame snapshot. This occurs after simulation ticking, as well as during a rollback before we resimulate forward.
	  @param bRebaseBasedState	If true and the state was using based movement, it will use the current game world base pos/rot instead of the captured one. This is necessary during rollbacks.
	*/
	JOLTMOVER_API void SetFrameStateFromContext(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState, bool bRebaseBasedState);
	JOLTMOVER_API void SetFrameStateFromContextFromNestedChild(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState, bool bRebaseBasedState);

	/** Update cached frame state if it has changed */
	JOLTMOVER_API void UpdateCachedFrameState(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState);

public:
	JOLTMOVER_API virtual void CreateDefaultInputAndState(FJoltMoverInputCmdContext& OutInputCmd, FJoltMoverSyncState& OutSyncState, FJoltMoverAuxStateContext& OutAuxState) const;

	/** Handle a blocking impact.*/
	UFUNCTION(BlueprintCallable, Category = Mover)
	JOLTMOVER_API void HandleImpact(FJoltMoverOnImpactParams& ImpactParams);

protected:
	JOLTMOVER_API void FindDefaultComponents();
	JOLTMOVER_API void FindDefaultUpdatedComponent();
	JOLTMOVER_API void UpdateTickRegistration();

	JOLTMOVER_API virtual void DoQueueNextMode(FName DesiredModeName, bool bShouldReenter=false);

	// Broadcast during the simulation tick after inputs have been processed, but before the actual move is performed.
	// Note - When async simulating, the delegate would be called on the async thread, and might be broadcast multiple times.
	UPROPERTY(BlueprintAssignable, Category = Mover)
	FJoltMover_OnPreMovement OnPreMovement;

	/** Called when a rollback occurs, before the simulation state has been restored. NewBaseTimeStep represents the current time and frame we're about to simulate. */
	JOLTMOVER_API void OnSimulationPreRollback(const FJoltMoverSyncState* InvalidSyncState, const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* InvalidAuxState, const FJoltMoverAuxStateContext* AuxState, const FJoltMoverTimeStep& NewBaseTimeStep);
	
	/** Called when a rollback occurs, after the simulation state has been restored. NewBaseTimeStep represents the current time and frame we're about to simulate. */
	JOLTMOVER_API void OnSimulationRollback(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState, const FJoltMoverTimeStep& NewBaseTimeStep);

	JOLTMOVER_API void ProcessFirstSimTickAfterRollback(const FJoltMoverTimeStep& TimeStep);

	#if WITH_EDITOR
	JOLTMOVER_API virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	JOLTMOVER_API virtual void PostCDOCompiled(const FPostCDOCompiledContext& Context) override;
	JOLTMOVER_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	JOLTMOVER_API virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;
	JOLTMOVER_API bool ValidateSetup(class FDataValidationContext& ValidationErrors) const;
	JOLTMOVER_API virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;

	UFUNCTION()
	JOLTMOVER_API TArray<FString> GetStartingMovementModeNames();
	#endif // WITH_EDITOR

	UFUNCTION()
	JOLTMOVER_API virtual void PhysicsVolumeChanged(class APhysicsVolume* NewVolume);

	JOLTMOVER_API virtual void OnHandleImpact(const FJoltMoverOnImpactParams& ImpactParams);

	/** internal function to perform post-sim scheduling to optionally support simple based movement */
	JOLTMOVER_API void UpdateBasedMovementScheduling(const FJoltMoverTickEndData& SimOutput);

	JOLTMOVER_API UJoltBaseMovementMode* GetActiveModeInternal(TSubclassOf<UJoltBaseMovementMode> ModeType, bool bRequireExactClass = false) const;

	TObjectPtr<UPrimitiveComponent> MovementBaseDependency;	// used internally for based movement scheduling management
	
	/** internal function to ensure SharedSettings array matches what's needed by the list of Movement Modes */
	JOLTMOVER_API void RefreshSharedSettings();

	/** This is the component that's actually being moved. Typically it is the Actor's root component and often a collidable primitive. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> UpdatedComponent = nullptr;

	/** UpdatedComponent, cast as a UPrimitiveComponent. May be invalid if UpdatedComponent was null or not a UPrimitiveComponent. */
	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> UpdatedCompAsPrimitive = nullptr;
	
	/** JoltPhysicsComponent, must be a UPrimitiveComponent. Should not be invalid. 
	 * It is fetched automatically on BeginPlay where the first jolt primitive type in the hierarchy is used*/
	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> JoltPhysicsComponent = nullptr;

	/** The main visual component associated with this Mover actor, typically a mesh and typically parented to the UpdatedComponent. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> PrimaryVisualComponent;


	/** Cached original offset from the visual component, used for cases where we want to move the visual component away from the root component (for smoothing, corrections, etc.) */
	FTransform BaseVisualComponentTransform = FTransform::Identity;

	// TODO: Look at possibility of replacing this with a FGameplayTagCountContainer that could possibly represent both internal and external tags
	/** A list of gameplay tags associated with this Mover Component added from sources outside of Mover */
	FGameplayTagContainer ExternalGameplayTags;
	
	FJoltMoverInputCmdContext CachedLastProducedInputCmd;
	
	FJoltMoverInputCmdContext CachedLastUsedInputCmd;
	
	FJoltMoverDoubleBuffer<FJoltMoverSyncState> MoverSyncStateDoubleBuffer;
	
	const FJoltUpdatedMotionState* LastMoverDefaultSyncState = nullptr;

	FJoltMoverTimeStep CachedLastSimTickTimeStep;	// Saved timestep info from our last simulation tick, used during rollback handling. This will rewind during corrections.
	FJoltMoverTimeStep CachedNewestSimTickTimeStep;	// Saved timestep info from the newest (farthest-advanced) simulation tick. This will not rewind during corrections.

	UPROPERTY(Transient)
	TScriptInterface<IJoltMoverBackendLiaisonInterface> BackendLiaisonComp;

	/** Tick function that may be called anytime after this actor's movement step, useful as a way to support based movement on objects that are not */
	FJoltMoverDynamicBasedMovementTickFunction BasedMovementTickFunction;

	UPROPERTY(Transient)
	TObjectPtr<UJoltMovementModeStateMachine> ModeFSM;	// JAH TODO: Also consider allowing a type property on the component to allow an alternative machine implementation to be allocated/used

	/** Used to store cached data & computations between decoupled systems, that can be referenced by name */
	UPROPERTY(Transient)
	TObjectPtr<UJoltMoverBlackboard> SimBlackboard = nullptr;

	/** Used to store cached data & computations between decoupled systems, that can be referenced by name. Rollback-aware. */
	UPROPERTY(Transient)
	TObjectPtr<UJoltRollbackBlackboard> RollbackBlackboard = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UJoltRollbackBlackboard_InternalWrapper> RollbackBlackboard_InternalWrapper = nullptr;	// This is a thin layer for use only by in-simulation users

	/**
	 * Layered moves registered on this component that can be activated regardless of the current mode
	 * Changes to this array or its contents occur ONLY during PreSimulationTick to ensure threadsafe access during async simulations.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UJoltLayeredMoveLogic>> RegisteredMoves;

	UPROPERTY(Transient)
	TArray<TSubclassOf<UJoltLayeredMoveLogic>> MovesPendingRegistration;

	UPROPERTY(Transient)
	TArray<TSubclassOf<UJoltLayeredMoveLogic>> MovesPendingUnregistration;
	
	/** Helper function for making Queueing and making Layered Moves */
	bool MakeAndQueueLayeredMove(const TSubclassOf<UJoltLayeredMoveLogic>& MoveLogicClass, const FJoltLayeredMoveActivationParams* ActivationParams);
	

	
private:
	/** Collection of settings objects that are shared between movement modes. This list is automatically managed based on the @MovementModes contents. */
	UPROPERTY(EditDefaultsOnly, EditFixedSize, Instanced, Category = Mover, meta = (NoResetToDefault, ObjectMustImplement = "/Script/JoltMover.MovementSettingsInterface"))
	TArray<TObjectPtr<UObject>> SharedSettings;
	
	/** cm/s^2, only meaningful if @bHasGravityOverride is enabled.Set @SetGravityOverride */
	UPROPERTY(EditDefaultsOnly, Category="Jolt Mover|Gravity", meta=(ForceUnits = "cm/s^2"))
	FVector GravityAccelOverride;

	/** Settings that can lock movement to a particular plane */
	UPROPERTY(EditDefaultsOnly, Category = "Jolt Mover|Constraints")
	FJoltPlanarConstraint PlanarConstraint;

	/** Effects queued to be applied to the simulation at a given frame. If the frame happens to be in the past, the effect will be applied at the earliest occasion */
	TArray<FJoltScheduledInstantMovementEffect> QueuedInstantMovementEffects;

public:

	// If enabled, the movement of the primary visual component will be smoothed via an offset from the root moving component. This is useful in fixed-tick simulations with variable rendering rates.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JoltMover")
	EJoltMoverSmoothingMode SmoothingMode = EJoltMoverSmoothingMode::VisualComponentOffset;

	// Whether to warn when we detect that an external system has moved our object, outside of movement simulation control
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JoltMover", AdvancedDisplay)
	uint8 bWarnOnExternalMovement : 1 = 1;

	// If enabled, we'll accept any movements from an external system in the next simulation state update
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JoltMover", AdvancedDisplay)
	uint8 bAcceptExternalMovement : 1 = 0;
	
	// Whether to warn when we detect that an external system has moved our object, outside of movement simulation control
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BulletMover", AdvancedDisplay)
	uint8 bIgnoreVelocityGeneratedByMovementMode : 1 = 0;

	// If enabled, we'll send inputs along with to sim proxy via the sync state, and they'll be available via GetLastInputCmd. This may be useful for cases where input is used to hint at object state, such as an anim graph. This option is intended to be temporary until all networking backends allow this.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JoltMover", AdvancedDisplay, Experimental)
	uint8 bSyncInputsForSimProxy : 1 = 0;

	JOLTMOVER_API void SetSimulationOutput(const FJoltMoverTimeStep& TimeStep, const UE::JoltMover::FJoltSimulationOutputData& OutputData);

	// Dispatch a simulation event. It will be processed immediately.
	JOLTMOVER_API void DispatchSimulationEvent(const FJoltMoverSimulationEventData& EventData);

protected:
	JOLTMOVER_API virtual void ProcessSimulationEvent(const FJoltMoverSimulationEventData& EventData);
	JOLTMOVER_API virtual void SetAdditionalSimulationOutput(const FJoltMoverDataCollection& Data);
	JOLTMOVER_API virtual void CheckForExternalMovement(const FJoltMoverTickStartData& SimStartingData);

private:
	// Whether to override the up direction with a fixed value instead of using gravity to deduce it
	UPROPERTY(EditDefaultsOnly, Category="Jolt Mover|UpDirection")
	bool bHasUpDirectionOverride = false;

	// A fixed up direction to use if bHasUpDirectionOverride is true
	UPROPERTY(EditDefaultsOnly, Category="Jolt Mover|UpDirection", meta = (EditCondition = "bHasUpDirectionOverride"))
	FVector UpDirectionOverride = FVector::UpVector;

	/** Whether or not gravity is overridden on this actor. Otherwise, fall back on world settings. See @SetGravityOverride */
	UPROPERTY(EditDefaultsOnly, Category="Jolt Mover|Gravity")
	bool bHasGravityOverride = false;
	

	/**
     * If true, then the transform updates applied in UJoltMoverComponent::SetFrameStateFromContext will use a "deferred group move"
     * to improve performance.
     *
     * It is not recommended that you enable this when you need exact, high fidelity characters such as your player character.
     * This is mainly a benefit for scenarios with large amounts of NPCs or lower fidelity characters where it is acceptable
     * to not have immediately applied transforms.
     *
     * This only does something if the "s.GroupedComponentMovement.Enable" CVar is set to true.
     */
    UPROPERTY(EditDefaultsOnly, Category="JoltMover", meta = (EditCondition = "Engine.SceneComponent.IsGroupedComponentMovementEnabled"))
    bool bUseDeferredGroupMovement = false;

	/** Transient flag indicating whether we are executing OnRegister(). */
	bool bInOnRegister = false;

	/** Transient flag indicating whether we are executing InitializeComponent(). */
	bool bInInitializeComponent = false;

	// Transient flag indicating we've had a rollback and haven't started simulating forward again yet
	bool bHasRolledBack = false;

	/**
	 * A cached quaternion representing the rotation from world space to gravity relative space defined by GravityAccelOverride.
	 */
	UPROPERTY(Category="Jolt Mover|Gravity", VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
	FQuat WorldToGravityTransform;
	
	/**
	 * A cached quaternion representing the inverse rotation from world space to gravity relative space defined by GravityAccelOverride.
	 */
	UPROPERTY(Category="Jolt Mover|Gravity", VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
	FQuat GravityToWorldTransform;
	
protected:

	/** If enabled, this actor will be moved to follow a base actor that it's standing on. Typically disabled for physics-based movement, which handles based movement internally. */
	UPROPERTY(EditDefaultsOnly, Category = "JoltMover")
	bool bSupportsKinematicBasedMovement = false;

	// Delay added to scheduled instant movement effects
	// This value is cached from the settings found in the network settings component
	float EventSchedulingMinDelaySeconds = 0.3f;

	FJoltMoverAuxStateContext CachedLastAuxState;

	friend class UJoltBaseMovementMode;
	friend class UJoltMoverDebugComponent;
	friend class UJoltBasedMovementUtils;
	
	
	
	
	
#pragma region JOLT PHYSICS
protected:
	
	

	
	
	virtual void InitializeWithJolt() {};
	
	virtual void JoltPreSimulationTick(const FJoltMoverTimeStep& InTimeStep, const FJoltMoverTickStartData& SimInput, FJoltMoverTickEndData& SimOutput) {};
	virtual void FinalizeStateFromJoltSimulation(FJoltMoverTickEndData& SimOutput) {};
	
public:
	virtual void SendFinalVelocityToJolt(const FJoltMoverTimeStep& InTimeStep, const FVector& LinearVelocity, const FVector& AngularVelocity) {}

#pragma endregion
};
