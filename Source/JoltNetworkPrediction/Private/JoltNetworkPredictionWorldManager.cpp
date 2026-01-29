// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltNetworkPredictionWorldManager.h"
#include "Engine/Engine.h"
#include "PBDRigidsSolver.h"
#include "ChaosSolversModule.h"
#include "JoltNetworkPredictionLagCompensation.h"
#include "JoltNetworkPredictionReplicatedManager.h"
#include "Core/Singletons/JoltPhysicsWorldSubsystem.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Services/JoltNetworkPredictionService_Finalize.inl"
#include "Services/JoltNetworkPredictionService_Input.inl"
#include "Services/JoltNetworkPredictionService_Interpolate.inl"
#include "Services/JoltNetworkPredictionService_Rollback.inl"
#include "Services/JoltNetworkPredictionService_ServerRPC.inl"
#include "TestFramework/Input/Keyboard.h"


JOLTNETSIM_DEVCVAR_SHIPCONST_INT(ToggleLagCompensationDebug, 0, "j.np.DrawLagCompensationDebug", "Toggle Lag Compensation Debug , 1 : Enabled , 0 : Disabled");

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltNetworkPredictionWorldManager)


UJoltNetworkPredictionWorldManager* UJoltNetworkPredictionWorldManager::ActiveInstance=nullptr;

// -----------------------------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------------------------

UJoltNetworkPredictionWorldManager::UJoltNetworkPredictionWorldManager()
{

}

void UJoltNetworkPredictionWorldManager::Initialize(FSubsystemCollectionBase& Collection)
{
	UWorld* World = GetWorld();
	check(World);
	if (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game)
	{
		PreTickDispatchHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &UJoltNetworkPredictionWorldManager::OnWorldPreTick);
		PostTickDispatchHandle = World->OnPostTickDispatch().AddUObject(this, &UJoltNetworkPredictionWorldManager::ReconcileSimulationsPostNetworkUpdate);
		PreWorldActorTickHandle = FWorldDelegates::OnWorldPreActorTick.AddUObject(this, &UJoltNetworkPredictionWorldManager::BeginNewSimulationFrame);
		SyncNetworkPredictionSettings(GetDefault<UJoltNetworkPredictionSettingsObject>());
	}
}

void UJoltNetworkPredictionWorldManager::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (PreTickDispatchHandle.IsValid())
		{
			FWorldDelegates::OnWorldTickStart.Remove(PreTickDispatchHandle);
		}
		if (PostTickDispatchHandle.IsValid())
		{
			World->OnPostTickDispatch().Remove(PostTickDispatchHandle);
		}
		if (PreWorldActorTickHandle.IsValid())
		{
			FWorldDelegates::OnWorldPreActorTick.Remove(PreWorldActorTickHandle);
		}
		EnableLocalPlayerControllersTicking();
	}
}

void UJoltNetworkPredictionWorldManager::SyncNetworkPredictionSettings(const UJoltNetworkPredictionSettingsObject* SettingsObj)
{
	this->Settings = SettingsObj->Settings;
}

float UJoltNetworkPredictionWorldManager::GetCurrentLagCompensationTimeMS(const AActor* Actor) const
{
	if (!Actor || !Actor->GetWorld())
	{
		return 0.f;
	}
	// Authority Always Uses Current Sim Time. Pending Frame is For Next Sim Tick So We Remove A Tick in Fixed Tick.
	// stand alone gets interp time so it will always early out when trying to rewind actors and return latest
	// when queering the history
	if (Actor->GetLocalRole() == ROLE_Authority || UKismetSystemLibrary::IsStandalone(Actor)) 
	{
		if (Settings.PreferredTickingPolicy == EJoltNetworkPredictionTickingPolicy::Fixed)
		{
			return FixedTickState.GetTotalSimTimeMS() - FixedTickState.FixedStepMS;
		}
		return VariableTickState.GetNextTimeStep(VariableTickState.Frames[VariableTickState.PendingFrame]).TotalSimulationTime;
	}
	// SP and AP both use the interpolation time 
	if (Settings.PreferredTickingPolicy == EJoltNetworkPredictionTickingPolicy::Fixed) 
	{
		return FixedTickState.Interpolation.InterpolatedTimeMS;
	}
	return VariableTickState.Interpolation.fTimeMS;
}

FNpLagCompensationData UJoltNetworkPredictionWorldManager::GetActorDefaultStateAtTime(AActor* RequestingActor,
	AActor* TargetActor, const float TargetSimTimeMS)
{
	if (!RequestingActor || !TargetActor)
	{
		return FNpLagCompensationData();
	}
	FNpLagCompensationData ReturnData;
	TSharedPtr<FNpLagCompensationData> FoundData = GetActorStateAtTime(RequestingActor,TargetActor,TargetSimTimeMS);
	if (FoundData)
	{
		ReturnData = *FoundData;
		return ReturnData;
	}
	ReturnData.Location = TargetActor->GetActorTransform().GetLocation();
	ReturnData.Rotation = TargetActor->GetActorTransform().GetRotation();
	ReturnData.SimTimeMs = GetCurrentLagCompensationTimeMS(RequestingActor);
	ReturnData.CanRewindFurther = false;
	//ReturnData.CollisionExtent .. Todo Fix Me,
	return ReturnData;
}


void UJoltNetworkPredictionWorldManager::OnInputReceived(const int32& Frame,const float& InterpolationTime,const TArray<FJoltSimulationReplicatedInput>& Inputs
                                                     , UJoltNetworkPredictionPlayerControllerComponent* RPCHandler)
{
	// make sure handler is in the input handlers array.
	if (!RPCHandler)
	{
		return;
	}
	RPCHandlers.AddUnique(RPCHandler);
	
	const bool ShouldEatCmd = RPCHandler->LastReceivedFrame >= Frame;
	if (!ShouldEatCmd)
	{
		for (TUniquePtr<IJoltInputService>& Ptr : Services.FixedInputRemote.Array)
		{
			Ptr->OnFixedInputReceived(Frame,InterpolationTime,Inputs,RPCHandler,&FixedTickState);
		}
		RPCHandler->LastReceivedFrame = Frame;
	}
	//ToDo : Add for variable tick rate too.
}

