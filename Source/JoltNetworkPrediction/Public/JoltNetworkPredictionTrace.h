// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JoltNetworkPredictionDriver.h"
#include "JoltNetworkPredictionID.h"

enum ENetRole : int;
struct FJoltNetworkPredictionInstanceArchetype;
struct FJoltNetworkPredictionInstanceConfig;

#ifndef UE_JNP_TRACE_ENABLED
#define UE_JNP_TRACE_ENABLED (WITH_EDITOR || UE_BUILD_TEST)
#endif

// Tracing user state (content) can generate a lot of data. So this can be turned off here
#ifndef UE_JNP_TRACE_USER_STATES_ENABLED
#define UE_JNP_TRACE_USER_STATES_ENABLED 1
#endif


#if UE_JNP_TRACE_ENABLED

// Traces what caused a reconcile (added in user ShouldReconcile function)
// Note this actually adds the 'return true' logic for reconciliation
#define UE_JNP_TRACE_RECONCILE(Condition, Str) if (Condition) { FJoltNetworkPredictionTrace::TraceReconcile(Str); return true; }

// General trace to push the active simulation's trace ID
#define UE_JNP_TRACE_SIM(TraceID) FJoltNetworkPredictionTrace::TraceSimulationScope(TraceID)

// Called when simulation is created. (Note this also sets a Scope for tracing the initial user states next)
#define UE_JNP_TRACE_SIM_CREATED(ID, Driver, ModelDef) FJoltNetworkPredictionTrace::TraceSimulationCreated<ModelDef>(ID, Driver)

// Trace config of sim changing
#define UE_JNP_TRACE_SIM_CONFIG(TraceID, NetRole, bHasNetConnection, Archetype, Config, ServiceMask) FJoltNetworkPredictionTrace::TraceSimulationConfig(TraceID, NetRole, bHasNetConnection, Archetype, Config, (int32)ServiceMask);

// Called when a PIE session is started. This is so we can keep our sets of worlds/simulations separate in between runs.
#define UE_JNP_TRACE_PIE_START() FJoltNetworkPredictionTrace::TracePIEStart()

// Called during WorldPreInit. This mainly just ensure we have a valid trace context so that actors loaded with the map can trace their \initialization
#define UE_JNP_TRACE_WORLD_PREINIT() FJoltNetworkPredictionTrace::TraceWorldPreInit()

// Generic fault/error message that gets bubbled up in the NP Insights UI
#define UE_JNP_TRACE_SYSTEM_FAULT(Format, ...) FJoltNetworkPredictionTrace::TraceSystemFault(TEXT(Format), ##__VA_ARGS__)

// Trace engine frame starting for GameInstance
#define UE_JNP_TRACE_WORLD_FRAME_START(GameInstance, DeltaSeconds) FJoltNetworkPredictionTrace::TraceWorldFrameStart(GameInstance, DeltaSeconds)

// Called to set the general tick state
#define UE_JNP_TRACE_PUSH_TICK(StartMS, DeltaMS, OutputFrame) FJoltNetworkPredictionTrace::TraceTick(StartMS, DeltaMS, OutputFrame)

// Called when an actual instance ticks (after calling UE_JNP_TRACE_PUSH_TICK)
#define UE_JNP_TRACE_SIM_TICK(TraceID) FJoltNetworkPredictionTrace::TraceSimTick(TraceID)

// General trace to push the active simulation's trace ID and setup for updating the traced state for an already ticked frame (Used to update async data after completion)
#define UE_JNP_TRACE_SIM_STATE(TraceID) FJoltNetworkPredictionTrace::TraceSimState(TraceID)

// Called when we receive networked data (regardless of what we end up doing with it)
#define UE_JNP_TRACE_NET_RECV(Frame, TimeMS) FJoltNetworkPredictionTrace::TraceNetRecv(Frame, TimeMS)

// Called when ShouldReconcile returns true, signaling a rollback/correction is required
#define UE_JNP_TRACE_SHOULD_RECONCILE(TraceID) FJoltNetworkPredictionTrace::TraceShouldReconcile(TraceID)

// Called when received data is injected back into the local frame buffer (Note that the sim itself may not have been in error, we may be rolling "everything" back)
#define UE_JNP_TRACE_ROLLBACK_INJECT(TraceID) FJoltNetworkPredictionTrace::TraceRollbackInject(TraceID)

// Called before running input producing services
#define UE_JNP_TRACE_PUSH_INPUT_FRAME(Frame) FJoltNetworkPredictionTrace::TracePushInputFrame(Frame)

// Traces current local frame # -> server frame # offset
#define UE_JNP_TRACE_FIXED_TICK_OFFSET(Offset, bChanged) FJoltNetworkPredictionTrace::TraceFixedTickOffset(Offset, bChanged)

// Called to trace buffered input state
#define UE_JNP_TRACE_BUFFERED_INPUT(NumBufferedFrames, bFault) FJoltNetworkPredictionTrace::TraceBufferedInput(NumBufferedFrames, bFault)

// Trace call to Driver's ProduceInput function
#define UE_JNP_TRACE_PRODUCE_INPUT(TraceID) FJoltNetworkPredictionTrace::TraceProduceInput(TraceID)

// Called to indicate we are about to write state to the buffers outside of the normal simulation tick/netrecive. TODO: add char* identifier to debug where the mod came from
#define UE_JNP_TRACE_OOB_STATE_MOD(TraceID, Frame, StrView) FJoltNetworkPredictionTrace::TraceOOBStateMod(TraceID, Frame, StrView)

