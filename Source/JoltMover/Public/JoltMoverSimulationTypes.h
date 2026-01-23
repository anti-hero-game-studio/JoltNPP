// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <functional>

#include "Misc/StringBuilder.h"
#include "JoltNetworkPredictionReplicationProxy.h"
#include "Engine/NetSerialization.h"
#include "JoltMoverTypes.h"
#include "MoveLibrary/JoltMovementRecord.h"
#include "JoltLayeredMove.h"
#include "JoltLayeredMoveGroup.h"
#include "JoltMovementModifier.h"
#include "JoltMoverDataModelTypes.h"
#include "JoltInstantMovementEffect.h"
#include "UObject/Interface.h"

#include "JoltMoverSimulationTypes.generated.h"

// Names for our default modes
namespace DefaultModeNames
{
	const FName Walking = TEXT("Walking");
	const FName Falling = TEXT("Falling");
	const FName Flying  = TEXT("Flying");
	const FName Swimming  = TEXT("Swimming");
}

// Commonly-used blackboard object keys
namespace CommonBlackboard
{
	const FName LastFloorResult = TEXT("LastFloor");
	const FName LastWaterResult = TEXT("LastWater");
	const FName LastFoundDynamicMovementBase = TEXT("LastFoundDynamicMovementBase");
	const FName LastAppliedDynamicMovementBase = TEXT("LastAppliedDynamicMovementBase");
	const FName TimeSinceSupported = TEXT("TimeSinceSupported");

	const FName LastModeChangeRecord = TEXT("LastModeChangeRecord");
}


/**
 * Filled out by a MovementMode during simulation tick to indicate its ending state, allowing for a residual time step and switching modes mid-tick
 */
USTRUCT(BlueprintType)
struct FJoltMovementModeTickEndState
{
	GENERATED_BODY()
	
	FJoltMovementModeTickEndState() 
	{ 
		ResetToDefaults(); 
	}

	void ResetToDefaults()
	{
		RemainingMs = 0.f;
		NextModeName = NAME_None;
		bEndedWithNoChanges = false;
	}

	// Any unused tick time
	UPROPERTY(BlueprintReadWrite, Category=Mover)
	float RemainingMs;

	UPROPERTY(BlueprintReadWrite, Category=Mover)
	FName NextModeName = NAME_None;

	// Affirms that no state changes were made during this simulation tick. Can help optimizations if modes set this during sim tick.
	UPROPERTY(BlueprintReadWrite, Category=Mover)
	bool bEndedWithNoChanges = false;

};

USTRUCT()
struct FJoltScheduledInstantMovementEffect
{
	GENERATED_BODY()

	/** Turns a FJoltInstantMovementEffect into a scheduled one (FJoltScheduledInstantMovementEffect)
	*	The effect can be scheduled to apply immediately, or scheduled to apply with a delay
	*   This function should not be called on the game thread
	*   @param World The world, used to retrieve the current server frame in async mode, or the sim time otherwise
	*   @param TimeStep the time step of the current or upcoming tick
	*   @param InstantMovementEffect the effect to schedule
	*   @param SchedulingDelaySeconds Scheduling delay to ensure it applies on all end points on the same frame (this is only perfectly accurate when simulation dt is fixed)
	*/
	static FJoltScheduledInstantMovementEffect ScheduleEffect(UWorld* World, const FJoltMoverTimeStep& TimeStep, TSharedPtr<FJoltInstantMovementEffect> InstantMovementEffect, float SchedulingDelaySeconds = 0.0f);

	bool ShouldExecuteAtFrame(int32 CurrentServerFrame) const
	{
		ensureMsgf(bIsFixedDt, TEXT("In variable delta time mode, use the version of ShouldExecute that takes a floating point time"));
		return (CurrentServerFrame >= ExecutionServerFrame);
	}

	bool ShouldExecuteAtTime(double CurrentServerTime) const
	{
		ensureMsgf(!bIsFixedDt, TEXT("In fixed delta time mode, use the version of ShouldExecute that takes a frame number"));
		return (CurrentServerTime >= ExecutionServerTimeSeconds);
	}

	void NetSerialize(const FJoltNetSerializeParams& P)
	{
		P.Ar.SerializeBits(&bIsFixedDt, 1);
		if (bIsFixedDt)
		{
			P.Ar << ExecutionServerFrame;
		}
		else
		{
			P.Ar << ExecutionServerTimeSeconds;
		}
		
		Effect->NetSerialize(P.Ar);
	}

