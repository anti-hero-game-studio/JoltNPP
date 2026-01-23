// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltMoverSimulationTypes.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PBDRigidsSolver.h"
#include "GameFramework/GameStateBase.h"
#include "Physics/NetworkPhysicsComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMoverSimulationTypes)

UScriptStruct* FJoltMoverSimulationEventData::GetScriptStruct() const
{
	checkf(false, TEXT("%hs is being called erroneously. This must be overridden in derived types!"), __FUNCTION__);
	return FJoltMoverSimulationEventData::StaticStruct();
}

FJoltScheduledInstantMovementEffect FJoltScheduledInstantMovementEffect::ScheduleEffect(UWorld* World, const FJoltMoverTimeStep& TimeStep, TSharedPtr<FJoltInstantMovementEffect> InstantMovementEffect, float SchedulingDelaySeconds)
{
	FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	Chaos::FPhysicsSolver* Solver = Scene ? Scene->GetSolver() : nullptr;
	bool bIsFixedDt = Solver ? Solver->IsUsingFixedDt() : false;
	if (bIsFixedDt)
	{
		int32 ServerFrame = TimeStep.ServerFrame;
		if (SchedulingDelaySeconds != 0.0f)
		{
			const double DeltaTime = Solver->GetAsyncDeltaTime();
			const int32 DelayFrames = (DeltaTime > UE_SMALL_NUMBER) ? FMath::CeilToInt32(SchedulingDelaySeconds / DeltaTime) : 0;
			ServerFrame += DelayFrames;
		}
		return FJoltScheduledInstantMovementEffect(/* ExecutionServerFrame = */ ServerFrame, /* ExecutionServerTime = */ 0.0, /* bIsFixedDt = */ true, InstantMovementEffect);
	}
	else
	{
		AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
		double ServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : 0.0;
		ServerTime += SchedulingDelaySeconds;
		
		return FJoltScheduledInstantMovementEffect(/* ExecutionServerFrame = */ INDEX_NONE, /* ExecutionServerTime = */ ServerTime, /* bIsFixedDt = */ false, InstantMovementEffect);
	}
}

namespace UE::JoltMover
{
	void FJoltSimulationOutputData::Reset()
	{
		SyncState.Reset();
		LastUsedInputCmd.Reset();
		AdditionalOutputData.Empty();
		Events.Empty();
	}

	void FJoltSimulationOutputData::Interpolate(const FJoltSimulationOutputData& From, const FJoltSimulationOutputData& To, float Alpha, double SimTimeMs)
	{
		SyncState.Interpolate(&From.SyncState, &To.SyncState, Alpha);
		LastUsedInputCmd.Interpolate(&From.LastUsedInputCmd, &To.LastUsedInputCmd, Alpha);
		AdditionalOutputData.Interpolate(From.AdditionalOutputData, To.AdditionalOutputData, Alpha);
	}

	void FJoltSimulationOutputRecord::FData::Reset()
	{
		TimeStep = FJoltMoverTimeStep();
		SimOutputData.Reset();
	}

	void FJoltSimulationOutputRecord::Add(const FJoltMoverTimeStep& InTimeStep, const FJoltSimulationOutputData& InData)
	{
		CurrentIndex = (CurrentIndex + 1) % 2;
		Data[CurrentIndex] = { InTimeStep, InData };

		// When we're resimulating we can be adding data that is at an earlier time to
		// the currently stored data, so fix up if necessary
		if (InTimeStep.bIsResimulating)
		{
			// Remove previous result if it is ahead in time of this latest result
			const uint8 PrevIndex = (CurrentIndex + 1) % 2;
			if (Data[PrevIndex].TimeStep.BaseSimTimeMs > InTimeStep.BaseSimTimeMs)
			{
				Data[PrevIndex] = Data[CurrentIndex];
			}

			// If resimulating find any events stored after the time step and remove
			Events.RemoveAllSwap([&InTimeStep](const TSharedPtr<FJoltMoverSimulationEventData> Event) {
				return !Event.IsValid() || (Event->EventTimeMs >= InTimeStep.BaseSimTimeMs);
				});
		}

		// Transfer events to local Events array
		for (TSharedPtr<FJoltMoverSimulationEventData>& Event : Data[CurrentIndex].SimOutputData.Events)
		{
			Events.Add(Event);
		}
		Data[CurrentIndex].SimOutputData.Events.Empty();
	}

	const FJoltSimulationOutputData& FJoltSimulationOutputRecord::GetLatest() const
	{
		return Data[CurrentIndex].SimOutputData;
	}

	void FJoltSimulationOutputRecord::CreateInterpolatedResult(double AtBaseTimeMs, FJoltMoverTimeStep& OutTimeStep, FJoltSimulationOutputData& OutData)
	{
		const uint8 PrevIndex = (CurrentIndex + 1) % 2;
		const double PrevTimeMs = Data[PrevIndex].TimeStep.BaseSimTimeMs;
		const double CurrTimeMs = Data[CurrentIndex].TimeStep.BaseSimTimeMs;

		if (FMath::IsNearlyEqual(PrevTimeMs, CurrTimeMs) || (AtBaseTimeMs >= CurrTimeMs))
		{
			OutData = Data[CurrentIndex].SimOutputData;
			OutTimeStep = Data[CurrentIndex].TimeStep;
		}
		else if (AtBaseTimeMs <= PrevTimeMs)
		{
			OutData = Data[PrevIndex].SimOutputData;
			OutTimeStep = Data[PrevIndex].TimeStep;
		}
		else
		{
			const float Alpha = FMath::Clamp((AtBaseTimeMs - PrevTimeMs) / (CurrTimeMs - PrevTimeMs), 0.0f, 1.0f);
			OutData.Interpolate(Data[PrevIndex].SimOutputData, Data[CurrentIndex].SimOutputData, Alpha, AtBaseTimeMs);
			OutTimeStep = Data[PrevIndex].TimeStep;
		}

		OutTimeStep.BaseSimTimeMs = AtBaseTimeMs;

		for (TSharedPtr<FJoltMoverSimulationEventData>& Event : Events)
		{
			if (Event->EventTimeMs <= AtBaseTimeMs)
			{
				OutData.Events.Add(MoveTemp(Event));
			}
		}

		Events.RemoveAllSwap([](const TSharedPtr<FJoltMoverSimulationEventData> Event) {
			return !Event.IsValid();
			});
	}

	void FJoltSimulationOutputRecord::Clear()
	{
		CurrentIndex = 1;
		Data[0].Reset();
		Data[1].Reset();
		Events.Empty();
	}

} // namespace UE::JoltMover