// Called whenever a new user state has been inserted into the buffers. Analysis will determine "how" it got there from previous trace events
#define UE_JNP_TRACE_USER_STATE_INPUT(ModelDef, UserState) FJoltNetworkPredictionTrace::TraceUserState<ModelDef>(UserState, FJoltNetworkPredictionTrace::ETraceUserState::Input)
#define UE_JNP_TRACE_USER_STATE_SYNC(ModelDef, UserState) FJoltNetworkPredictionTrace::TraceUserState<ModelDef>(UserState, FJoltNetworkPredictionTrace::ETraceUserState::Sync)
#define UE_JNP_TRACE_USER_STATE_AUX(ModelDef, UserState) FJoltNetworkPredictionTrace::TraceUserState<ModelDef>(UserState, FJoltNetworkPredictionTrace::ETraceUserState::Aux)


#else

// Compiled out
#define UE_JNP_TRACE_RECONCILE(Condition, Str) if (Condition) { return true; }
#define UE_JNP_TRACE_SIM(...)
#define UE_JNP_TRACE_SIM_CREATED(...)
#define UE_JNP_TRACE_SIM_CONFIG(...)

#define UE_JNP_TRACE_PIE_START(...)
#define UE_JNP_TRACE_WORLD_PREINIT(...)
#define UE_JNP_TRACE_SYSTEM_FAULT(Format, ...) UE_LOG(LogJoltNetworkPrediction, Warning, TEXT(Format), ##__VA_ARGS__);
#define UE_JNP_TRACE_WORLD_FRAME_START(...)
#define UE_JNP_TRACE_PUSH_TICK(...)
#define UE_JNP_TRACE_SIM_TICK(...)
#define UE_JNP_TRACE_SIM_STATE(...)
#define UE_JNP_TRACE_NET_RECV(...)
#define UE_JNP_TRACE_SHOULD_RECONCILE(...)
#define UE_JNP_TRACE_ROLLBACK_INJECT(...)
#define UE_JNP_TRACE_PUSH_INPUT_FRAME(...)
#define UE_JNP_TRACE_FIXED_TICK_OFFSET(...)
#define UE_JNP_TRACE_PRODUCE_INPUT(...)
#define UE_JNP_TRACE_BUFFERED_INPUT(...)
#define UE_JNP_TRACE_OOB_STATE_MOD(...)

#define UE_JNP_TRACE_USER_STATE_INPUT(...)
#define UE_JNP_TRACE_USER_STATE_SYNC(...)
#define UE_JNP_TRACE_USER_STATE_AUX(...)

#endif // UE_JNP_TRACE_ENABLED

UE_TRACE_CHANNEL_EXTERN(JoltNetworkPredictionChannel, JOLTNETWORKPREDICTION_API);

class AActor;
class UGameInstance;

template<typename Model>
struct TJoltNetworkedSimulationState;

class JOLTNETWORKPREDICTION_API FJoltNetworkPredictionTrace
{
public:

	template<typename ModelDef>
	static void TraceSimulationCreated(FJoltNetworkPredictionID ID, typename ModelDef::Driver* Driver)
	{
		TStringBuilder<256> Builder;
		FJoltNetworkPredictionDriver<ModelDef>::GetTraceString(Driver, Builder);
		TraceSimulationCreated_Internal(ID, Builder);
	}

	static void TraceWorldFrameStart(UGameInstance* GameInstance, float DeltaSeconds);
	static void TraceSimulationConfig(int32 TraceID, ENetRole NetRole, bool bHasNetConnection, const FJoltNetworkPredictionInstanceArchetype& Archetype, const FJoltNetworkPredictionInstanceConfig& Config, int32 ServiceMask);

	static void TracePIEStart();
	static void TraceWorldPreInit();
	static void TraceSystemFault(const TCHAR* Fmt, ...);

	static void TraceSimulationScope(int32 TraceID);

	static void TraceTick(int32 StartMS, int32 DeltaMS, int32 OutputFrame);
	static void TraceSimTick(int32 TraceID);
	// Used to lazy update user state after a simtick
	static void TraceSimState(int32 TraceID);

	static void TraceNetRecv(int32 Frame, int32 TimeMS);
	
	static void TraceReconcile(const FAnsiStringView& StrView); 

	static void TraceShouldReconcile(int32 TraceID);
	static void TraceRollbackInject(int32 TraceID);

	static void TracePushInputFrame(int32 Frame);
	static void TraceFixedTickOffset(int32 Offset, bool bChanged);
	static void TraceBufferedInput(int32 NumBufferedFrames, bool bFault);
	static void TraceProduceInput(int32 TraceID);

	static void TraceOOBStateMod(int32 SimulationId, int32 Frame, const FAnsiStringView& StrView);

	enum class ETraceUserState : uint8
	{
		Input,
		Sync,
		Aux,
	};

	template<typename ModelDef, typename StateType>
	static void TraceUserState(const StateType* State, ETraceUserState StateTypeEnum)
	{
#if UE_JNP_TRACE_USER_STATES_ENABLED
		if (std::is_void_v<StateType>)
		{
			return;
		}

		if (UE_TRACE_CHANNELEXPR_IS_ENABLED(JoltNetworkPredictionChannel))
		{
			jnpCheckSlow(State);

			TAnsiStringBuilder<512> Builder;
			FJoltNetworkPredictionDriver<ModelDef>::TraceUserStateString(State, Builder);
			TraceUserState_Internal(StateTypeEnum, Builder);
		}
#endif
	}

private:

	static void TraceSimulationCreated_Internal(FJoltNetworkPredictionID ID, FStringBuilderBase& Builder);

	// ----------------------------------------------------------------------------------
	

	static void TraceUserState_Internal(ETraceUserState StateType, FAnsiStringBuilderBase& Builder);

	friend struct FScopedSimulationTraceHelper;
};