	void ToString(FAnsiStringBuilderBase& Out) const
	{
		if (bIsFixedDt)
		{
			Out.Appendf("ExecutionServerFrame: %d", ExecutionServerFrame);
		}
		else
		{
			Out.Appendf("ExecutionDateSeconds: %f", ExecutionServerTimeSeconds);
		}

		Out.Appendf(" | Effect = % s", *(Effect.IsValid() ? Effect->ToSimpleString() : "Invalid"));
	}

	// Server frame at which this instant movement effect should be applied
	// Only valid if bIsFixedDt is true, i.e. in fixed time step mode
	UPROPERTY(VisibleAnywhere, Category = "JoltMover")
	int32 ExecutionServerFrame = INDEX_NONE;

	// Server Time (in seconds) after which this instant movement effect should be applied
	// Only valid if bIsFixedDt is false, i.e. in variable time step mode
	UPROPERTY(VisibleAnywhere, Category = "JoltMover")
	double ExecutionServerTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, Category = "JoltMover")
	bool bIsFixedDt = true;

	TSharedPtr<FJoltInstantMovementEffect> Effect;
};

/**
 * The client generates this representation of "input" to the simulated actor for one simulation frame. This can be direct mapping
 * of controls, or more abstract data. It is composed of a collection of typed structs that can be customized per project.
 */
USTRUCT(BlueprintType)
struct FJoltMoverInputCmdContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = Mover)
	FJoltMoverDataCollection Collection;

	UScriptStruct* GetStruct() const
	{
		return StaticStruct();
	}

	void NetSerialize(const FJoltNetSerializeParams& P)
	{
		bool bIgnoredResult(false);
		Collection.NetSerialize(P.Ar, P.Map, bIgnoredResult);
	}

	void ToString(FAnsiStringBuilderBase& Out) const
	{
		Collection.ToString(Out);
	}

	void Interpolate(const FJoltMoverInputCmdContext* From, const FJoltMoverInputCmdContext* To, float Pct)
	{
		Collection.Interpolate(From->Collection, To->Collection, Pct);
	}

	void Reset()
	{
		Collection.Empty();
	}
};


/** State we are evolving frame to frame and keeping in sync (frequently changing). It is composed of a collection of typed structs 
 *  that can be customized per project. Mover actors are required to have FJoltUpdatedMotionState as one of these structs.
 */
USTRUCT(BlueprintType)
struct FJoltMoverSyncState
{
	GENERATED_BODY()

public:

	// The mode we ended up in from the prior frame, and which we'll start in during the next frame
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Mover)
	FName MovementMode;

	// Additional moves influencing our proposed motion
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Mover)
	FJoltLayeredMoveGroup LayeredMoves;

	// Additional moves influencing our proposed motion
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Mover)
	FJoltLayeredMoveInstanceGroup LayeredMoveInstances;

	// Additional modifiers influencing our simulation
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Mover)
	FJoltMovementModifierGroup MovementModifiers;

	UPROPERTY(BlueprintReadWrite, Category = Mover)
	FJoltMoverDataCollection Collection;

	FJoltMoverSyncState()
	{
		MovementMode = NAME_None;
	}

	bool HasSameContents(const FJoltMoverSyncState& Other) const
	{
		return MovementMode == Other.MovementMode &&
			LayeredMoves.HasSameContents(Other.LayeredMoves) &&
			LayeredMoveInstances.HasSameContents(Other.LayeredMoveInstances) &&
			MovementModifiers.HasSameContents(Other.MovementModifiers) &&
			Collection.HasSameContents(Other.Collection);
	}

	UScriptStruct* GetStruct() const { return StaticStruct(); }


	void NetSerialize(const FJoltNetSerializeParams& P)
	{
		P.Ar << MovementMode;
		LayeredMoves.NetSerialize(P.Ar);
		LayeredMoveInstances.NetSerialize(P.Ar);
		MovementModifiers.NetSerialize(P.Ar);

		bool bIgnoredResult(false);
		Collection.NetSerialize(P.Ar, P.Map, bIgnoredResult);
	}

	void ToString(FAnsiStringBuilderBase& Out) const
	{
		Out.Appendf("JoltMovementMode: %s\n", TCHAR_TO_ANSI(*MovementMode.ToString()));
		Out.Appendf("Layered Moves: %s\n", TCHAR_TO_ANSI(*LayeredMoves.ToSimpleString()));
		Out.Appendf("Layered Moves: %s\n", TCHAR_TO_ANSI(*LayeredMoveInstances.ToSimpleString()));
		Out.Appendf("Movement Modifiers: %s\n", TCHAR_TO_ANSI(*MovementModifiers.ToSimpleString()));
		Collection.ToString(Out);
	}

	bool ShouldReconcile(const FJoltMoverSyncState& AuthorityState) const
	{
		return (MovementMode != AuthorityState.MovementMode) || 
			   Collection.ShouldReconcile(AuthorityState.Collection) ||
			   MovementModifiers.ShouldReconcile(AuthorityState.MovementModifiers);
	}

	void Interpolate(const FJoltMoverSyncState* From, const FJoltMoverSyncState* To, float Pct)
	{
		MovementMode = To->MovementMode;
		LayeredMoves = To->LayeredMoves;
		LayeredMoveInstances = To->LayeredMoveInstances;
		MovementModifiers = To->MovementModifiers;

		Collection.Interpolate(From->Collection, To->Collection, Pct);
	}

	// Resets the sync state to its default configuration and removes any
	// active or queued layered modes and modifiers
	void Reset()
	{
		MovementMode = NAME_None;
		Collection.Empty();
		LayeredMoves.Reset();
		LayeredMoveInstances.Reset();
		MovementModifiers.Reset();
	}
};

