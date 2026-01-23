#pragma once

// HEADER_UNIT_SKIP - Not included directly
#include "JoltNetworkPredictionInstanceData.h"
#include "JoltNetworkPredictionTickState.h"
#include "JoltNetworkPredictionTrace.h"
#include "JoltNetworkPredictionUtil.h"


// Common util used by the ticking services. Might make sense to move to FJoltNetworkPredictionDriverBase if needed elsewhere
template<typename ModelDef>
struct TJoltPhysicsUtil
{
	using StateTypes = typename ModelDef::StateTypes;
	using InputType = typename StateTypes::InputType;
	using SyncType = typename StateTypes::SyncType;
	using AuxType = typename StateTypes::AuxType;

	using FrameDataType = typename TJoltInstanceFrameState<ModelDef>::FFrame;

	template<typename SimulationType = typename ModelDef::Simulation>
	static typename TEnableIf<!std::is_same_v<SimulationType, void>>::Type DoTick(TInstanceData<ModelDef>& Instance, FrameDataType& InputFrameData, FrameDataType& OutputFrameData, const FJoltNetSimTimeStep& Step, const int32 CueTimeMS, EJoltSimulationTickContext TickContext)
	{
		// Instance.CueDispatcher->PushContext({Step.Frame, CueTimeMS, TickContext}); TODO:@GreggoryAddison::PotentialIssue || This could cause cues to fire twice in a frame.
		
		// Update cached view before calling tick. If something tries to do an OOB mod to this simulation, it 
		// can only write to the output/pending state. (Input state is frozen now).
		FJoltNetworkPredictionStateView* View = Instance.Info.View;
		View->bTickInProgress = true;
		View->UpdateView(Step.Frame, Step.TotalSimulationTime, &OutputFrameData.InputCmd, &OutputFrameData.SyncState, &OutputFrameData.AuxState);
		// update the interpolation pointer after sim tick, so if queried during it we have the same value as the input.
		View->UpdateInterpolationTime(&OutputFrameData.InterpolationTimeMS);
		// FIXME: aux. Copy it over and make a fake lazy writer for now
		OutputFrameData.AuxState = InputFrameData.AuxState;
		TJoltNetSimLazyWriterFunc<AuxType> LazyAux((void*)&OutputFrameData.AuxState);

		View->LatestInterpTimeMS = InputFrameData.InterpolationTimeMS;

		FJoltNetSimTimeStep TickStep(Step);
		TickStep.InterpolationTimeMS = View->LatestInterpTimeMS;
		TickStep.IsResimulating = TickContext == EJoltSimulationTickContext::Resimulate || TickContext == EJoltSimulationTickContext::ResimExtrapolate;
		Instance.Info.Simulation->PostPhysicsTick( TickStep,
			{ InputFrameData.InputCmd, InputFrameData.SyncState, InputFrameData.AuxState }, // TJoltNetSimInput
			{ OutputFrameData.SyncState, LazyAux, Instance.CueDispatcher.Get() } ); // TJoltNetSimOutput

		
		View->bTickInProgress = false;
		// Instance.CueDispatcher->PopContext(); TODO:@GreggoryAddison::PotentialIssue || This could cause cues to fire twice in a frame.

		// Fixme: should only trace aux if it changed
		UE_JNP_TRACE_USER_STATE_SYNC(ModelDef, OutputFrameData.SyncState.Get());
		UE_JNP_TRACE_USER_STATE_AUX(ModelDef, OutputFrameData.AuxState.Get());
	}

	template<typename SimulationType = typename ModelDef::Simulation>
	static typename TEnableIf<std::is_same_v<SimulationType, void>>::Type DoTick(TInstanceData<ModelDef>& Instance, FrameDataType& InputFrameData, FrameDataType& OutputFrameData, const FJoltNetSimTimeStep& Step, const int32 EndTimeMS, EJoltSimulationTickContext TickContext)
	{
		jnpCheckf(false, TEXT("DoTick called on %s with no Simulation defined"), ModelDef::GetName());
	}
};


// The tick service's role is to tick new simulation frames based on local frame state (fixed or independent/variable)
class IJoltLocalPhysicsService
{
public:

	virtual ~IJoltLocalPhysicsService() = default;
	virtual void Tick(const FJoltNetSimTimeStep& Step, const FJoltServiceTimeStep& ServiceStep) = 0;
};

template<typename InModelDef>
class TJoltLocalPhysicsServiceBase : public IJoltLocalPhysicsService
{
public:

	using ModelDef = InModelDef;
	using StateTypes = typename ModelDef::StateTypes;
	using InputType = typename StateTypes::InputType;
	using SyncType = typename StateTypes::SyncType;
	using AuxType = typename StateTypes::AuxType;

	TJoltLocalPhysicsServiceBase(TJoltModelDataStore<ModelDef>* InDataStore)
		: DataStore(InDataStore) { }

	void RegisterInstance(FJoltNetworkPredictionID ID)
	{
		InstancesToTick.Add((int32)ID, FInstance {ID.GetTraceID(), DataStore->Instances.GetIndex(ID), DataStore->Frames.GetIndex(ID)} );
	}

