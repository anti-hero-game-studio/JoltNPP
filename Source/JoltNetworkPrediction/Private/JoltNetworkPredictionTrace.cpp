// Copyright Epic Games, Inc. All Rights Reserved.


#include "JoltNetworkPredictionTrace.h"
#include "Containers/StringFwd.h"
#include "Engine/GameInstance.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "JoltNetworkPredictionLog.h"
#include "Trace/Trace.h"
#include "Trace/Trace.inl"

// TODO:
// Should update string tracing with UE::Trace::AnsiString

namespace NetworkPredictionTraceInternal
{
	enum class EJoltNetworkPredictionTraceVersion : uint32
	{
		Initial = 1,
	};

	static constexpr EJoltNetworkPredictionTraceVersion NetworkPredictionTraceVersion = EJoltNetworkPredictionTraceVersion::Initial;
};

UE_TRACE_CHANNEL_DEFINE(JoltNetworkPredictionChannel)

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, SimScope)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
UE_TRACE_EVENT_END()

// Trace a simulation creation. GroupName is attached as attachment.
UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, SimulationCreated)
	UE_TRACE_EVENT_FIELD(uint32, SimulationID) // server assigned (shared client<->server)
	UE_TRACE_EVENT_FIELD(int32, TraceID) // process unique id
	UE_TRACE_EVENT_FIELD(UE::Trace::AnsiString, DebugName)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, SimulationConfig)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
	UE_TRACE_EVENT_FIELD(uint8, NetRole)
	UE_TRACE_EVENT_FIELD(uint8, bHasNetConnection)
	UE_TRACE_EVENT_FIELD(uint8, TickingPolicy)
	UE_TRACE_EVENT_FIELD(uint8, NetworkLOD)
	UE_TRACE_EVENT_FIELD(int32, ServiceMask)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, SimulationScope)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, SimState)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, Version)
	UE_TRACE_EVENT_FIELD(uint32, Version)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, WorldPreInit)
	UE_TRACE_EVENT_FIELD(uint64, EngineFrameNumber)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, PieBegin)
	UE_TRACE_EVENT_FIELD(uint64, EngineFrameNumber)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, WorldFrameStart)
	UE_TRACE_EVENT_FIELD(uint64, EngineFrameNumber)
	UE_TRACE_EVENT_FIELD(float, DeltaSeconds)
UE_TRACE_EVENT_END()

// General system fault. Log message is in attachment
UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, SystemFault)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Message)
UE_TRACE_EVENT_END()

// Traces general tick state (called before ticking N sims)
UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, Tick)
	UE_TRACE_EVENT_FIELD(int32, StartMS)
	UE_TRACE_EVENT_FIELD(int32, DeltaMS)
	UE_TRACE_EVENT_FIELD(int32, OutputFrame)
UE_TRACE_EVENT_END()

// Signals that the given sim has done a tick. Expected to be called after the 'Tick' event has been traced
UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, SimTick)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
UE_TRACE_EVENT_END()

// Signals that we are in are receiving a NetSerialize function
UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, NetRecv)
	UE_TRACE_EVENT_FIELD(int32, Frame)
	UE_TRACE_EVENT_FIELD(int32, TimeMS)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, ShouldReconcile)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, Reconcile)
	UE_TRACE_EVENT_FIELD(UE::Trace::AnsiString, UserString)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, RollbackInject)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, PushInputFrame)
	UE_TRACE_EVENT_FIELD(int32, Frame)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, FixedTickOffset)
	UE_TRACE_EVENT_FIELD(int32, Offset)
	UE_TRACE_EVENT_FIELD(bool, Changed)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, BufferedInput)
	UE_TRACE_EVENT_FIELD(int32, NumBufferedFrames)
	UE_TRACE_EVENT_FIELD(bool, bFault)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, ProduceInput)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, OOBStateMod)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
	UE_TRACE_EVENT_FIELD(int32, Frame)
	UE_TRACE_EVENT_FIELD(UE::Trace::AnsiString, Source)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, InputCmd)
	UE_TRACE_EVENT_FIELD(UE::Trace::AnsiString, Value)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, SyncState)
	UE_TRACE_EVENT_FIELD(UE::Trace::AnsiString, Value)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(JoltNetworkPrediction, AuxState)
	UE_TRACE_EVENT_FIELD(UE::Trace::AnsiString, Value)