void UJoltNetworkPredictionWorldManager::OnReceivedAckedData(const FJoltSerializedAckedFrames& AckedFrames,
	UJoltNetworkPredictionPlayerControllerComponent* RPCHandler)
{
	check(AckedFrames.IDs.Num() == AckedFrames.AckedFrames.Num())
	if (!RPCHandler || !RPCHandler->GetNetConnection())
	{
		return;
	}
	FJoltAckedFrames& ClientAckedFrames = FixedTickState.ServerAckedFrames.ConnectionsAckedFrames.FindOrAdd(RPCHandler->GetNetConnection());
	for (int32 i = 0 ; i < AckedFrames.IDs.Num() ; ++i)
	{
		uint32 ID = AckedFrames.IDs[i];
		uint32 AckedFrame = AckedFrames.AckedFrames[i];
		uint32& AddedFrame = ClientAckedFrames.IDsToAckedFrames.FindOrAdd(ID);
		AddedFrame = AckedFrame;
	}
}

void UJoltNetworkPredictionWorldManager::RegisterRPCHandler(UJoltNetworkPredictionPlayerControllerComponent* RPCHandler)
{
	RPCHandlers.AddUnique(RPCHandler);
}

void UJoltNetworkPredictionWorldManager::UnRegisterRPCHandler(UJoltNetworkPredictionPlayerControllerComponent* RPCHandler)
{
	RPCHandlers.Remove(RPCHandler);
}

void UJoltNetworkPredictionWorldManager::SetTimeDilation(const FJoltSimTimeDilation& TimeDilation)
{
	FixedTickState.TimeDilationState.TimeDilation = TimeDilation.GetTimeDilation();
}

// -----------------------------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------------------------

void UJoltNetworkPredictionWorldManager::OnWorldPreTick(UWorld* InWorld, ELevelTick InLevelTick, float InDeltaSeconds)
{
	if (InWorld != GetWorld())
	{
		return;
	}

	UE_JNP_TRACE_WORLD_FRAME_START(InWorld->GetGameInstance(), InDeltaSeconds);

	OnWorldPreTick_Internal(InDeltaSeconds, Settings.FixedTickFrameRate);

	// Instantiate replicated manager on server
	if (!ReplicatedManager && InWorld->GetNetMode() != NM_Client)
	{
		UClass* ReplicatedManagerClass = GetDefault<UJoltNetworkPredictionSettingsObject>()->Settings.ReplicatedManagerClassOverride.Get();
		ReplicatedManager = ReplicatedManagerClass ? InWorld->SpawnActor<AJoltNetworkPredictionReplicatedManager>(ReplicatedManagerClass) : InWorld->SpawnActor<AJoltNetworkPredictionReplicatedManager>();
	}
}

void UJoltNetworkPredictionWorldManager::OnWorldPreTick_Internal(float InDeltaSeconds, float InFixedFrameRate)
{
	// Update fixed tick rate, this can be changed via editor settings
	FixedTickState.FixedStepRealTimeMS = (1.f /  InFixedFrameRate) * 1000.f;
	FixedTickState.FixedStepMS = (int32)FixedTickState.FixedStepRealTimeMS;

	// *** Modified By Kai Smoothing Support *** //

	// Time Dilation : happens Only On Locally Controlled Client (Not On Listen Server Local Player)
	// This Is Calculated by the server based on its input buffer count and sent to local player inside TFixedTickReplicator_AP NetSend().
	//NOTE : Time Dilation is implemented the exact same way the physics simulation does time dilation. with bit more network bandwidth optimization
	FixedTickState.TimeDilationState.FixedStepDilatedTimeMS =  FixedTickState.FixedStepRealTimeMS;
	if (NetworkPredictionCVars::DisableTimeDilation() == 0)
	{
		FixedTickState.TimeDilationState.FixedStepDilatedTimeMS =  FixedTickState.FixedStepRealTimeMS * FixedTickState.TimeDilationState.TimeDilation;
	}
	// *** END Modified By Kai Smoothing Support *** //

	ActiveInstance = this;
}

void UJoltNetworkPredictionWorldManager::ReconcileSimulationsPostNetworkUpdate()
{
	UWorld* World = GetWorld();
	if (World->GetNetMode() != NM_Client)
	{
		for (auto It = FixedTickState.ServerAckedFrames.ConnectionsAckedFrames.CreateIterator(); It; ++It)
		{
			if (!IsValid(It.Key()) || It.Key()->GetConnectionState() == USOCK_Closed)
			{
				It.RemoveCurrent();
			}
		}
		return;
	}

	EnableLocalPlayerControllersTicking();

	ReconcileSimulationsPostNetworkUpdate_Internal();
}

