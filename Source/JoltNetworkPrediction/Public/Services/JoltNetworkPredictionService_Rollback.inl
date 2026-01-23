// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// HEADER_UNIT_SKIP - Not included directly

#include "JoltNetworkPredictionCVars.h"
#include "JoltNetworkPredictionLog.h"
#include "Services/JoltNetworkPredictionInstanceData.h"
#include "Services/JoltNetworkPredictionService_Ticking.inl"

namespace NetworkPredictionCVars
{
	JOLTNETSIM_DEVCVAR_SHIPCONST_INT(ForceReconcile,			0, "j.np.ForceReconcile",				"Force a single reconcile back to the last server-acknoledged frame. When used with np.ForceReconcileExtraFrames, additional frames can be rolled back. No effect on server. Resets after use.");
	JOLTNETSIM_DEVCVAR_SHIPCONST_INT(ForceReconcileExtraFrames, 0, "j.np.ForceReconcileExtraFrames",	"Roll back this extra number of frames during the next targeted reconcile. Must be positive and reasonable given the buffer sizes.");
	JOLTNETSIM_DEVCVAR_SHIPCONST_INT(SkipReconcile,				0, "j.np.SkipReconcile",				"Skip all reconciles");
	JOLTNETSIM_DEVCVAR_SHIPCONST_INT(PrintReconciles,			0, "j.np.PrintReconciles",			"Print reconciles to log");
}

class IJoltFixedRollbackService
{
public:

	virtual ~IJoltFixedRollbackService() = default;
	virtual int32 QueryRollback(FJoltFixedTickState* TickState) = 0;

	virtual void PreStepRollback(const FJoltNetSimTimeStep& Step, const FJoltServiceTimeStep& ServiceStep, const int32 Offset, const bool bFirstStepInResim) = 0;
	virtual void StepRollback(const FJoltNetSimTimeStep& Step, const FJoltServiceTimeStep& ServiceStep) = 0;
};

template<typename InModelDef>
class TJoltFixedRollbackService : public IJoltFixedRollbackService
{
public:

	using ModelDef = InModelDef;
	using StateTypes = typename ModelDef::StateTypes;
	using SyncAuxType = TJoltSyncAuxPair<StateTypes>;

	static constexpr bool bNeedsTickService = FJoltNetworkPredictionDriver<ModelDef>::HasSimulation();

	TJoltFixedRollbackService(TJoltModelDataStore<ModelDef>* InDataStore)
		: DataStore(InDataStore), InternalTickService(InDataStore) { }

	void RegisterInstance(FJoltNetworkPredictionID ID)
	{
		const int32 ClientRecvIdx = DataStore->ClientRecv.GetIndexChecked(ID);
		JnpResizeAndSetBit(InstanceBitArray, ClientRecvIdx);

		if (bNeedsTickService)
		{
			InternalTickService.RegisterInstance(ID);
		}
	}

	void UnregisterInstance(FJoltNetworkPredictionID ID)
	{
		const int32 ClientRecvIdx = DataStore->ClientRecv.GetIndexChecked(ID);
		InstanceBitArray[ClientRecvIdx] = false;
		
		if (bNeedsTickService)
		{
			InternalTickService.UnregisterInstance(ID);
		}
	}

