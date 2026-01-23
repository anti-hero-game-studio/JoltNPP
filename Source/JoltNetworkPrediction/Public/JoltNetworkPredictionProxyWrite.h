// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JoltNetworkPredictionProxy.h"
#include "JoltNetworkPredictionTrace.h"

inline void FJoltNetworkPredictionProxy::TraceViaConfigFunc(EConfigAction Action)
{
	// The ConfigFunc allows use to use the registered ModelDef type to access FJoltNetworkPredictionDriver<ModelDef>::TraceUserState
	// this allows for per-ModelDef customizations but more importantly will call State->ToString on the correct child class.
	// consider FChildSyncState : FBaseSyncState{}; with a base driver class that calls WriteSyncState<FBaseSyncState>(...);
#if UE_JNP_TRACE_USER_STATES_ENABLED
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(JoltNetworkPredictionChannel))
	{
		ConfigFunc(this, FJoltNetworkPredictionID(), Action);
	}
#endif
}

template<typename TInputCmd>
const TInputCmd* FJoltNetworkPredictionProxy::WriteInputCmd(TFunctionRef<void(TInputCmd&)> WriteFunc,const FAnsiStringView& TraceMsg)
{
	if (TInputCmd* InputCmd = static_cast<TInputCmd*>(View.PendingInputCmd))
	{
		WriteFunc(*InputCmd);
		
		UE_JNP_TRACE_OOB_STATE_MOD(ID.GetTraceID(), View.PendingFrame, TraceMsg);
		TraceViaConfigFunc(EConfigAction::TraceInput);
		return InputCmd;
	}
	return nullptr;
}

template<typename TSyncState>
const TSyncState* FJoltNetworkPredictionProxy::WriteSyncState(TFunctionRef<void(TSyncState&)> WriteFunc, const FAnsiStringView& TraceMsg)
{
	if (TSyncState* SyncState = static_cast<TSyncState*>(View.PendingSyncState))
	{
		WriteFunc(*SyncState);
		UE_JNP_TRACE_OOB_STATE_MOD(ID.GetTraceID(), View.PendingFrame, TraceMsg);
		ConfigFunc(this, FJoltNetworkPredictionID(), EConfigAction::TraceSync);
		return SyncState;
	}
	return nullptr;
}

template<typename TSyncState>
const TSyncState* FJoltNetworkPredictionProxy::WritePresentationSyncState(TFunctionRef<void(TSyncState&)> WriteFunc, const FAnsiStringView& TraceMsg)
{
	if (TSyncState* SyncState = static_cast<TSyncState*>(View.PresentationSyncState))
	{
		WriteFunc(*SyncState);
		UE_JNP_TRACE_OOB_STATE_MOD(ID.GetTraceID(), View.PendingFrame, TraceMsg);
		ConfigFunc(this, FJoltNetworkPredictionID(), EConfigAction::TraceSync);
		return SyncState;
	}
	return nullptr;
}

template<typename TSyncState>
const TSyncState* FJoltNetworkPredictionProxy::WritePrevPresentationSyncState(TFunctionRef<void(TSyncState&)> WriteFunc, const FAnsiStringView& TraceMsg)
{
	if (TSyncState* SyncState = static_cast<TSyncState*>(View.PrevPresentationSyncState))
	{
		WriteFunc(*SyncState);
		UE_JNP_TRACE_OOB_STATE_MOD(ID.GetTraceID(), View.PendingFrame, TraceMsg);
		ConfigFunc(this, FJoltNetworkPredictionID(), EConfigAction::TraceSync);
		return SyncState;
	}
	return nullptr;
}

template<typename TAuxState>
const TAuxState* FJoltNetworkPredictionProxy::WriteAuxState(TFunctionRef<void(TAuxState&)> WriteFunc, const FAnsiStringView& TraceMsg)
{
	if (TAuxState* AuxState = static_cast<TAuxState*>(View.PendingAuxState))
	{
		WriteFunc(*AuxState);
		UE_JNP_TRACE_OOB_STATE_MOD(ID.GetTraceID(), View.PendingFrame, TraceMsg);
		ConfigFunc(this, FJoltNetworkPredictionID(), EConfigAction::TraceAux);
		return AuxState;
	}
	return nullptr;
}

template<typename TAuxState>
const TAuxState* FJoltNetworkPredictionProxy::WritePresentationAuxState(TFunctionRef<void(TAuxState&)> WriteFunc, const FAnsiStringView& TraceMsg)
{
	if (TAuxState* AuxState = static_cast<TAuxState*>(View.PresentationAuxState))
	{
		WriteFunc(*AuxState);
		UE_JNP_TRACE_OOB_STATE_MOD(ID.GetTraceID(), View.PendingFrame, TraceMsg);
		ConfigFunc(this, FJoltNetworkPredictionID(), EConfigAction::TraceAux);
		return AuxState;
	}
	return nullptr;
}

template<typename TAuxState>
const TAuxState* FJoltNetworkPredictionProxy::WritePrevPresentationAuxState(TFunctionRef<void(TAuxState&)> WriteFunc, const FAnsiStringView& TraceMsg)
{
	if (TAuxState* AuxState = static_cast<TAuxState*>(View.PrevPresentationAuxState))
	{
		WriteFunc(*AuxState);
		UE_JNP_TRACE_OOB_STATE_MOD(ID.GetTraceID(), View.PendingFrame, TraceMsg);
		ConfigFunc(this, FJoltNetworkPredictionID(), EConfigAction::TraceAux);
		return AuxState;
	}
	return nullptr;
}