void UJoltNetworkPredictionWorldManager::ReconcileSimulationsPostNetworkUpdate_Internal()
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_JNP_RECONCILE);
	TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::Reconcile);

	ActiveInstance = this;
	bLockServices = true;

	// Trace Local->Server offset. We need to trace this so that we can flag reconciles that happened
	// due to this (usually caused by server being starved for input)
	const bool OffsetChanged = (FixedTickState.LastOffset != FixedTickState.Offset);
	UE_JNP_TRACE_FIXED_TICK_OFFSET(FixedTickState.Offset, OffsetChanged);
	if (OffsetChanged)
	{
		UE_LOG(LogJoltNetworkPrediction,Warning,TEXT("ReconcileFromInputOffset Old Offset %d, New Offset %d"),FixedTickState.LastOffset,FixedTickState.Offset)
	}
	FixedTickState.LastOffset = FixedTickState.Offset;

	// -------------------------------------------------------------------------
	//	Non-rollback reconcile services
	// -------------------------------------------------------------------------
	
	// Don't reconcile FixedTick interpolates until we've started interpolation
	// This makes the service's implementation easier if it can rely on a known
	// ToFrame while reconciling network updates
	if (FixedTickState.Interpolation.ToFrame != INDEX_NONE)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::ReconcileFixedInterpolate);
		for (TUniquePtr<IJoltFixedInterpolateService>& Ptr : Services.FixedInterpolate.Array)
		{
			Ptr->Reconcile(&FixedTickState);
		}
	}

	for (TUniquePtr<IJoltIndependentInterpolateService>& Ptr : Services.IndependentInterpolate.Array)
	{
		Ptr->Reconcile(&VariableTickState);
	}
	
	// Does anyone need to rollback?
	TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::ReconcileQueryRollback);
	int32 RollbackFrame = INDEX_NONE;
	for (TUniquePtr<IJoltFixedRollbackService>& Ptr : Services.FixedRollback.Array)
	{
		const int32 ReqFrame = Ptr->QueryRollback(&FixedTickState);
		if (ReqFrame != INDEX_NONE)
		{
			RollbackFrame = (RollbackFrame == INDEX_NONE ? ReqFrame : FMath::Min(RollbackFrame, ReqFrame));
		}
	}

	if (RollbackFrame != INDEX_NONE)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_JNP_ROLLBACK);
		TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::Rollback);

		if (RollbackFrame < FixedTickState.PendingFrame)
		{
			// Common case: rollback to previously ticked frame and resimulate			

			const int32 EndFrame = FixedTickState.PendingFrame;
			const int32 NumFrames = EndFrame - RollbackFrame;
			jnpEnsureSlow(NumFrames > 0);
			
			
			bool bFirstStep = true;
			UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>();

			// Do rollback as necessary
			for (int32 Frame=RollbackFrame; Frame < EndFrame; ++Frame)
			{
				
				/*const int32 ServerInputFrame = Frame + FixedTickState.Offset;
				UE_LOG(LogJoltNetworkPrediction, Warning, TEXT(" [F]Previous Pending Frame = %d"), FixedTickState.PendingFrame);
				UE_LOG(LogJoltNetworkPrediction, Warning, TEXT(" [F]Roll Back Frame = %d"), Frame);
				UE_LOG(LogJoltNetworkPrediction, Warning, TEXT(" [F]Next Time Stamp Frame = %d"), FixedTickState.GetNextTimeStep().Frame);
				UE_LOG(LogJoltNetworkPrediction, Warning, TEXT(" [F]Server Frame = %d"), ServerInputFrame);*/
				FixedTickState.PendingFrame = Frame;
				FJoltNetSimTimeStep Step = FixedTickState.GetNextTimeStep();
				FJoltServiceTimeStep ServiceStep = FixedTickState.GetNextServiceTimeStep();
			
				
				UE_JNP_TRACE_PUSH_TICK(Step.TotalSimulationTime, FixedTickState.FixedStepMS, Step.Frame);

				// Everyone must apply corrections and flush as necessary before anyone runs the next sim tick
				// bFirstStep will indicate that even if they don't have a correction, they need to rollback their historic state
				for (TUniquePtr<IJoltFixedRollbackService>& Ptr : Services.FixedRollback.Array)
				{
					//UE_LOG(LogJoltNetworkPrediction, Warning, TEXT("Roll Back : Mover Pre-StepRollBack : Frame = %d"), Frame);
					Ptr->PreStepRollback(Step, ServiceStep, FixedTickState.Offset, bFirstStep);
				}

				if (bFirstStep && Subsystem)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::RestoreStateForFrame);
					Subsystem->RestoreStateForFrame(Frame);
				}
				
				
				for (TUniquePtr<IJoltFixedPhysicsRollbackService>& Ptr : Services.FixedPhysicsRollback.Array)
				{
					//UE_LOG(LogJoltNetworkPrediction, Warning, TEXT("Roll Back : Physics Pre-StepRollBack : Frame = %d"), Frame);
					Ptr->PreStepRollback(Step, ServiceStep, FixedTickState.Offset, bFirstStep);
				}
				// Run Sim ticks
				for (TUniquePtr<IJoltFixedRollbackService>& Ptr : Services.FixedRollback.Array)
				{
					//UE_LOG(LogJoltNetworkPrediction, Warning, TEXT("Roll Back : Mover StepRollBack : Frame = %d"), Frame);
					Ptr->StepRollback(Step, ServiceStep);
				}
				
				//TODO:@GreggoryAddison::CodeCompletion || I will have to manually add decay on inputs that I don't own
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::JoltPhysicsTick_Rollback);
					if (Subsystem)
					{
						//UE_LOG(LogJoltNetworkPrediction, Warning, TEXT("Roll Back : Physics Step : Frame = %d"), Frame);
						const double FixedTimeStep = Step.StepMS * 0.001;
						Subsystem->StepPhysics(FixedTimeStep);
						Subsystem->SaveStateForFrame(Frame);
					}
				}
				
				// TODO:@GreggoryAddison::CodeModularity || This will need to be wrapped in a boolean in order to support a Kinematic body using jolt.
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::PostJoltPhysicsTick_Rollback);
					for (TUniquePtr<IJoltLocalPhysicsService>& Ptr : Services.FixedPhysics.Array)
					{
						//UE_LOG(LogJoltNetworkPrediction, Warning, TEXT("[MSL] Roll Back : Post Physics Step : Frame = %d"), Frame);
						Ptr->Tick(Step, ServiceStep);
					}
				}
				
				bFirstStep = false;
			}
			FixedTickState.PendingFrame = EndFrame;
		}
		else if (RollbackFrame == FixedTickState.PendingFrame)
		{
			// Correction is at the PendingFrame (frame we haven't ticked yet)
			// For now, just do nothing. We are either in a really bad state of PL or are just starting up
			// As our input frames make the round trip, we'll get some slack and be doing corrections in the above code block
			// (Setting the correction data now most likely is still wrong and not worth the iteration time)
			
			UE_LOG(LogJoltNetworkPrediction, Log, TEXT("RollbackFrame %d EQUAL PendingFrame %d... Offset: %d"), RollbackFrame, FixedTickState.PendingFrame, FixedTickState.Offset);
		}
		else if (RollbackFrame > FixedTickState.PendingFrame)
		{
			// Most likely we haven't had a confirmed frame yet so our local frame -> server mapping hasn't been set yet
			UE_LOG(LogJoltNetworkPrediction, Log, TEXT("RollbackFrame %d AHEAD of PendingFrame %d... Offset: %d"), RollbackFrame, FixedTickState.PendingFrame, FixedTickState.Offset);
		}
	}

	// -------------------------------------------------------------------------
	//	Independent Tick rollback
	// -------------------------------------------------------------------------
	for (TUniquePtr<IJoltIndependentRollbackService>& Ptr : Services.IndependentRollback.Array)
	{
		Ptr->Reconcile(&VariableTickState);
	}

	bLockServices = false;
	DeferredServiceConfigDelegate.Broadcast(this);
	DeferredServiceConfigDelegate.Clear();
}

void UJoltNetworkPredictionWorldManager::BeginNewSimulationFrame(UWorld* InWorld, ELevelTick InLevelTick, float DeltaTimeSeconds)
{
	if (InWorld != GetWorld() || !InWorld->HasBegunPlay())
	{
		return;
	}
	TickLocalPlayerControllers(InLevelTick, DeltaTimeSeconds);
	BeginNewSimulationFrame_Internal(DeltaTimeSeconds);
}