/** 
 *  Double Buffer struct for various Mover data. 
 */
template<typename T>
struct FJoltMoverDoubleBuffer
{
	// Sets all buffered data - usually used for initializing data
	void SetBufferedData(const T& InDataToCopy)
	{
		Buffer[0] = InDataToCopy;
		Buffer[1] = InDataToCopy;
	}
	
	// Gets data that is safe to read and is not being written to
	const T& GetReadable() const
	{
		return Buffer[ReadIndex];
	}

	// Gets data that is being written to and is expected to change
	T& GetWritable()
	{
		return Buffer[(ReadIndex + 1) % 2];
	}

	// Flips which data in the buffer we return for reading and writing
	void Flip()
	{
		ReadIndex = (ReadIndex + 1) % 2;
	}
	
private:
	uint32 ReadIndex = 0;
	T Buffer[2];
};

// Auxiliary state that is input into the simulation (changes rarely)
USTRUCT(BlueprintType)
struct FJoltMoverAuxStateContext
{
	GENERATED_BODY()

public:
	UScriptStruct* GetStruct() const { return StaticStruct(); }

	bool ShouldReconcile(const FJoltMoverAuxStateContext& AuthorityState) const
	{ 
		return Collection.ShouldReconcile(AuthorityState.Collection); 
	}

	void NetSerialize(const FJoltNetSerializeParams& P)
	{
		bool bIgnoredResult(false);
		Collection.NetSerialize(P.Ar, P.Map, bIgnoredResult);
	}

	void ToString(FAnsiStringBuilderBase& Out) const
	{
		Collection.ToString(Out);
	}

	void Interpolate(const FJoltMoverAuxStateContext* From, const FJoltMoverAuxStateContext* To, float Pct)
	{
		Collection.Interpolate(From->Collection, To->Collection, Pct);
	}

	UPROPERTY(BlueprintReadWrite, Category = Mover)
	FJoltMoverDataCollection Collection;
};


/**
 * Contains all state data for the start of a simulation tick
 */
USTRUCT(BlueprintType)
struct FJoltMoverTickStartData
{
	GENERATED_BODY()

	FJoltMoverTickStartData() {}
	FJoltMoverTickStartData(
			const FJoltMoverInputCmdContext& InInputCmd,
			const FJoltMoverSyncState& InSyncState,
			const FJoltMoverAuxStateContext& InAuxState)
		:  InputCmd(InInputCmd), SyncState(InSyncState), AuxState(InAuxState)
	{
	}

	UPROPERTY(BlueprintReadOnly, Category=Mover)
	FJoltMoverInputCmdContext InputCmd;
	UPROPERTY(BlueprintReadOnly, Category=Mover)
	FJoltMoverSyncState SyncState;
	UPROPERTY(BlueprintReadOnly, Category=Mover)
	FJoltMoverAuxStateContext AuxState;
};

/**
 * Contains all state data produced by a simulation tick, including new simulation state
 */
USTRUCT(BlueprintType)
struct FJoltMoverTickEndData
{
	GENERATED_BODY()

	FJoltMoverTickEndData() {}
	FJoltMoverTickEndData(
		const FJoltMoverSyncState* SyncState,
		const FJoltMoverAuxStateContext* AuxState)
	{
		this->SyncState = *SyncState;
		this->AuxState = *AuxState;
	}

	void InitForNewFrame()
	{
		MovementEndState.ResetToDefaults();
		MoveRecord.Reset();
	}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Mover)
	FJoltMoverSyncState SyncState;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Mover)
	FJoltMoverAuxStateContext AuxState;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Mover)
	FJoltMovementModeTickEndState MovementEndState;

	FJoltMovementRecord MoveRecord;
};