	void UnregisterInstance(FJoltNetworkPredictionID ID)
	{
		InstancesToTick.Remove((int32)ID);
	}

	void Tick(const FJoltNetSimTimeStep& Step, const FJoltServiceTimeStep& ServiceStep) override
	{
		Tick_Internal<false>(Step, ServiceStep);
	}

	void TickResim(const FJoltNetSimTimeStep& Step, const FJoltServiceTimeStep& ServiceStep)
	{
		Tick_Internal<true>(Step, ServiceStep);
	}

	void BeginRollback(const int32 LocalFrame, const int32 StartTimeMS, const int32 ServerFrame)
	{
		for (auto It : InstancesToTick)
		{
			TInstanceData<ModelDef>& Instance = DataStore->Instances.GetByIndexChecked(It.Value.InstanceIdx);
			UE_JNP_TRACE_SIM(Instance.TraceID);
			// Instance.CueDispatcher->NotifyRollback(ServerFrame); TODO:@GreggoryAddison::PotentialIssue || This could cause cues to fire twice in a frame.
		}
	}

protected:

	template<bool bIsResim>
	void Tick_Internal(const FJoltNetSimTimeStep& Step, const FJoltServiceTimeStep& ServiceStep)
	{
		const int32 InputFrame = ServiceStep.LocalInputFrame;
		const int32 OutputFrame = ServiceStep.LocalOutputFrame;

		const int32 StartTime = Step.TotalSimulationTime;
		const int32 EndTime = ServiceStep.EndTotalSimulationTime;

		for (auto It : InstancesToTick)
		{
			TInstanceData<ModelDef>& Instance = DataStore->Instances.GetByIndexChecked(It.Value.InstanceIdx);
			TJoltInstanceFrameState<ModelDef>& Frames = DataStore->Frames.GetByIndexChecked(It.Value.FrameBufferIdx);

			typename TJoltInstanceFrameState<ModelDef>::FFrame& InputFrameData = Frames.Buffer[InputFrame];
			typename TJoltInstanceFrameState<ModelDef>::FFrame& OutputFrameData = Frames.Buffer[OutputFrame];

			UE_JNP_TRACE_SIM_TICK(It.Value.TraceID);

			// Copy current input into the output frame. This is redundant in the case where we are polling
			// local input but is needed in the other cases. Simpler to just copy it always.
			if (!bIsResim || Instance.NetRole == ROLE_SimulatedProxy)
			{
				OutputFrameData.InputCmd = InputFrameData.InputCmd;
			}
			
			TJoltPhysicsUtil<ModelDef>::DoTick(Instance, InputFrameData, OutputFrameData, Step, EndTime, GetTickContext<bIsResim>(Instance.NetRole));
		}
	}

	template<bool bIsResim>
	EJoltSimulationTickContext GetTickContext(ENetRole NetRole)
	{
		if (bIsResim)
		{
			switch(NetRole)
			{
			case ENetRole::ROLE_AutonomousProxy:
				return EJoltSimulationTickContext::Resimulate;
				break;
			case ENetRole::ROLE_SimulatedProxy:
				return EJoltSimulationTickContext::Resimulate;
				break;
			}
		}
		else
		{
			switch(NetRole)
			{
			case ENetRole::ROLE_Authority:
				return EJoltSimulationTickContext::Authority;
				break;
			case ENetRole::ROLE_AutonomousProxy:
				return EJoltSimulationTickContext::Predict;
				break;
			case ENetRole::ROLE_SimulatedProxy:
				return EJoltSimulationTickContext::Predict; // Fixme: all sim proxies are forward predicted now. We need to look at net LOD here?
				break;
			}
		}

		jnpEnsureMsgf(false, TEXT("Unexpected NetRole %d during regular tick"), NetRole);
		return EJoltSimulationTickContext::None;
	}

	struct FInstance
	{
		int32 TraceID;
		int32 InstanceIdx; // idx into TJoltModelDataStore::Instances
		int32 FrameBufferIdx; // idx into TJoltModelDataStore::Frames
	};

	TSortedMap<int32, FInstance> InstancesToTick;
	TJoltModelDataStore<ModelDef>* DataStore;
	
};

// To allow template specialization
template<typename InModelDef>
class TJoltLocalPhysicsService : public TJoltLocalPhysicsServiceBase<InModelDef>
{
public:
	TJoltLocalPhysicsService(TJoltModelDataStore<InModelDef>* InDataStore) 
		: TJoltLocalPhysicsServiceBase<InModelDef>(InDataStore)
	{

	}
};

// -------------------------------------------------------------


// Service for ticking independent simulations that are remotely controlled.
// E.g, only used by the server for ticking remote clients that are in independent ticking mode,
class IJoltRemoteIndependentPhysicsService
{
public:

	virtual ~IJoltRemoteIndependentPhysicsService() = default;
	virtual void Tick(float DeltaTimeSeconds, const FJoltVariableTickState* VariableTickState) = 0;
};

// Ticking remote clients on the server. 
template<typename InModelDef>
class TJoltRemoteIndependentPhysicsService : public IJoltRemoteIndependentPhysicsService
{
public:
	using ModelDef = InModelDef;