void UJoltNetworkPredictionWorldManager::BeginNewSimulationFrame_Internal(float DeltaTimeSeconds)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_JNP_TICK);
	TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::Tick);
	
	ActiveInstance = this;
	bLockServices = true;

	const float fEngineFrameDeltaTimeMS = DeltaTimeSeconds * 1000.f;

	// -------------------------------------------------------------------------
	//	Fixed Tick
	// -------------------------------------------------------------------------
	if (Services.FixedTick.Array.Num() > 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_JNP_TICK_FIXED);
		TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::FixedTick);

		FixedTickState.UnspentTimeMS += fEngineFrameDeltaTimeMS;

		while ((FixedTickState.UnspentTimeMS + KINDA_SMALL_NUMBER) >= FixedTickState.TimeDilationState.FixedStepDilatedTimeMS)
		{
			//ToDo : Add SubStep Count here and get out if reaches max. don't allow 1 bad frame to freeze game
			
			FixedTickState.UnspentTimeMS -= FixedTickState.TimeDilationState.FixedStepDilatedTimeMS;
			if (FMath::IsNearlyZero(FixedTickState.UnspentTimeMS))
			{
				FixedTickState.UnspentTimeMS = 0.f;
			}

			FJoltNetSimTimeStep Step = FixedTickState.GetNextTimeStep();
			FJoltServiceTimeStep ServiceStep = FixedTickState.GetNextServiceTimeStep();

			
			const int32 ServerInputFrame = FixedTickState.PendingFrame + FixedTickState.Offset;
			// server that produces input doesn't interpolate, all entities tick for him so provide sim time as interp time
			const bool bIsServer = GetWorld()->GetNetMode() == NM_ListenServer || GetWorld()->GetNetMode() == NM_DedicatedServer;
			const float InterpTimeMs = bIsServer ? FixedTickState.GetTotalSimTimeMS() : FixedTickState.Interpolation.InterpolatedTimeMS;
			UE_JNP_TRACE_PUSH_INPUT_FRAME(ServerInputFrame);
			if (Services.FixedInputRemote.Array.Num() > 0)
			{
				for (UJoltNetworkPredictionPlayerControllerComponent*& InputHandler : RPCHandlers)
				{
					//ToDo : This Is Hard Coded 32 Needs to fix it
					if (IsValid(InputHandler))
					{
						InputHandler->AdvanceLastConsumedFrame(32);
					}
				}

				for (TUniquePtr<IJoltInputService>& Ptr : Services.FixedInputRemote.Array)
				{
					Ptr->ProduceInput(FixedTickState.FixedStepMS,InterpTimeMs);
				}
			}
			for (TUniquePtr<IJoltInputService>& Ptr : Services.FixedInputLocal.Array)
			{
				Ptr->ProduceInput(FixedTickState.FixedStepMS,InterpTimeMs);
			}

			UE_JNP_TRACE_PUSH_TICK(Step.TotalSimulationTime, FixedTickState.FixedStepMS, Step.Frame);
			
			// Should we increment PendingFrame before or after the tick?
			// Before: sims that are spawned during Tick (of other sims) will not be ticked this frame.
			// So we want their seed state/cached pending frame to be set to the next pending frame, not this one.
			FixedTickState.PendingFrame++;

			for (TUniquePtr<IJoltLocalTickService>& Ptr : Services.FixedTick.Array)
			{
				Ptr->Tick(Step, ServiceStep);
			}
			
			{
		
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::JoltPhysicsTick);
					if (UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>())
					{
						//UE_LOG(LogJoltNetworkPrediction, Warning, TEXT("[MSL] Time | DeltaTime = %f | Frame = %d"), DeltaTimeSeconds, Step.Frame);
						const double FixedTimeStep = Step.StepMS * 0.001;
						Subsystem->StepPhysics(FixedTimeStep);
						Subsystem->SaveStateForFrame(Step.Frame);
					}
				
				}
			
				// TODO:@GreggoryAddison::CodeModularity || This will need to be wrapped in a boolean in order to support a Kinematic body using jolt.
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::PostJoltPhysicsTick);
					for (TUniquePtr<IJoltLocalPhysicsService>& Ptr : Services.FixedPhysics.Array)
					{
						//UE_LOG(LogJoltNetworkPrediction, Warning, TEXT("[MSL] Non Rollback Frame = %d"), Step.Frame);
						Ptr->Tick(Step, ServiceStep);
					}
				}
			}
			
			
			if (Settings.bEnableFixedTickSmoothing)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::FixedSmoothing);
				for (TUniquePtr<IJoltFixedSmoothingService>& Ptr : Services.FixedSmoothing.Array)
				{
					Ptr->UpdateSmoothing(ServiceStep, &FixedTickState);
				}
			}

			for (UJoltNetworkPredictionLagCompensation* RegisteredComp : RegisteredLagCompComponents)
			{
				if (RegisteredComp->HasSimulation())
				{
					RegisteredComp->CaptureStateAndAddToHistory(ServiceStep.EndTotalSimulationTime);
				}
			}
			
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::CallServerRPC);
				// send multiple RPC for each input command.
				// since we are sending inputs for all simulation together this is better than sending all in 1 RPC
				
				if (Services.FixedServerRPC.Array.Num() > 0)
				{
					const int32 NumInputToSend = FMath::Max(Settings.FixedTickInputSendCount,1);
					const int32 StartFrame = FMath::Max(FixedTickState.PendingFrame - NumInputToSend ,0);
					// PendingFrame doesn't have an input written yet, so don't send its contents
					for (int32 i = StartFrame; i < FixedTickState.PendingFrame; i++)
					{
						for (TUniquePtr<IJoltFixedServerRPCService>& Ptr : Services.FixedServerRPC.Array)
						{
							Ptr->AddInputToHandler(i);
						}
						for (UJoltNetworkPredictionPlayerControllerComponent*& InputHandler : RPCHandlers)
						{
							InputHandler->SendServerRpc(i);
							// we only need to send acked frames once
							if (FixedTickState.LocalAckedFrames.IDsToAckedFrames.Num() > 0)
							{
								InputHandler->SendAckedFrames(FJoltSerializedAckedFrames(FixedTickState.LocalAckedFrames));
							}
						}
						FixedTickState.LocalAckedFrames.IDsToAckedFrames.Reset();
					}
				}
			}
			
			// TODO:@GreggoryAddison::CodeModularity || This is mean to be behind a bool for the cases where you are not using a physics sim. In the default case this will always be true.
			FixedTickState.UnspentTimeMS = 0.f;
			break;
		}
		
	}

	// -------------------------------------------------------------------------
	//	Local Independent Tick
	// -------------------------------------------------------------------------
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::IndependentTick);

		// Update VariableTickState	
		constexpr int32 MinStepMS = 1;
		constexpr int32 MaxStepMS = 100;

		VariableTickState.UnspentTimeMS += fEngineFrameDeltaTimeMS;
		float fDeltaMS = FMath::FloorToFloat(VariableTickState.UnspentTimeMS);
		VariableTickState.UnspentTimeMS -= fDeltaMS;

		const int32 DeltaSimMS = FMath::Clamp((int32)fDeltaMS, MinStepMS, MaxStepMS);

		FJoltVariableTickState::FFrame& PendingFrameData = VariableTickState.Frames[VariableTickState.PendingFrame];
		PendingFrameData.DeltaMS = DeltaSimMS;

		// Input
		UE_JNP_TRACE_PUSH_INPUT_FRAME(VariableTickState.PendingFrame);
		for (TUniquePtr<IJoltInputService>& Ptr : Services.IndependentLocalInput.Array)
		{
			Ptr->ProduceInput(DeltaSimMS,VariableTickState.Interpolation.fTimeMS);
		}

		// -------------------------------------------------------------------------
		// LocalTick
		// -------------------------------------------------------------------------

		FJoltNetSimTimeStep Step = VariableTickState.GetNextTimeStep(PendingFrameData);
		FJoltServiceTimeStep ServiceStep = VariableTickState.GetNextServiceTimeStep(PendingFrameData);
		UE_JNP_TRACE_PUSH_TICK(Step.TotalSimulationTime, Step.StepMS, Step.Frame);

		for (TUniquePtr<IJoltLocalTickService>& Ptr : Services.IndependentLocalTick.Array)
		{
			Ptr->Tick(Step, ServiceStep);
		}	
		
		for (TUniquePtr<IJoltLocalPhysicsService>& Ptr : Services.IndependentLocalPhysics.Array)
		{
			Ptr->Tick(Step, ServiceStep);
		}	

		// -------------------------------------------------------------------------
		//	Remote Independent Tick
		// -------------------------------------------------------------------------
		for (TUniquePtr<IJoltRemoteIndependentTickService>& Ptr : Services.IndependentRemoteTick.Array)
		{
			Ptr->Tick(DeltaTimeSeconds, &VariableTickState);
		}
		
		for (TUniquePtr<IJoltRemoteIndependentPhysicsService>& Ptr : Services.IndependentRemotePhysics.Array)
		{
			Ptr->Tick(DeltaTimeSeconds, &VariableTickState);
		}

		// Increment local PendingFrame and set (next) pending frame's TotalMS
		const int32 EndTotalSimTimeMS = PendingFrameData.TotalMS + PendingFrameData.DeltaMS;
		VariableTickState.Frames[++VariableTickState.PendingFrame].TotalMS = EndTotalSimTimeMS;
	}
	
	// -------------------------------------------------------------------------
	// Interpolation
	// -------------------------------------------------------------------------
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::Interpolation);

		if (Services.FixedInterpolate.Array.Num() > 0)
		{
			const int32 LatestRecvFrame = FMath::Max(FixedTickState.Interpolation.LatestRecvFrameAP, FixedTickState.Interpolation.LatestRecvFrameSP);
			if (LatestRecvFrame != INDEX_NONE)
			{
				// We want 100ms of buffered time. As long a actors replicate at >= 10hz, this is should be good
				// Its better to keep this simple with a single time rather than trying to coordinate lowest amount of buffered time
				// between all the registered instances in the different ModelDefs
				const int32 DesiredBufferedMS = Settings.FixedTickInterpolationBufferedMS;
				float InterpolateRate = 1.f;
				if (FixedTickState.Interpolation.ToFrame == INDEX_NONE)
				{
					const int32 NumBufferedFrames = LatestRecvFrame;
					const int32 BufferedMS = NumBufferedFrames * FixedTickState.FixedStepMS;

					//UE_LOG(LogTemp, Warning, TEXT("BufferedMS: %d Frames: %d (No ToFrame)"), BufferedMS, NumBufferedFrames);

					if (BufferedMS < DesiredBufferedMS)
					{
						// Not enough time buffered yet to start interpolating
						InterpolateRate = 0.f;
					}
					else
					{
						// Begin interpolation
						const int32 DesiredNumBufferedFrames = (DesiredBufferedMS / FixedTickState.FixedStepMS);
						FixedTickState.Interpolation.ToFrame = LatestRecvFrame - DesiredNumBufferedFrames;

						FixedTickState.Interpolation.PCT = 0.f;
						FixedTickState.Interpolation.AccumulatedTimeMS = 0.f;

						// We need to force a reconcile here since we supress the call until interpolation starts
						for (TUniquePtr<IJoltFixedInterpolateService>& Ptr : Services.FixedInterpolate.Array)
						{
							Ptr->Reconcile(&FixedTickState);
						}
					}
				}
				else
				{
					const int32 NumBufferedFrames = LatestRecvFrame - FixedTickState.Interpolation.ToFrame;
					const int32 BufferedMS = NumBufferedFrames * FixedTickState.FixedStepMS;

					//UE_LOG(LogTemp, Warning, TEXT("BufferedMS: %d Frames: %d"), BufferedMS, NumBufferedFrames);

					if (NumBufferedFrames <= 0)
					{
						InterpolateRate = 0.f;
					}
				}

				int32 AdvanceFrames = 0;
				if (InterpolateRate > 0.f)
				{
					const float fScaledDeltaTimeMS = (InterpolateRate * fEngineFrameDeltaTimeMS);

					// If The Buffer Has more BufferedMS Than We Want , Interpolate to further frame by the amount of excess buffered MS
					//Eg. if we have 80ms buffered , in a 20ms tick simulation, and we want 60ms of buffered time, add an extra frame to target interpolation.
					// nothing we can do if we have too little time buffered. we will copy frame in the service.
					FixedTickState.Interpolation.AccumulatedTimeMS += fScaledDeltaTimeMS;
					AdvanceFrames = (int32)FixedTickState.Interpolation.AccumulatedTimeMS / FixedTickState.FixedStepRealTimeMS;
					
					const int32 NumBufferedFrames = LatestRecvFrame - (FixedTickState.Interpolation.ToFrame + AdvanceFrames);
					// Buffered time After Advancing.
					const int32 BufferedMS = ((LatestRecvFrame - AdvanceFrames) * FixedTickState.FixedStepMS) - FixedTickState.Interpolation.InterpolatedTimeMS;
					// Time Over the desired Buffered time after advancing
					const float ExcessTime = FMath::Max(0.f,BufferedMS - Settings.FixedTickInterpolationBufferedMS);
					// extra frames we should advance to bring buffered time lower , if it gets too big
					int32 ExtraFrames = 0;
					if(ExcessTime > FixedTickState.FixedStepRealTimeMS)
					{
						ExtraFrames = FMath::CeilToInt32(FMath::Max(ExcessTime / FixedTickState.FixedStepMS,0));
					}
					
					if (AdvanceFrames > 0)
					{
						FixedTickState.Interpolation.ToFrame += AdvanceFrames;
						// Add Extra Advance Frames to target interpolation frame
						FixedTickState.Interpolation.ToFrame += ExtraFrames;
						//Make Sure ToFrame doesn't go above latest received frame
						FixedTickState.Interpolation.ToFrame = FMath::Min(FixedTickState.Interpolation.ToFrame,LatestRecvFrame);
						FixedTickState.Interpolation.AccumulatedTimeMS -= (AdvanceFrames * FixedTickState.FixedStepRealTimeMS);
					}
					const float RawPCT = FixedTickState.Interpolation.AccumulatedTimeMS / (float)FixedTickState.FixedStepRealTimeMS;
					FixedTickState.Interpolation.PCT = FMath::Clamp<float>(RawPCT, 0.f, 1.f);
					jnpEnsureMsgf(FixedTickState.Interpolation.PCT >= 0.f && FixedTickState.Interpolation.PCT <= 1.f, TEXT("Interpolation PCT out of range. %f"), FixedTickState.Interpolation.PCT);

					const float PCTms = FixedTickState.Interpolation.PCT * (float)FixedTickState.FixedStepMS;
					FixedTickState.Interpolation.InterpolatedTimeMS = ((FixedTickState.Interpolation.ToFrame-1) * FixedTickState.FixedStepMS) + (int32)PCTms;
					//const float BufferedTimeMS = (LatestRecvFrame * FixedTickState.FixedStepMS) - FixedTickState.Interpolation.InterpolatedTimeMS;
					//UE_LOG(LogTemp,Error,TEXT("InterpTimeMs %d , SimTime %d , BufferedTime : %f"),FixedTickState.Interpolation.InterpolatedTimeMS
						//,FixedTickState.GetTotalSimTimeMS(),BufferedTimeMS);
					//UE_LOG(LogTemp, Warning, TEXT("[Interpolate] %s Interpolating ToFrame %d. PCT: %.2f. Buffered: %d"), *GetPathName(), FixedTickState.Interpolation.ToFrame, FixedTickState.Interpolation.PCT, FixedTickState.Interpolation.LatestRecvFrame - FixedTickState.Interpolation.ToFrame);

					{
						TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::FinalizeFrame);
						for (TUniquePtr<IJoltFixedInterpolateService>& Ptr : Services.FixedInterpolate.Array)
						{
							Ptr->FinalizeFrame(DeltaTimeSeconds, &FixedTickState);
						}
						for (UJoltNetworkPredictionLagCompensation* RegisteredComp : RegisteredLagCompComponents)
						{
							if (!RegisteredComp->HasSimulation())
							{
								RegisteredComp->CaptureStateAndAddToHistory(FixedTickState.Interpolation.InterpolatedTimeMS);
							}
						}
					}
				}
			}
		}

		if (Services.IndependentInterpolate.Array.Num() > 0)
		{
			const int32 DesiredBufferedMS = Settings.IndependentTickInterpolationBufferedMS;
			const int32 MaxBufferedMS = Settings.IndependentTickInterpolationMaxBufferedMS;
		
			if (VariableTickState.Interpolation.LatestRecvTimeMS > DesiredBufferedMS)
			{
				float InterpolationRate = 1.f;

				const int32 BufferedMS = VariableTickState.Interpolation.LatestRecvTimeMS - (int32)VariableTickState.Interpolation.fTimeMS;
				if (BufferedMS > MaxBufferedMS)
				{
					UE_LOG(LogJoltNetworkPrediction, Warning, TEXT("Independent Interpolation fell behind. BufferedMS: %d"), BufferedMS);
					VariableTickState.Interpolation.fTimeMS = (float)(VariableTickState.Interpolation.LatestRecvTimeMS - DesiredBufferedMS);
				}
				else if (BufferedMS <= 0)
				{
					UE_LOG(LogJoltNetworkPrediction, Warning, TEXT("Independent Interpolation starved: %d"), BufferedMS);
					InterpolationRate = 0.f;
				}

				if (InterpolationRate > 0.f)
				{
					const float fScaledDeltaTimeMS = (InterpolationRate * fEngineFrameDeltaTimeMS);
					VariableTickState.Interpolation.fTimeMS += fScaledDeltaTimeMS;
				}

				{
					TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::FinalizeFrame);
					for (TUniquePtr<IJoltIndependentInterpolateService>& Ptr : Services.IndependentInterpolate.Array)
					{
						Ptr->FinalizeFrame(DeltaTimeSeconds, &VariableTickState);
					}
				}
			}
		}
	}


	//-------------------------------------------------------------------------------------------------------------
	// Handle newly spawned services right now, so that they can Finalize/SendRPCs on the very first frame of life
	//-------------------------------------------------------------------------------------------------------------

	bLockServices = false;
	DeferredServiceConfigDelegate.Broadcast(this);
	DeferredServiceConfigDelegate.Clear();

	// -------------------------------------------------------------------------
	//	Finalize
	// -------------------------------------------------------------------------
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::FinalizeFrame);

		const int32 FixedTotalSimTimeMS = FixedTickState.GetTotalSimTimeMS();
		const int32 FixedServerFrame = FixedTickState.PendingFrame + FixedTickState.Offset;
		for (TUniquePtr<IJoltFinalizeService>& Ptr : Services.FixedFinalize.Array)
		{
			Ptr->FinalizeFrame(DeltaTimeSeconds, FixedServerFrame, FixedTotalSimTimeMS, FixedTickState.FixedStepMS);
		}

		if (Settings.bEnableFixedTickSmoothing)
		{
			for (TUniquePtr<IJoltFixedSmoothingService>& Ptr : Services.FixedSmoothing.Array)
			{
				Ptr->FinalizeSmoothingFrame(&FixedTickState);
			}
		}

		const int32 IndependentTotalSimTimeMS = VariableTickState.Frames[VariableTickState.PendingFrame].TotalMS;
		const int32 IndependentFrame = VariableTickState.PendingFrame;
		for (TUniquePtr<IJoltFinalizeService>& Ptr : Services.IndependentLocalFinalize.Array)
		{
			Ptr->FinalizeFrame(DeltaTimeSeconds, IndependentFrame, IndependentTotalSimTimeMS, 0);
		}
	
		for (TUniquePtr<IJoltRemoteFinalizeService>& Ptr : Services.IndependentRemoteFinalize.Array)
		{
			Ptr->FinalizeFrame(DeltaTimeSeconds);
		}
	}
	

	// -------------------------------------------------------------------------
	// Call server RPC (Independent)
	// -------------------------------------------------------------------------
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(JoltNetworkPrediction::CallServerRPC);
		for (TUniquePtr<IJoltServerRPCService>& Ptr : Services.ServerRPC.Array)
		{
			Ptr->CallServerRPC(DeltaTimeSeconds);
		}
	}
}