	int32 QueryRollback(FJoltFixedTickState* TickState) final override
	{
		jnpCheckSlow(TickState);
		JnpClearBitArray(RollbackBitArray);

		// DataStore->ClientRecvBitMask size can change without us knowing so make sure out InstanceBitArray size stays in sync
		JnpResizeBitArray(InstanceBitArray, DataStore->ClientRecvBitMask.Num());

		const int32 Offset = TickState->Offset;
		int32 RollbackFrame = INDEX_NONE;
		for (TConstDualSetBitIterator<FDefaultBitArrayAllocator,FDefaultBitArrayAllocator> BitIt(InstanceBitArray, DataStore->ClientRecvBitMask); BitIt; ++BitIt)
		{
			const int32 ClientRecvIdx = BitIt.GetIndex();
			TJoltClientRecvData<ModelDef>& ClientRecvData = DataStore->ClientRecv.GetByIndexChecked(ClientRecvIdx);
			TJoltInstanceFrameState<ModelDef>& Frames = DataStore->Frames.GetByIndexChecked(ClientRecvData.FramesIdx);

			UE_JNP_TRACE_SIM(ClientRecvData.TraceID);
			
			const int32 LocalFrame = ClientRecvData.ServerFrame - Offset;
			typename TJoltInstanceFrameState<ModelDef>::FFrame& LocalFrameData = Frames.Buffer[LocalFrame];

			bool bDoRollback = false;

			if (NetworkPredictionCVars::ForceReconcile() > 0)
			{
				UE_JNP_TRACE_SHOULD_RECONCILE(ClientRecvData.TraceID);
				bDoRollback = true;
				RollbackFrame = LocalFrame - FMath::Max(0, NetworkPredictionCVars::ForceReconcileExtraFrames());

				if (NetworkPredictionCVars::PrintReconciles())
				{				
					UE_LOG(LogJoltNetworkPrediction, Warning, TEXT("Reconcile activated due to ForceReconcile (to RollbackFrame=%i, including %i extra rollback frames)"), RollbackFrame, -(RollbackFrame-LocalFrame));
				}

				NetworkPredictionCVars::SetForceReconcile(0); // reset
			}
			else if (FJoltNetworkPredictionDriver<ModelDef>::ShouldReconcile( SyncAuxType(LocalFrameData.SyncState, LocalFrameData.AuxState), SyncAuxType(ClientRecvData.SyncState, ClientRecvData.AuxState) ))
			{
				UE_JNP_TRACE_SHOULD_RECONCILE(ClientRecvData.TraceID);
				bDoRollback = true;
				
				if (NetworkPredictionCVars::PrintReconciles())
				{
					UE_LOG(LogJoltNetworkPrediction, Warning, TEXT("Reconcile required due to Sync/Aux mismatch. LocalFrame: %d. Recv Frame: %d. Offset: %d. Idx: %d"), LocalFrame, ClientRecvData.ServerFrame, Offset, LocalFrame % Frames.Buffer.Capacity());

					UE_LOG(LogJoltNetworkPrediction, Warning, TEXT("Received:"));
					FJoltNetworkPredictionDriver<ModelDef>::LogUserStates({ClientRecvData.InputCmd, ClientRecvData.SyncState, ClientRecvData.AuxState });

					UE_LOG(LogJoltNetworkPrediction, Warning, TEXT("Local:"));
					FJoltNetworkPredictionDriver<ModelDef>::LogUserStates({LocalFrameData.InputCmd, LocalFrameData.SyncState, LocalFrameData.AuxState });
				}
			}

			if (bDoRollback && !NetworkPredictionCVars::SkipReconcile())
			{
				RollbackFrame = (RollbackFrame == INDEX_NONE) ? LocalFrame : FMath::Min(RollbackFrame, LocalFrame);
			}
			else
			{
				// Copy received InputCmd to head. This feels a bit out of place here but is ok for now.
				//	-If we rollback, this isn't needed since rollback will copy the cmd (someone else could cause the rollback though, making this redundant)
				//	-Making a second "no rollback happening" pass on all SPs is an option but the branch here seems better, this is the only place we are touching the head frame buffer though...
				if (ClientRecvData.NetRole == ROLE_SimulatedProxy)
				{
					typename TJoltInstanceFrameState<ModelDef>::FFrame& PendingFrameData = Frames.Buffer[TickState->PendingFrame];
					PendingFrameData.InputCmd = ClientRecvData.InputCmd;
				}
			}
			// Regardless if this instance needs to rollback or not, we are marking it in the RollbackBitArray.
			// This could be a ModelDef setting ("Rollback everyone" or "Just who needs it") 
			// Or maybe something more dynamic/spatial ("rollback all instances within this radius", though to do this you may need to consider some ModelDef independent way of doing so)
			JnpResizeAndSetBit(RollbackBitArray, ClientRecvIdx);

			// We've taken care of this instance, reset it for next time
			DataStore->ClientRecvBitMask[ClientRecvIdx] = false;
		}
		
		return RollbackFrame;
	}