// Input parameters to provide context for SimulationTick functions
USTRUCT(BlueprintType)
struct FJoltSimulationTickParams
{
	GENERATED_BODY()

	// Components involved in movement by the simulation
	// This will be empty when the simulation is ticked asynchronously
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Mover)
	FJoltMovingComponentSet MovingComps;

	// Blackboard
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Mover)
	TObjectPtr<UJoltMoverBlackboard> SimBlackboard;

	// Simulation state data at the start of the tick, including Input Cmd
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Mover)
	FJoltMoverTickStartData StartState;

	// Time and frame information for this tick
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Mover)
	FJoltMoverTimeStep TimeStep;

	// Proposed movement for this tick
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Mover)
	FJoltProposedMove ProposedMove;
};

UINTERFACE(MinimalAPI, BlueprintType)
class UJoltMoverInputProducerInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * MoverInputProducerInterface: API for any object that can produce input for a Mover simulation frame
 */
class IJoltMoverInputProducerInterface : public IInterface
{
	GENERATED_BODY()

public:
	/** Contributes additions to the input cmd for this simulation frame. Typically this is translating accumulated user input (or AI state) into parameters that affect movement. */
	UFUNCTION(BlueprintNativeEvent)
	JOLTMOVER_API void ProduceInput(int32 SimTimeMs, FJoltMoverInputCmdContext& InputCmdResult);
};


/** 
 * FJoltMoverPredictTrajectoryParams: parameter block for querying future trajectory samples based on a starting state
 * See UJoltMoverComponent::GetPredictedTrajectory
 */
USTRUCT(BlueprintType)
struct FJoltMoverPredictTrajectoryParams
{
	GENERATED_BODY()

	/** How many samples to predict into the future, including the first sample, which is always a snapshot of the
	 *  starting state with 0 accumulated time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover, meta = (ClampMin = 1))
	int32 NumPredictionSamples = 1;

	/* How much time between predicted samples */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover, meta = (ClampMin = 0.00001))
	float SecondsPerSample = 0.333f;

	/** If true, samples are based on the visual component transform, rather than the 'updated' movement root.
	 *  Typically, this is a mesh with its component location at the bottom of the collision primitive.
	 *  If false, samples are from the movement root. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	bool bUseVisualComponentRoot = false;

	/** If true, gravity will not taken into account during prediction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	bool bDisableGravity = false;

 	/** Optional starting sync state. If not set, prediction will begin from the current state. */
 	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
 	TOptional<FJoltMoverSyncState> OptionalStartSyncState;
 
 	/** Optional starting aux state. If not set, prediction will begin from the current state. */
 	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
 	TOptional<FJoltMoverAuxStateContext> OptionalStartAuxState;

 	/** Optional input cmds to use, one per sample. If none are specified, prediction will begin with last-used inputs. 
 	 *  If too few are specified for the number of samples, the final input in the array will be used repeatedly to cover remaining samples. */
 	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
 	TArray<FJoltMoverInputCmdContext> OptionalInputCmds;

};

USTRUCT()
struct FJoltMoverSimEventGameThreadContext
{
	GENERATED_BODY()

public:
	UJoltMoverComponent* MoverComp = nullptr;
};

USTRUCT()
struct FJoltMoverSimulationEventData
{
	GENERATED_BODY()

	using FJoltEventProcessedCallbackPtr = std::function<void(const FJoltMoverSimulationEventData& Data, const FJoltMoverSimEventGameThreadContext& GameThreadContext)>;

	FJoltMoverSimulationEventData(double InEventTimeMs, FJoltEventProcessedCallbackPtr InEventProcessedCallback = nullptr)
		: EventProcessedCallback(InEventProcessedCallback)
		, EventTimeMs(InEventTimeMs)
	{
	}
	FJoltMoverSimulationEventData() {}
	virtual ~FJoltMoverSimulationEventData() {}

	// User must override
	JOLTMOVER_API virtual UScriptStruct* GetScriptStruct() const;

	template<typename T>
	T* CastTo_Mutable()
	{
		return T::StaticStruct() == GetScriptStruct() ? static_cast<T*>(this) : nullptr;
	}

	template<typename T>
	const T* CastTo() const
	{
		return const_cast<const T*>(const_cast<FJoltMoverSimulationEventData*>(this)->CastTo_Mutable<T>());
	}

	void OnEventProcessed(const FJoltMoverSimEventGameThreadContext& GameThreadContext) const
	{
		if (EventProcessedCallback)
		{
			EventProcessedCallback(*this, GameThreadContext);
		}
	}