EJoltNetworkPredictionTickingPolicy UJoltNetworkPredictionWorldManager::PreferredDefaultTickingPolicy() const
{
	return Settings.PreferredTickingPolicy;
}

// Added to Tick Player Controllers before Simulation , Disable Their tick for that frame and then re-enable it when frame ends.
// this is to ensure if player controller changes worlds it will tick on its own fine.
void UJoltNetworkPredictionWorldManager::TickLocalPlayerControllers(ELevelTick InLevelTick, float InDeltaSeconds) const
{
	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		APlayerController* PC = Iterator->Get();
		if (PC && PC->IsLocalPlayerController())
		{
			PC->TickActor(InDeltaSeconds,InLevelTick,PC->PrimaryActorTick);
			PC->PrimaryActorTick.SetTickFunctionEnable(false);
		}
	}
}

void UJoltNetworkPredictionWorldManager::EnableLocalPlayerControllersTicking() const
{
	// Enable Tick Back on Local Player Controllers, in case we change worlds
	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		APlayerController* PC = Iterator->Get();
		if (PC && PC->IsLocalPlayerController())
		{
			PC->PrimaryActorTick.SetTickFunctionEnable(true);
		}
	}
}


// --------------------------------------- Lag Compensation ------------------------------//

void UJoltNetworkPredictionWorldManager::RegisterRewindableComponent(UJoltNetworkPredictionLagCompensation* RewindComp)
{
	if (RewindComp)
	{
		if (LagCompRegistrationLock > 0)
		{
			PendingAddLagCompComponents.AddUnique(RewindComp);
			return;
		}
		// we do this check so we don't initialize again, can use TSet or just AddUnique
		// but that doesn't tell me if it's newly added
		if (!RegisteredLagCompComponents.Contains(RewindComp))
		{
			int32 ActualMaxRewindTime = Settings.MaxRewindTimeMS + Settings.FixedTickInterpolationBufferedMS;
			const int32 TickTimeMS = FMath::Floor((1 / Settings.FixedTickFrameRate) * 1000);
			ActualMaxRewindTime += Settings.FixedTickDesiredBufferedInputCount * TickTimeMS;
			// 10 frames for safety
			ActualMaxRewindTime += TickTimeMS * 10.f;
			const int32 MaxSize = FMath::Max(ActualMaxRewindTime , Settings.MaxBufferedRewindHistoryTimeMS) / TickTimeMS;
			RewindComp->InitializeHistory(MaxSize);
			RegisteredLagCompComponents.Add(RewindComp);
		}
	}
}

