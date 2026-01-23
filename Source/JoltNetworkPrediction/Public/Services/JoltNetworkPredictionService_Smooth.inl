// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JoltNetworkPredictionCVars.h"
#include "JoltNetworkPredictionTickState.h"
#include "JoltNetworkPredictionTrace.h"
#include "Services/JoltNetworkPredictionInstanceData.h"


namespace NetworkPredictionCVars
{
	JOLTNETSIM_DEVCVAR_SHIPCONST_INT(DisableSmoothing, 0, "j.np.Smoothing.Disable", "Disables smoothing and just finalizes using the latest simulation state");
}


// ------------------------------------------------------------------------------
//	FixedTick Smoothing
//
//  This first pass service simply performs interpolation between the most recent fixed tick states
//  and passes the smoothed state to the driver to handle however it chooses. 
//	
//  Future improvements could include smoothing out corrections after a reconcile, and expanding
//  that to smoothing for Independent ticking mode.
// ------------------------------------------------------------------------------
class IJoltFixedSmoothingService
{
public:

	virtual ~IJoltFixedSmoothingService() = default;
	virtual void UpdateSmoothing(const FJoltServiceTimeStep& ServiceStep,const FJoltFixedTickState* TickState) = 0;
	virtual void FinalizeSmoothingFrame(const FJoltFixedTickState* TickState) = 0;
};

template<typename InModelDef>
class TJoltFixedSmoothingService : public IJoltFixedSmoothingService
{
public:

	using ModelDef = InModelDef;
	using StateTypes = typename ModelDef::StateTypes;
	using SyncType = typename StateTypes::SyncType;
	using AuxType = typename StateTypes::AuxType;
	using SyncAuxType = TJoltSyncAuxPair<StateTypes>;
	

	TJoltFixedSmoothingService(TJoltModelDataStore<ModelDef>* InDataStore)
		: DataStore(InDataStore) { }

	void RegisterInstance(FJoltNetworkPredictionID ID)
	{
		const int32 InstanceDataIdx = DataStore->Instances.GetIndex(ID);
		FSparseArrayAllocationInfo AllocInfo = Instances.InsertUninitialized(InstanceDataIdx);
		new (AllocInfo.Pointer) FInstance(ID.GetTraceID(), InstanceDataIdx, DataStore->Frames.GetIndex(ID));
		TInstanceData<ModelDef>& InstanceData = DataStore->Instances.GetByIndexChecked(InstanceDataIdx);
		// Point the PresentationView to our managed state. Note this only has to be done once
		FInstance* InternalInstance = (FInstance*)AllocInfo.Pointer;
		InstanceData.Info.View->UpdatePresentationView(InternalInstance->SyncState, InternalInstance->AuxState);
		InstanceData.Info.View->UpdatePrevPresentationView(InternalInstance->LastSyncState, InternalInstance->LastAuxState);
	}

	void UnregisterInstance(FJoltNetworkPredictionID ID)
	{
		const int32 Idx = DataStore->Instances.GetIndex(ID);
		TInstanceData<ModelDef>& InstanceData = DataStore->Instances.GetByIndexChecked(Idx);
		InstanceData.Info.View->ClearPresentationView();
		Instances.RemoveAt(Idx);
	}
	static void SetSmoothingSpeed(const float& InSmoothingSpeed) 
	{ 
		SmoothingSpeed = InSmoothingSpeed; 
	}