	void SetEventProcessedCallback(FJoltEventProcessedCallbackPtr Callback)
	{
		EventProcessedCallback = Callback;
	}

private:
	// This callback is fired when the event is processed on the game thread
	// This is called before and in addition to any type based handling
	FJoltEventProcessedCallbackPtr EventProcessedCallback = nullptr;

public:
	double EventTimeMs = 0.0;
};

USTRUCT()
struct FJoltMovementModeChangedEventData : public FJoltMoverSimulationEventData
{
	GENERATED_BODY()

	FJoltMovementModeChangedEventData(float InEventTimeMs, const FName InPreviousModeName, const FName InNewModeName, FJoltEventProcessedCallbackPtr InEventProcessedCallback = nullptr)
		: FJoltMoverSimulationEventData(InEventTimeMs, InEventProcessedCallback)
		, PreviousModeName(InPreviousModeName)
		, NewModeName(InNewModeName)
	{
	}
	FJoltMovementModeChangedEventData() {}

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FJoltMovementModeChangedEventData::StaticStruct();
	}

	FName PreviousModeName = NAME_None;
	FName NewModeName = NAME_None;
};

USTRUCT()
struct FJoltTeleportSucceededEventData : public FJoltMoverSimulationEventData
{
	GENERATED_BODY()

	FJoltTeleportSucceededEventData(float InEventTimeMs, const FVector& InFromLocation, const FQuat& InFromRotation, const FVector& InToLocation, const FQuat& InToRotation)
		: FJoltMoverSimulationEventData(InEventTimeMs)
		, FromLocation(InFromLocation)
		, FromRotation(InFromRotation)
		, ToLocation(InToLocation)
		, ToRotation(InToRotation)
	{
	}
	FJoltTeleportSucceededEventData() {}

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FJoltTeleportSucceededEventData::StaticStruct();
	}

	FVector FromLocation;
	FQuat FromRotation;
	FVector ToLocation;
	FQuat ToRotation;
};

UENUM(BlueprintType)
enum class ETeleportFailureReason : uint8
{
	Reason_NotAvailable UMETA(DisplayName = "Reason Not Available", Tooltip = "A reason for the teleport failure was not indicated"),
};

USTRUCT()
struct FJoltTeleportFailedEventData : public FJoltMoverSimulationEventData
{
	GENERATED_BODY()

	FJoltTeleportFailedEventData(float InEventTimeMs, const FVector& InFromLocation, const FQuat& InFromRotation, const FVector& InToLocation, const FQuat& InToRotation, ETeleportFailureReason InTeleportFailureReason)
		: FJoltMoverSimulationEventData(InEventTimeMs)
		, FromLocation(InFromLocation)
		, FromRotation(InFromRotation)
		, ToLocation(InToLocation)
		, ToRotation(InToRotation)
		, TeleportFailureReason(InTeleportFailureReason)
	{
	}
	FJoltTeleportFailedEventData() {}

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FJoltTeleportFailedEventData::StaticStruct();
	}

	FVector FromLocation;
	FQuat FromRotation;
	FVector ToLocation;
	FQuat ToRotation;
	ETeleportFailureReason TeleportFailureReason;
};

namespace UE::JoltMover
{
	struct FJoltSimulationOutputData
	{
		JOLTMOVER_API void Reset();
		JOLTMOVER_API void Interpolate(const FJoltSimulationOutputData& From, const FJoltSimulationOutputData& To, float Alpha, double SimTimeMs);

		FJoltMoverSyncState SyncState;
		FJoltMoverInputCmdContext LastUsedInputCmd;
		FJoltMoverDataCollection AdditionalOutputData;
		TArray<TSharedPtr<FJoltMoverSimulationEventData>> Events;
	};

	class FJoltSimulationOutputRecord
	{
	public:
		struct FData
		{
			JOLTMOVER_API void Reset();

			FJoltMoverTimeStep TimeStep;
			FJoltSimulationOutputData SimOutputData;
		};

		JOLTMOVER_API void Add(const FJoltMoverTimeStep& InTimeStep, const FJoltSimulationOutputData& InData);

		JOLTMOVER_API const FJoltSimulationOutputData& GetLatest() const;

		/** This will create an interpolated output and extract events from the stored data with time stamps up until the input time */
		JOLTMOVER_API void CreateInterpolatedResult(double AtBaseTimeMs, FJoltMoverTimeStep& OutTimeStep, FJoltSimulationOutputData& OutData);

		JOLTMOVER_API void Clear();

	private:
		FData Data[2];
		TArray<TSharedPtr<FJoltMoverSimulationEventData>> Events;
		uint8 CurrentIndex = 1;
	};

} // namespace UE::JoltMover