	// These are rough ballparks, maybe should be configurable
	static constexpr int32 MinRemoteClientStepMS = 1;
	static constexpr int32 MaxRemoteClientStepMS = 100;

	static constexpr int32 MaxRemoteClientStepsPerFrame = 6;
	static constexpr int32 MaxRemoteClientTotalMSPerFrame = 200;

	TJoltRemoteIndependentPhysicsService(TJoltModelDataStore<ModelDef>* InDataStore)
		: DataStore(InDataStore) { }

	void RegisterInstance(FJoltNetworkPredictionID ID)
	{
		const int32 ServerRecvIdx = DataStore->ServerRecv_IndependentTick.GetIndexChecked(ID);
		JnpResizeAndSetBit(InstanceBitArray, ServerRecvIdx);
	}

	void UnregisterInstance(FJoltNetworkPredictionID ID)
	{
		const int32 ServerRecvIdx = DataStore->ServerRecv_IndependentTick.GetIndexChecked(ID);
		InstanceBitArray[ServerRecvIdx] = false;
	}

	void Tick(float DeltaTimeSeconds, const FJoltVariableTickState* VariableTickState) final override
	{
		jnpEnsureSlow(VariableTickState->PendingFrame >= 0);

		const float fEngineFrameDeltaTimeMS = DeltaTimeSeconds * 1000.f;
		const int32 CueTimeMS = VariableTickState->Frames[VariableTickState->PendingFrame].TotalMS; // This time stamp is what will get replicated to SP clients for Cues.

		for (TConstSetBitIterator<> BitIt(InstanceBitArray); BitIt; ++BitIt)
		{
			const int32 ServerRecvIdx = BitIt.GetIndex();
			TJoltServerRecvData_Independent<ModelDef>& ServerRecvData = DataStore->ServerRecv_IndependentTick.GetByIndexChecked(ServerRecvIdx);
			ServerRecvData.UnspentTimeMS += fEngineFrameDeltaTimeMS;

			TJoltInstanceFrameState<ModelDef>& Frames = DataStore->Frames.GetByIndexChecked(ServerRecvData.FramesIdx);
			TInstanceData<ModelDef>& InstanceData = DataStore->Instances.GetByIndexChecked(ServerRecvData.InstanceIdx);

			int32 TotalFrames = 0;
			int32 TotalMS = 0;

			const int32 TraceID = ServerRecvData.TraceID;

			while (ServerRecvData.LastConsumedFrame < ServerRecvData.LastRecvFrame)
			{
				const int32 NextFrame = ++ServerRecvData.LastConsumedFrame;
				typename TJoltServerRecvData_Independent<ModelDef>::FFrame& NextRecvData = ServerRecvData.InputBuffer[NextFrame];

				if (NextRecvData.DeltaTimeMS == 0)
				{
					// Dropped cmd, just skip and pretend nothing happened (expect client to be corrected)
					continue;
				}

				const int32 InputCmdMS = FMath::Clamp(NextRecvData.DeltaTimeMS, MinRemoteClientStepMS, MaxRemoteClientTotalMSPerFrame);

				if (InputCmdMS > (int32)ServerRecvData.UnspentTimeMS)
				{
					break;
				}

				const int32 NewTotalMS = TotalMS + InputCmdMS;
				if (NewTotalMS > MaxRemoteClientTotalMSPerFrame)
				{
					break;
				}

				// Tick
				{
					TotalMS = NewTotalMS;
					ServerRecvData.UnspentTimeMS -= (float)InputCmdMS;
					if (FMath::IsNearlyZero(ServerRecvData.UnspentTimeMS))
					{
						ServerRecvData.UnspentTimeMS = 0.f;
					}

					const int32 InputFrame = ServerRecvData.PendingFrame++;
					const int32 OutputFrame = ServerRecvData.PendingFrame;

					Frames.Buffer[InputFrame].InputCmd = NextRecvData.InputCmd;

					typename TJoltInstanceFrameState<ModelDef>::FFrame& InputFrameData = Frames.Buffer[InputFrame];
					typename TJoltInstanceFrameState<ModelDef>::FFrame& OutputFrameData = Frames.Buffer[OutputFrame];

					FJoltNetSimTimeStep Step {InputCmdMS, ServerRecvData.TotalSimTimeMS, OutputFrame };

					ServerRecvData.TotalSimTimeMS += InputCmdMS;

					UE_JNP_TRACE_PUSH_TICK(Step.TotalSimulationTime, Step.StepMS, Step.Frame);
					UE_JNP_TRACE_SIM_TICK(TraceID);

					TJoltPhysicsUtil<ModelDef>::DoTick(InstanceData, InputFrameData, OutputFrameData, Step, CueTimeMS, EJoltSimulationTickContext::Authority);
					
				}

				if (++TotalFrames == MaxRemoteClientStepsPerFrame)
				{
					break;
				}
			}
		}
	}

private:

	TBitArray<> InstanceBitArray; // Indices into DataStore->ServerRecv_IndependentTick that we are managing
	TJoltModelDataStore<ModelDef>* DataStore;
};