	void PreStepRollback(const FJoltNetSimTimeStep& Step, const FJoltServiceTimeStep& ServiceStep, const int32 Offset, const bool bFirstStepInResim)
	{
		if (bFirstStepInResim)
		{
			// Apply corrections for the instances that have corrections on this frame
			ApplyCorrection<false>(ServiceStep.LocalInputFrame, Offset);

			// Everyone must rollback Cue dispatcher and flush
			InternalTickService.BeginRollback(ServiceStep.LocalInputFrame, Step.TotalSimulationTime, Step.Frame);
			
			// Everyone we are managing needs to rollback to this frame, even if they don't have a correction 
			// (this frame or this rollback - they will need to restore their collision data since we are about to retick everyone in step)

			QUICK_SCOPE_CYCLE_COUNTER(JNP_Rollback_RestoreFrame);
			TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::RestoreFrame);

			for (TConstSetBitIterator<> BitIt(InstanceBitArray); BitIt; ++BitIt)
			{
				TJoltClientRecvData<ModelDef>& ClientRecvData = DataStore->ClientRecv.GetByIndexChecked(BitIt.GetIndex());
				TInstanceData<ModelDef>& InstanceData = DataStore->Instances.GetByIndexChecked(ClientRecvData.InstanceIdx);
				TJoltInstanceFrameState<ModelDef>& Frames = DataStore->Frames.GetByIndexChecked(ClientRecvData.FramesIdx);
				typename TJoltInstanceFrameState<ModelDef>::FFrame& LocalFrameData = Frames.Buffer[ServiceStep.LocalInputFrame];

				FJoltNetworkPredictionDriver<ModelDef>::RestoreFrame(InstanceData.Info.Driver, LocalFrameData.SyncState.Get(), LocalFrameData.AuxState.Get());
			}
		}
		else
		{
			ApplyCorrection<true>(ServiceStep.LocalInputFrame, Offset);
		}
	}

	void StepRollback(const FJoltNetSimTimeStep& Step, const FJoltServiceTimeStep& ServiceStep) final override
	{
		if (bNeedsTickService)
		{
			InternalTickService.TickResim(Step, ServiceStep);
		}
	}	

private:

	template<bool FlushCorrection>
	void ApplyCorrection(const int32 LocalInputFrame, const int32 Offset)
	{
		// Insert correction data on the right frame
		for (TConstSetBitIterator<> BitIt(RollbackBitArray); BitIt; ++BitIt)
		{
			const int32 ClientRecvIdx = BitIt.GetIndex();
			TJoltClientRecvData<ModelDef>& ClientRecvData = DataStore->ClientRecv.GetByIndexChecked(ClientRecvIdx);

			const int32 LocalFrame = ClientRecvData.ServerFrame - Offset;
			if (LocalFrame == LocalInputFrame)
			{
				// Time to inject
				TJoltInstanceFrameState<ModelDef>& Frames = DataStore->Frames.GetByIndexChecked(ClientRecvData.FramesIdx);
				typename TJoltInstanceFrameState<ModelDef>::FFrame& LocalFrameData = Frames.Buffer[LocalFrame];
				LocalFrameData.SyncState = ClientRecvData.SyncState;
				LocalFrameData.AuxState = ClientRecvData.AuxState;

				TInstanceData<ModelDef>& InstanceData = DataStore->Instances.GetByIndexChecked(ClientRecvData.InstanceIdx);

				// Copy input cmd if SP
				if (ClientRecvData.NetRole == ROLE_SimulatedProxy)
				{
					LocalFrameData.InputCmd = ClientRecvData.InputCmd;
				}

				RollbackBitArray[ClientRecvIdx] = false;
				UE_JNP_TRACE_ROLLBACK_INJECT(ClientRecvData.TraceID);

				if (FlushCorrection)
				{
					// Push to component/collision scene immediately (we aren't garunteed to tick next, so get our collision right)
					FJoltNetworkPredictionDriver<ModelDef>::RestoreFrame(InstanceData.Info.Driver, LocalFrameData.SyncState.Get(), LocalFrameData.AuxState.Get());
				}
			}
		}
	}
	
	TBitArray<> InstanceBitArray; // Indices into DataStore->ClientRecv that we are managing
	TBitArray<> RollbackBitArray; // Indices into DataStore->ClientRecv that we should rollback

	TJoltModelDataStore<ModelDef>* DataStore;

	TJoltLocalTickService<ModelDef>	InternalTickService;
};

// ------------------------------------------------------------------------------------------------

class IJoltIndependentRollbackService
{
public:

	virtual ~IJoltIndependentRollbackService() = default;
	virtual void Reconcile(const FJoltVariableTickState* TickState) = 0;
};

template<typename InModelDef>
class TJoltIndependentRollbackService : public IJoltIndependentRollbackService
{
public:

	using ModelDef = InModelDef;
	using StateTypes = typename ModelDef::StateTypes;
	using SyncAuxType = TJoltSyncAuxPair<StateTypes>;

	TJoltIndependentRollbackService(TJoltModelDataStore<ModelDef>* InDataStore)
		: DataStore(InDataStore) { }

	void RegisterInstance(FJoltNetworkPredictionID ID)
	{
		const int32 ClientRecvIdx = DataStore->ClientRecv.GetIndexChecked(ID);
		JnpResizeAndSetBit(InstanceBitArray, ClientRecvIdx);

		// Only APs should register for this service. We do not support rollback for independent tick SP actors.
		jnpEnsureSlow(DataStore->Instances.GetByIndexChecked( DataStore->ClientRecv.GetByIndexChecked(ClientRecvIdx).InstanceIdx ).NetRole == ROLE_AutonomousProxy);
	}