void UJoltNetworkPredictionWorldManager::UnregisterRewindableComponent(UJoltNetworkPredictionLagCompensation* RewindComp)
{
	if (LagCompRegistrationLock > 0)
	{
		PendingRemoveLagCompComponents.AddUnique(RewindComp);
		return;
	}
	RegisteredLagCompComponents.Remove(RewindComp);
}

bool UJoltNetworkPredictionWorldManager::RewindActors(AActor* RequestingActor, const float RewindSimTimeMS)
{
	if (!RequestingActor)
	{
		return false;
	}
	const float CurrentSimTimeMS = GetCurrentLagCompensationTimeMS(RequestingActor);
	if (UKismetSystemLibrary::IsStandalone(RequestingActor))
	{
		// This function can be called in simulation code without caring if it's server to client.
		// this would happen when local player is trying to rewind others, but he's at the current interpolation time.
		return false;
	}
	
	if (RegisteredLagCompComponents.Num() <= 0)
	{
		return false;
	}

	float FinalRewindTime = FMath::Clamp(RewindSimTimeMS,0.f,CurrentSimTimeMS);
	// if we are on server Clamp Rewind Time To A Maximum, client can rewind his own simulation to correct himself for as long as he likes.
	// Client Rewinds To make sure any targeting that happens during the simulation , has other actors in same place at the re-simulation time.
	if (RequestingActor->GetLocalRole() == ROLE_Authority)
	{
		FinalRewindTime = ClampRewindingTime(CurrentSimTimeMS,RewindSimTimeMS);
		if (FinalRewindTime > RewindSimTimeMS)
		{
			UE_LOG(LogJoltNetworkPrediction,Warning,TEXT("Desired Lag Compensation Rewind Exceeded Supported ping for %s : Desired Time %f , FinalTime %f")
		,*GetNameSafe(RequestingActor),RewindSimTimeMS,FinalRewindTime);
		}
	}
	FLagCompensationRegistrationLock(this);
	bool DidRewind = false;
	for (UJoltNetworkPredictionLagCompensation* RewindComp : RegisteredLagCompComponents)
	{
		if (!RewindComp || !RequestingActor)
		{
			continue;
		}
		const FNpLagCompensationHistory& History = RewindComp->GetLagCompensationHistory();
		if (RequestingActor == RewindComp->GetOwner() || History.Num() <= 0)
		{
			continue;
		}
		// draw debug on local client based on current state
		if (History.Last()->SimTimeMs == RewindSimTimeMS)
		{
#if WITH_EDITOR
			if (ToggleLagCompensationDebug() > 0)
			{
				const FColor Color = RewindComp->GetOwnerRole() == ROLE_Authority ? FColor::Red : FColor::Blue;
				const float SizeMultiplier = RewindComp->GetOwnerRole() == ROLE_Authority ? 1.02f : 1.f;
				DrawDebugBox(GetWorld(),History.Last()->Location,History.Last()->CollisionExtent
					,FQuat::Identity,FColor::Blue,false,5.f);
			}
#endif
		}
		// if we are already rewinding we already captured latest state
		if (History.bIsInRewind == false)
		{
			RewindComp->CapturePreRewindState();
		}

		
		TSharedPtr<FNpLagCompensationData> CurrentRewindData = GetRewindDataFromComponent(FinalRewindTime,RewindComp);
		if (CurrentRewindData == nullptr)
		{
			continue;
		}

		if (History.Last()->SimTimeMs != RewindSimTimeMS)
		{
			RewindComp->SetOwningActorState(CurrentRewindData);
			RewindComp->OnStartedRewind();
			DidRewind = true;
		}
#if WITH_EDITOR
		if (ToggleLagCompensationDebug() > 0)
		{
			const FColor Color = RewindComp->GetOwnerRole() == ROLE_Authority ? FColor::Red : FColor::Blue;
			DrawDebugBox(GetWorld(),CurrentRewindData->Location,CurrentRewindData->CollisionExtent
				,FQuat::Identity,Color,false,5.f);
		}
#endif
	}
	return DidRewind;
}