UE_TRACE_EVENT_END()

// ---------------------------------------------------------------------------

void FJoltNetworkPredictionTrace::TraceSimulationCreated_Internal(FJoltNetworkPredictionID ID, FStringBuilderBase& Builder)
{
	const uint16 AttachmentSize = Builder.Len() * sizeof(FStringBuilderBase::ElementType);

	UE_TRACE_LOG(JoltNetworkPrediction, SimulationCreated, JoltNetworkPredictionChannel)
		<< SimulationCreated.SimulationID((int32)ID)
		<< SimulationCreated.TraceID(ID.GetTraceID())
		<< SimulationCreated.DebugName(Builder.ToString(), Builder.Len());
}

void FJoltNetworkPredictionTrace::TraceWorldFrameStart(UGameInstance* GameInstance, float DeltaSeconds)
{
	if (!GameInstance || GameInstance->GetWorld()->GetNetMode() == NM_Standalone)
	{
		// No networking yet, don't start tracing
		return;
	}

	UE_TRACE_LOG(JoltNetworkPrediction, WorldFrameStart, JoltNetworkPredictionChannel)
		<< WorldFrameStart.EngineFrameNumber(GFrameNumber)
		<< WorldFrameStart.DeltaSeconds(DeltaSeconds);
}

void FJoltNetworkPredictionTrace::TraceSimulationConfig(int32 TraceID, ENetRole NetRole, bool bHasNetConnection, const FJoltNetworkPredictionInstanceArchetype& Archetype, const FJoltNetworkPredictionInstanceConfig& Config, int32 ServiceMask)
{
	jnpEnsureMsgf(NetRole != ENetRole::ROLE_None && NetRole != ENetRole::ROLE_MAX, TEXT("Invalid NetRole %d"), NetRole);

	UE_TRACE_LOG(JoltNetworkPrediction, SimulationConfig, JoltNetworkPredictionChannel)
		<< SimulationConfig.TraceID(TraceID)
		<< SimulationConfig.NetRole((uint8)NetRole)
		<< SimulationConfig.bHasNetConnection((uint8)bHasNetConnection)
		<< SimulationConfig.TickingPolicy((uint8)Archetype.TickingMode)
		<< SimulationConfig.ServiceMask(ServiceMask);		
}

void FJoltNetworkPredictionTrace::TraceSimulationScope(int32 TraceID)
{
	UE_TRACE_LOG(JoltNetworkPrediction, SimulationScope, JoltNetworkPredictionChannel)
		<< SimulationScope.TraceID(TraceID);
}

void FJoltNetworkPredictionTrace::TraceSimState(int32 TraceID)
{
	UE_TRACE_LOG(JoltNetworkPrediction, SimState, JoltNetworkPredictionChannel)
		<< SimState.TraceID(TraceID);
}

void FJoltNetworkPredictionTrace::TraceTick(int32 StartMS, int32 DeltaMS, int32 OutputFrame)
{
	UE_TRACE_LOG(JoltNetworkPrediction, Tick, JoltNetworkPredictionChannel)
		<< Tick.StartMS(StartMS)
		<< Tick.DeltaMS(DeltaMS)
		<< Tick.OutputFrame(OutputFrame);
}

void FJoltNetworkPredictionTrace::TraceSimTick(int32 TraceID)
{
	UE_TRACE_LOG(JoltNetworkPrediction, SimTick, JoltNetworkPredictionChannel)
		<< SimTick.TraceID(TraceID);
}

void FJoltNetworkPredictionTrace::TraceUserState_Internal(ETraceUserState StateType, FAnsiStringBuilderBase& Builder)
{
	switch(StateType)
	{
		case ETraceUserState::Input:
		{
			UE_TRACE_LOG(JoltNetworkPrediction, InputCmd, JoltNetworkPredictionChannel)
				<< InputCmd.Value(Builder.GetData(), Builder.Len());
			break;
		}
		case ETraceUserState::Sync:
		{
			UE_TRACE_LOG(JoltNetworkPrediction, SyncState, JoltNetworkPredictionChannel)
				<< SyncState.Value(Builder.GetData(), Builder.Len());
			break;
		}
		case ETraceUserState::Aux:
		{
			UE_TRACE_LOG(JoltNetworkPrediction, AuxState, JoltNetworkPredictionChannel)
				<< AuxState.Value(Builder.GetData(), Builder.Len());
			break;
		}
	}
}