	void UnregisterInstance(FJoltNetworkPredictionID ID)
	{
		const int32 ClientRecvIdx = DataStore->ClientRecv.GetIndexChecked(ID);
		InstanceBitArray[ClientRecvIdx] = false;
	}

	void Reconcile(const FJoltVariableTickState* TickState) final override
	{
		// DataStore->ClientRecvBitMask size can change without us knowing so make sure out InstanceBitArray size stays in sync
		JnpResizeBitArray(InstanceBitArray, DataStore->ClientRecvBitMask.Num());

		for (TConstDualSetBitIterator<FDefaultBitArrayAllocator,FDefaultBitArrayAllocator> BitIt(InstanceBitArray, DataStore->ClientRecvBitMask); BitIt; ++BitIt)
		{
			const int32 ClientRecvIdx = BitIt.GetIndex();
			TJoltClientRecvData<ModelDef>& ClientRecvData = DataStore->ClientRecv.GetByIndexChecked(ClientRecvIdx);
			TJoltInstanceFrameState<ModelDef>& Frames = DataStore->Frames.GetByIndexChecked(ClientRecvData.FramesIdx);

			const int32 LocalFrame = ClientRecvData.ServerFrame;
			typename TJoltInstanceFrameState<ModelDef>::FFrame& LocalFrameData = Frames.Buffer[LocalFrame];

			if (FJoltNetworkPredictionDriver<ModelDef>::ShouldReconcile( SyncAuxType(LocalFrameData.SyncState, LocalFrameData.AuxState), SyncAuxType(ClientRecvData.SyncState, ClientRecvData.AuxState) ))
			{
				UE_JNP_TRACE_SHOULD_RECONCILE(ClientRecvData.TraceID);
				if (NetworkPredictionCVars::PrintReconciles())
				{
					UE_LOG(LogJoltNetworkPrediction, Warning, TEXT("ShouldReconcile. Frame: %d."), LocalFrame);

					UE_LOG(LogJoltNetworkPrediction, Warning, TEXT("Received:"));
					FJoltNetworkPredictionDriver<ModelDef>::LogUserStates({ClientRecvData.InputCmd, ClientRecvData.SyncState, ClientRecvData.AuxState });

					UE_LOG(LogJoltNetworkPrediction, Warning, TEXT("Local:"));
					FJoltNetworkPredictionDriver<ModelDef>::LogUserStates({LocalFrameData.InputCmd, LocalFrameData.SyncState, LocalFrameData.AuxState });
				}

				LocalFrameData.SyncState = ClientRecvData.SyncState;
				LocalFrameData.AuxState = ClientRecvData.AuxState;

				TInstanceData<ModelDef>& Instance = DataStore->Instances.GetByIndexChecked(ClientRecvData.InstanceIdx);

				FJoltNetworkPredictionDriver<ModelDef>::RestoreFrame(Instance.Info.Driver, LocalFrameData.SyncState.Get(), LocalFrameData.AuxState.Get());

				// Do rollback
				const int32 EndFrame = TickState->PendingFrame;
				for (int32 Frame = LocalFrame; Frame < EndFrame; ++Frame)
				{
					const int32 InputFrame = Frame;
					const int32 OutputFrame = Frame+1;

					typename TJoltInstanceFrameState<ModelDef>::FFrame& InputFrameData = Frames.Buffer[InputFrame];
					typename TJoltInstanceFrameState<ModelDef>::FFrame& OutputFrameData = Frames.Buffer[OutputFrame];

					const FJoltVariableTickState::FFrame& TickData = TickState->Frames[InputFrame];

					FJoltNetSimTimeStep Step {TickData.DeltaMS, TickData.TotalMS, OutputFrame };

					const int32 EndTimeMS = TickData.TotalMS + TickData.DeltaMS;

					TJoltTickUtil<ModelDef>::DoTick(Instance, InputFrameData, OutputFrameData, Step, EndTimeMS, EJoltSimulationTickContext::Resimulate);

					UE_JNP_TRACE_PUSH_TICK(Step.TotalSimulationTime, Step.StepMS, Step.Frame);
					UE_JNP_TRACE_SIM_TICK(ClientRecvData.TraceID);
				}
			}

			// We've taken care of this instance, reset it for next time
			DataStore->ClientRecvBitMask[ClientRecvIdx] = false;
		}
	}

private:

	TBitArray<> InstanceBitArray; // Indices into DataStore->ClientRecv that we are managing
	TJoltModelDataStore<ModelDef>* DataStore;
};