bool UJoltNetworkPredictionWorldManager::UnwindActors()
{
	if (RegisteredLagCompComponents.Num() <= 0)
	{
		return false;
	}
	FLagCompensationRegistrationLock(this);
	bool DidUnwind = false;
	for ( UJoltNetworkPredictionLagCompensation* RewindComp : RegisteredLagCompComponents)
	{
		if (!RewindComp)
		{
			continue;
		}
		const FNpLagCompensationHistory& History = RewindComp->GetLagCompensationHistory();
		if (!History.bIsInRewind)
		{
			continue;
		}
		if (History.Num() <= 0)
		{
			UE_LOG(LogJoltNetworkPrediction, Log, TEXT("Trying To Unwind Actor %s That Has No History"),*GetNameSafe(RewindComp->GetOwner()));
			continue;
		}

		USceneComponent* RootComponent = RewindComp->GetOwner()->GetRootComponent();
		if (!RootComponent)
		{
			continue;
		}

		RewindComp->SetOwningActorState(History.PreRewindData);
		RewindComp->OnEndedRewind();
		DidUnwind = true;
	}
	return DidUnwind;
}

TSharedPtr<FNpLagCompensationData> UJoltNetworkPredictionWorldManager::GetActorStateAtTime(AActor* RequestingActor,
	AActor* TargetActor, const float TargetSimTimeMS)
{
	if (!TargetActor)
	{
		return nullptr;
	}
	UJoltNetworkPredictionLagCompensation* TargetComp = TargetActor->GetComponentByClass<UJoltNetworkPredictionLagCompensation>();
	if (!TargetComp)
	{
		return nullptr;
	}
	return GetComponentStateAtTime(RequestingActor,TargetComp,TargetSimTimeMS);
}