void FJoltNetworkPredictionTrace::TraceNetRecv(int32 Frame, int32 TimeMS)
{
	UE_TRACE_LOG(JoltNetworkPrediction, NetRecv, JoltNetworkPredictionChannel)
		<< NetRecv.Frame(Frame)
		<< NetRecv.TimeMS(TimeMS);
}

void FJoltNetworkPredictionTrace::TraceReconcile(const FAnsiStringView& StrView)
{
	UE_TRACE_LOG(JoltNetworkPrediction, Reconcile, JoltNetworkPredictionChannel)
		<< Reconcile.UserString(StrView.GetData(), StrView.Len());
}

void FJoltNetworkPredictionTrace::TraceShouldReconcile(int32 TraceID)
{
	UE_TRACE_LOG(JoltNetworkPrediction, ShouldReconcile, JoltNetworkPredictionChannel)
		<< ShouldReconcile.TraceID(TraceID);
}

void FJoltNetworkPredictionTrace::TraceRollbackInject(int32 TraceID)
{
	UE_TRACE_LOG(JoltNetworkPrediction, RollbackInject, JoltNetworkPredictionChannel)
		<< RollbackInject.TraceID(TraceID);
}

void FJoltNetworkPredictionTrace::TracePIEStart()
{
	UE_TRACE_LOG(JoltNetworkPrediction, PieBegin, JoltNetworkPredictionChannel)
		<< PieBegin.EngineFrameNumber(GFrameNumber);
}

void FJoltNetworkPredictionTrace::TraceWorldPreInit()
{
	UE_TRACE_LOG(JoltNetworkPrediction, Version, JoltNetworkPredictionChannel)
		<< Version.Version((uint32)NetworkPredictionTraceInternal::NetworkPredictionTraceVersion);

	UE_TRACE_LOG(JoltNetworkPrediction, WorldPreInit, JoltNetworkPredictionChannel)
		<< WorldPreInit.EngineFrameNumber(GFrameNumber);
}

void FJoltNetworkPredictionTrace::TracePushInputFrame(int32 Frame)
{
	UE_TRACE_LOG(JoltNetworkPrediction, PushInputFrame, JoltNetworkPredictionChannel)
		<< PushInputFrame.Frame(Frame);
}

void FJoltNetworkPredictionTrace::TraceFixedTickOffset(int32 Offset, bool bChanged)
{
	UE_TRACE_LOG(JoltNetworkPrediction, FixedTickOffset, JoltNetworkPredictionChannel)
		<< FixedTickOffset.Offset(Offset)
		<< FixedTickOffset.Changed(bChanged);
}

void FJoltNetworkPredictionTrace::TraceBufferedInput(int32 NumBufferedFrames, bool bFault)
{
	UE_TRACE_LOG(JoltNetworkPrediction, BufferedInput, JoltNetworkPredictionChannel)
		<< BufferedInput.NumBufferedFrames(NumBufferedFrames)
		<< BufferedInput.bFault(bFault);
}

void FJoltNetworkPredictionTrace::TraceProduceInput(int32 TraceID)
{
	UE_TRACE_LOG(JoltNetworkPrediction, ProduceInput, JoltNetworkPredictionChannel)
		<< ProduceInput.TraceID(TraceID);
}

void FJoltNetworkPredictionTrace::TraceOOBStateMod(int32 TraceID, int32 Frame, const FAnsiStringView& StrView)
{
	UE_TRACE_LOG(JoltNetworkPrediction, OOBStateMod, JoltNetworkPredictionChannel)
		<< OOBStateMod.TraceID(TraceID)
		<< OOBStateMod.Frame(Frame)
		<< OOBStateMod.Source(StrView.GetData(), StrView.Len());
}

void FJoltNetworkPredictionTrace::TraceSystemFault(const TCHAR* Fmt, ...)
{
	TStringBuilder<512> Builder;

	va_list Args;
	va_start(Args, Fmt);
	Builder.AppendV(Fmt, Args);
	va_end(Args);

	UE_LOG(LogJoltNetworkPrediction, Log, TEXT("SystemFault: %s"), Builder.ToString());

	UE_TRACE_LOG(JoltNetworkPrediction, SystemFault, JoltNetworkPredictionChannel)
		<< SystemFault.Message(Builder.GetData(), Builder.Len());
}