	virtual void UpdateSmoothing(const FJoltServiceTimeStep& ServiceStep, const FJoltFixedTickState* TickState) final override
	{
		const int32 OutputFrame = ServiceStep.LocalOutputFrame;
		const int32 InputFrame = ServiceStep.LocalInputFrame;
		for (auto& It : Instances)
		{
			FInstance& Instance = It;
			
			TJoltInstanceFrameState<ModelDef>& Frames = DataStore->Frames.GetByIndexChecked(Instance.FramesIdx);
			TInstanceData<ModelDef>& InstanceData = DataStore->Instances.GetByIndexChecked(Instance.InstanceIdx);

			typename TJoltInstanceFrameState<ModelDef>::FFrame& InputFrameData = Frames.Buffer[InputFrame];
			typename TJoltInstanceFrameState<ModelDef>::FFrame& OutputFrameData = Frames.Buffer[OutputFrame];
			
			if (NetworkPredictionCVars::DisableSmoothing() != 0 || !Instance.bHasTwoFrames)
			{
				OutputFrameData.SyncState.CopyTo(Instance.SyncState);
				OutputFrameData.AuxState.CopyTo(Instance.AuxState);
				OutputFrameData.SyncState.CopyTo(Instance.LastSyncState);
				OutputFrameData.AuxState.CopyTo(Instance.LastAuxState);
				Instance.bHasTwoFrames = true;
				continue;
			}

			// TODO: could improve this with a double-buffer that alternates, eliminating one copy
			// Set Last Presentation States To Current
			Instance.SyncState.CopyTo(Instance.LastSyncState);
			Instance.AuxState.CopyTo(Instance.LastAuxState);

			// Edited By kai : Add correction Smoothing
			TJoltConditionalState<SyncType> DeltaSynState;
			TJoltConditionalState<AuxType> DeltaAuxState;
			FJoltNetworkPredictionDriver<ModelDef>::GetSmoothingStateDelta(InstanceData.Info.Driver,InputFrameData.SyncState, InputFrameData.AuxState, Instance.LastSyncState, Instance.LastAuxState,
				DeltaSynState, DeltaAuxState);
			float Scale =  FMath::Clamp(1.f - SmoothingSpeed,0.f,1.f);
			FJoltNetworkPredictionDriver<ModelDef>::GetSmoothingStateScaled(InstanceData.Info.Driver,DeltaSynState, DeltaAuxState,Scale, DeltaSynState, DeltaAuxState);

			// Set Sync State To The Pending State
			OutputFrameData.SyncState.CopyTo(Instance.SyncState);
			OutputFrameData.AuxState.CopyTo(Instance.AuxState);

			FJoltNetworkPredictionDriver<ModelDef>::GetSmoothingStateUnion(InstanceData.Info.Driver,OutputFrameData.SyncState, OutputFrameData.AuxState, DeltaSynState, DeltaAuxState, 
				Instance.SyncState, Instance.AuxState);
		}
	}
	
	virtual void FinalizeSmoothingFrame(const FJoltFixedTickState* TickState) final override
	{
		const float RemainingTime = TickState->UnspentTimeMS;
		const float TimeStep = TickState->FixedStepMS;
		const float Alpha = FMath::Clamp(RemainingTime / TimeStep,0.f,1.f);
		
		if (NetworkPredictionCVars::DisableSmoothing() != 0)
		{
			for (auto& It : Instances)
			{
				const FInstance& Instance = It;
				{
					// Push non-smoothed results to driver
					const TInstanceData<ModelDef>& InstanceData = DataStore->Instances.GetByIndexChecked(Instance.InstanceIdx);
					FJoltNetworkPredictionDriver<ModelDef>::FinalizeSmoothingFrame(InstanceData.Info.Driver, Instance.SyncState.Get(), Instance.AuxState.Get());
				}
			}

			return;
		}

		
		for (auto& It : Instances)
		{
			const FInstance& Instance = It;
			
			// Interpolate and push smoothed state to driver
			{
				TJoltConditionalState<SyncType> SmoothedSyncState;
				TJoltConditionalState<AuxType> SmoothedAuxState;

				FJoltNetworkPredictionDriver<ModelDef>::Interpolate(SyncAuxType{Instance.LastSyncState.Get(), Instance.LastAuxState.Get()}, SyncAuxType{Instance.SyncState.Get(), Instance.AuxState.Get()},
					Alpha, SmoothedSyncState, SmoothedAuxState);

				// Push smoothed results to driver
				const TInstanceData<ModelDef>& InstanceData = DataStore->Instances.GetByIndexChecked(Instance.InstanceIdx);
				FJoltNetworkPredictionDriver<ModelDef>::FinalizeSmoothingFrame(InstanceData.Info.Driver, SmoothedSyncState, SmoothedAuxState);
				
			}
		}
	}

private:

	struct FInstance
	{
		FInstance(int32 InTraceID, int32 InInstanceIdx, int32 InFramesIdx)
			: TraceID(InTraceID), InstanceIdx(InInstanceIdx), FramesIdx(InFramesIdx) {}

		int32 TraceID;
		int32 InstanceIdx;
		int32 FramesIdx;
		uint8 bHasTwoFrames : 1 = false;
		
		// Latest states to smooth between. Stored here so that we can maintain FJoltNetworkPredictionStateView to them
		TJoltConditionalState<SyncType> SyncState;
		TJoltConditionalState<AuxType> AuxState;
		TJoltConditionalState<SyncType> LastSyncState;
		TJoltConditionalState<AuxType> LastAuxState;
		
	};
	
	TSparseArray<FInstance> Instances; // Indices are shared with DataStore->ClientRecv
	
	TJoltModelDataStore<ModelDef>* DataStore;

	inline static float SmoothingSpeed = 0.2f; // ToDo @Kai : Bring this back to settings
};