TSharedPtr<FNpLagCompensationData> UJoltNetworkPredictionWorldManager::GetComponentStateAtTime(AActor* RequestingActor,
								UJoltNetworkPredictionLagCompensation* TargetComp, const float TargetSimTimeMS)
{
	if (!RequestingActor || !TargetComp)
	{
		return nullptr;
	}
	const float CurrentSimTimeMS = GetCurrentLagCompensationTimeMS(TargetComp->GetOwner());
	// this check is here because it should be impossible, every component 
	checkf(RegisteredLagCompComponents.Num() > 0,TEXT("Trying to get actor state from non-existent component ??"
												   " this valid UJoltNetworkPredictionLagCompensation %s , didn't register with the subsystem, "
			   "possibly override of on component registered didn't call parent"),*GetNameSafe(TargetComp));
	if (FMath::IsNearlyEqual(CurrentSimTimeMS,TargetSimTimeMS))
	{
		// This function can be called in simulation code without caring if it's server to client.
		// this would happen when local player is trying to rewind others, but he's at the current interpolation time.
		// return latest, as in what the player sees.
		return GetLatestDataFromComponent(TargetComp);
	}

	float FinalRewindTime = FMath::Clamp(TargetSimTimeMS,0.f,CurrentSimTimeMS);
	// if we are on server Clamp Rewind Time To A Maximum, client can rewind his own simulation to correct himself for as long as he likes.
	// Client Rewinds To make sure any targeting that happens during the simulation , has other actors in same place at the re-simulation time.
	if (RequestingActor->GetLocalRole() == ROLE_Authority)
	{
		FinalRewindTime = ClampRewindingTime(CurrentSimTimeMS,TargetSimTimeMS);
		if (FinalRewindTime > TargetSimTimeMS)
		{
			UE_LOG(LogJoltNetworkPrediction,Warning,TEXT("Desired Lag Compensation Rewind Exceeded Supported ping for %s : Desired Time %f , FinalTime %f")
		,*GetNameSafe(RequestingActor),TargetSimTimeMS,FinalRewindTime);
		}
	}
	return TargetComp->GetLagCompensationHistory().GetStateAtTime(FinalRewindTime);
}


float UJoltNetworkPredictionWorldManager::GetInterpolationDelayMS(const UJoltNetworkPredictionWorldManager* NetworkPredictionWorldManager)
{
	if (NetworkPredictionWorldManager)
	{
		if (NetworkPredictionWorldManager->GetSettings().PreferredTickingPolicy == EJoltNetworkPredictionTickingPolicy::Fixed)
		{
			return NetworkPredictionWorldManager->GetSettings().FixedTickInterpolationBufferedMS;
		}
		else
		{
			return NetworkPredictionWorldManager->GetSettings().IndependentTickInterpolationMaxBufferedMS;
		}
	}
	return 0.f;
}

float UJoltNetworkPredictionWorldManager::ClampRewindingTime( const float CurrentTime,const float InTargetRewindTime) const
{
	float ClampedTime = InTargetRewindTime;
	const UJoltNetworkPredictionWorldManager* NetworkPredictionWorldManager = GetWorld()->GetSubsystem<UJoltNetworkPredictionWorldManager>();
	if (!NetworkPredictionWorldManager)
	{
		return InTargetRewindTime;
	}

	float MaxRewindDur = GetMaxRewindDuration(NetworkPredictionWorldManager);
	const float CurrentRewindDur = CurrentTime - InTargetRewindTime;
	MaxRewindDur = FMath::Min(CurrentRewindDur,MaxRewindDur);
	return CurrentTime - MaxRewindDur;
	//return ClampedTime;
}

float UJoltNetworkPredictionWorldManager::GetMaxRewindDuration(const UJoltNetworkPredictionWorldManager* NetworkPredictionWorldManager) const
{
	// the extra 1 frame is for lenience 
	float MaxRewindDur = Settings.MaxRewindTimeMS;
	if (NetworkPredictionWorldManager)
	{
		MaxRewindDur += GetInterpolationDelayMS(NetworkPredictionWorldManager);
		MaxRewindDur += (NetworkPredictionWorldManager->GetSettings().FixedTickDesiredBufferedInputCount + 1) * NetworkPredictionWorldManager->GetFixedTickState().FixedStepRealTimeMS;
	}
	return MaxRewindDur;
}

TSharedPtr<FNpLagCompensationData> UJoltNetworkPredictionWorldManager::GetRewindDataFromComponent(const float TargetTimeMS, const UJoltNetworkPredictionLagCompensation* LagCompComponent)
{
	if (!LagCompComponent)
	{
		return nullptr;
	}
	return  LagCompComponent->GetLagCompensationHistory().GetStateAtTime(TargetTimeMS);
}

TSharedPtr<FNpLagCompensationData> UJoltNetworkPredictionWorldManager::GetLatestDataFromComponent(
	const UJoltNetworkPredictionLagCompensation* LagCompComponent)
{
	return LagCompComponent->GetLagCompensationHistory().LastCopy();
}

const TArray<UJoltNetworkPredictionLagCompensation*>& UJoltNetworkPredictionWorldManager::GetRegisteredComponents()
{
	return RegisteredLagCompComponents;
}
