// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "JoltNetworkPredictionLagCompensationData.h"
#include "Subsystems/WorldSubsystem.h"

#include "Services/JoltNetworkPredictionServiceRegistry.h"
#include "JoltNetworkPredictionSerialization.h"

#include "JoltNetworkPredictionWorldManager.generated.h"

class UJoltNetworkPredictionLagCompensation;
class FChaosSolversModule;
class AJoltNetworkPredictionReplicatedManager;

namespace UE::Net::Private
{
	struct FNetPredictionTestWorld;
}

UCLASS()
class JOLTNETWORKPREDICTION_API UJoltNetworkPredictionWorldManager : public UWorldSubsystem
{
	GENERATED_BODY()
public:

	static UJoltNetworkPredictionWorldManager* ActiveInstance;

	UJoltNetworkPredictionWorldManager();

	// Server created, replicated manager (only used for centralized/system wide data replication)
	UPROPERTY()
	TObjectPtr<AJoltNetworkPredictionReplicatedManager> ReplicatedManager = nullptr;

	// Subsystem Init/Deinit
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Returns unique ID for a new simulation instance regardless of type.
	// Clients set bForClient to get a negative, temp ID to use until server version comes in
	FJoltNetworkPredictionID CreateSimulationID(const bool bForClient)
	{
		return FJoltNetworkPredictionID(bForClient ? TempClientSpawnCount-- : InstanceSpawnCounter++);
	}

	template<typename ModelDef>
	void RemapClientSimulationID(FJoltNetworkPredictionID ClientID, FJoltNetworkPredictionID ServerID)
	{
		jnpEnsure((int32)ClientID < INDEX_NONE && (int32)ServerID >= 0);
		jnpEnsure(ClientID.GetTraceID() == ServerID.GetTraceID());
		
		TJoltModelDataStore<ModelDef>* DataStore = Services.GetDataStore<ModelDef>();
		TInstanceData<ModelDef>& InstanceData = DataStore->Instances.FindOrAdd(ServerID);
		InstanceData = MoveTemp(DataStore->Instances.FindOrAdd(ClientID));
		DataStore->Instances.Remove(ClientID);
	}

	template<typename ModelDef>
	void RegisterInstance(FJoltNetworkPredictionID ID, const TJoltNetworkPredictionModelInfo<ModelDef>& ModelInfo)
	{
		// Register with data store
		TJoltModelDataStore<ModelDef>* DataStore = Services.GetDataStore<ModelDef>();
		TInstanceData<ModelDef>& InstanceData = DataStore->Instances.FindOrAdd(ID);
		InstanceData.Info = ModelInfo;
		InstanceData.TraceID = ID.GetTraceID();
		InstanceData.CueDispatcher->Driver = ModelInfo.Driver; // Awkward: we should convert Cues to a service so this isn't needed.
		InstanceData.Info.View->CueDispatcher = &InstanceData.CueDispatcher.Get(); // Double awkward: we should move Cuedispatcher and clean up these weird links
	}

	template<typename ModelDef>
	void UnregisterInstance(FJoltNetworkPredictionID ID)
	{
		if (!bLockServices)
		{
			Services.UnregisterInstance<ModelDef>(ID);
		}
		else
		{
			DeferredServiceConfigDelegate.AddLambda([ID](UJoltNetworkPredictionWorldManager* Manager)
			{
				TJoltModelDataStore<ModelDef>* DataStore = Manager->Services.GetDataStore<ModelDef>();
	            
				/// Clean up any Deferred Register Calls that are still pending, because apparently we get cleaned up instantly
				/// after being created, and since Delegate InvocationLists are reverse iterated, the deferred Unregister Call gets
				/// triggered first.
				FDelegateHandle* DeferredRegisterHandlePtr = DataStore->DeferredRegisterHandle.Find(ID);
				if (DeferredRegisterHandlePtr)
				{
					Manager->DeferredServiceConfigDelegate.Remove(*DeferredRegisterHandlePtr);
					DataStore->DeferredRegisterHandle.Remove(ID);
				}
	            
				Manager->Services.UnregisterInstance<ModelDef>(ID);
			});
		}
	}

	template<typename ModelDef>
	void ConfigureInstance(FJoltNetworkPredictionID ID, const FJoltNetworkPredictionInstanceArchetype& Archetype, const FJoltNetworkPredictionInstanceConfig& Config, FJoltReplicationProxySet RepProxies, ENetRole Role, bool bHasNetConnection
		, UJoltNetworkPredictionPlayerControllerComponent* RPCHandler);
	

	EJoltNetworkPredictionTickingPolicy PreferredDefaultTickingPolicy() const;

	void SyncNetworkPredictionSettings(const UJoltNetworkPredictionSettingsObject* Settings);

	const FJoltNetworkPredictionSettings& GetSettings() const { return Settings; }

	

	const FJoltFixedTickState& GetFixedTickState() const { return FixedTickState; }
	const FJoltVariableTickState& GetVariableTickState() const { return VariableTickState; }

	// IMPORTANT: this makes variable tick state unusable since it's only implemented for fixed tick
	// this is very crude way of doing this, each service can add a lambda along with the ID to its own UJoltNetworkPredictionPlayerControllerComponent
	// and when an input is received the component will loop through inputs received which has ID for each entry and call the lambda for correct IDs
	void OnInputReceived(const int32& Frame,const float& InterpolationTime,const TArray<FJoltSimulationReplicatedInput>& Inputs,
		UJoltNetworkPredictionPlayerControllerComponent* RPCHandler);
	void OnReceivedAckedData(const FJoltSerializedAckedFrames& AckedFrames, UJoltNetworkPredictionPlayerControllerComponent* RPCHandler);

	void RegisterRPCHandler(UJoltNetworkPredictionPlayerControllerComponent* RPCHandler);
	void UnRegisterRPCHandler(UJoltNetworkPredictionPlayerControllerComponent* RPCHandler);
	void SetTimeDilation(const FJoltSimTimeDilation& TimeDilation);
	

private:
	
	
	FJoltNetworkPredictionSettings Settings;

	FJoltFixedTickState FixedTickState;
	FJoltVariableTickState VariableTickState;
	FJoltNetworkPredictionServiceRegistry Services;

	// Player controller component responsible for handling input and data that should be per net connection not per sim instance.
	UPROPERTY()
	TArray<UJoltNetworkPredictionPlayerControllerComponent*> RPCHandlers;

	void OnWorldPreTick(UWorld* InWorld, ELevelTick InLevelTick, float InDeltaSeconds);
	void ReconcileSimulationsPostNetworkUpdate();
	void BeginNewSimulationFrame(UWorld* InWorld, ELevelTick InLevelTick, float InDeltaSeconds);

	void TickLocalPlayerControllers(ELevelTick InLevelTick, float InDeltaSeconds) const;
	void EnableLocalPlayerControllersTicking() const;
	// For ease of unit testing
	friend struct UE::Net::Private::FNetPredictionTestWorld;
	void OnWorldPreTick_Internal(float InDeltaSeconds, float InFixedFrameRate);
	void ReconcileSimulationsPostNetworkUpdate_Internal();
	void BeginNewSimulationFrame_Internal(float InDeltaSeconds);

	FDelegateHandle PreTickDispatchHandle;
	FDelegateHandle PostTickDispatchHandle;
	FDelegateHandle PreWorldActorTickHandle;

	int32 InstanceSpawnCounter = 1;
	int32 TempClientSpawnCount = -2; // negative IDs for client to use before getting server assigned id

	//Server Only Data
	FJoltServerAckedFrames ServerAckedFrames;
	// Client Only Data
	FJoltAckedFrames LocalAckedFrames;
	
	// Callbacks to change subscribed services, that couldn't be made inline
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnHandleDeferredServiceConfig, UJoltNetworkPredictionWorldManager*);
	FOnHandleDeferredServiceConfig DeferredServiceConfigDelegate;
	bool bLockServices = false; // Services are locked and we can't
	
	// ---------------------------------------

	template<typename ModelDef>
	void BindServerNetRecv_Fixed(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore);

	template<typename ModelDef>
	void BindServerNetRecv_Independent(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore);

	// ----

	template<typename ReplicatorType, typename ModelDef=typename ReplicatorType::ModelDef>
	void BindClientNetRecv_Fixed(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore, ENetRole NetRole);

	template<typename ReplicatorType, typename ModelDef=typename ReplicatorType::ModelDef>
	void BindClientNetRecv_Independent(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore, ENetRole NetRole);

	// ----

	template<typename ReplicatorType, typename ModelDef=typename ReplicatorType::ModelDef>
	void BindNetSend_Fixed(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore);

	template<typename ReplicatorType, typename ModelDef=typename ReplicatorType::ModelDef>
	void BindNetSend_IndependentLocal(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore);

	template<typename ReplicatorType, typename ModelDef=typename ReplicatorType::ModelDef>
	void BindNetSend_IndependentRemote(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore);

	// ----
	
	template<typename ReplicatorType, typename ModelDef=typename ReplicatorType::ModelDef>
	void BindReplayNetSendRecv_Fixed(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore, ENetRole NetRole);

	template<typename ReplicatorType, typename ModelDef = typename ReplicatorType::ModelDef>
	void BindReplayNetSendRecv_IndependentLocal(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore, ENetRole NetRole);

	template<typename ReplicatorType, typename ModelDef = typename ReplicatorType::ModelDef>
	void BindReplayNetSendRecv_IndependentRemote(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore, ENetRole NetRole);

	// ---------------------------------------

	template<typename ModelDef>
	void InitClientRecvData(FJoltNetworkPredictionID ID, TJoltClientRecvData<ModelDef>& ClientRecvData, TJoltModelDataStore<ModelDef>* DataStore, ENetRole NetRole);

   // --------------------------------------- Lag Compensation ------------------------------//
public:
	UFUNCTION(BlueprintCallable,Category = LagCompensation)
	void RegisterRewindableComponent(UJoltNetworkPredictionLagCompensation* RewindComp);

	UFUNCTION(BlueprintCallable,Category = LagCompensation)
	void UnregisterRewindableComponent(UJoltNetworkPredictionLagCompensation* RewindComp);

	/*
	 * CAUTION: Be very careful when using this function. if you don't call UnwindActors right after you are done,
	 * in the same execution you can cause big bugs, mainly server will move players and not bring them back
	 * effectively making them all teleport for no reason.
	 * Use Targeting Library Perform targeting , if you can.
	 * 
	 * normally this would not be exposed and targeting processors would only be used with this.
	 * but targeting didn't feel like it's something NP manager should be responsible for. maybe it should.
	 * or maybe we can use query only collision component proxies, lives on the actor,
	 * and we move them so we don't move the actors.
	 * and for safety beginning of every tick of movement we rest the proxies relative transform to default
	 * so even if Unwind is not called , it will be rest. and when rewind is called again it will rewind or reset.
	 * 
	 * but this will require game code to know when to use channels that main collision component ignores,
	 * and is block/overlap with these proxies.
	 * 
	 */
	UFUNCTION(BlueprintCallable,Category = LagCompensation)
	bool RewindActors(AActor* RequestingActor, const float RewindSimTimeMS);
	
	UFUNCTION(BlueprintCallable,Category = LagCompensation)
	bool UnwindActors();
	
	UFUNCTION(BlueprintPure,Category = LagCompensation)
	const TArray<UJoltNetworkPredictionLagCompensation*>& GetRegisteredComponents();
	
	UFUNCTION(BlueprintPure,Category = LagCompensation)
	float GetCurrentLagCompensationTimeMS(const AActor* Actor) const;

	UFUNCTION(BlueprintPure,Category = LagCompensation)
	FNpLagCompensationData GetActorDefaultStateAtTime(AActor* RequestingActor,AActor* TargetActor,const float TargetSimTimeMS);

	TSharedPtr<FNpLagCompensationData> GetActorStateAtTime(AActor* RequestingActor,AActor* TargetActor,
	const float TargetSimTimeMS) ;
	
	TSharedPtr<FNpLagCompensationData> GetComponentStateAtTime(AActor* RequestingActor,UJoltNetworkPredictionLagCompensation* TargetComp,
	const float TargetSimTimeMS) ;
	TSharedPtr<FNpLagCompensationData> GetRewindDataFromComponent(const float TargetTimeMS, const UJoltNetworkPredictionLagCompensation* LagCompComponent);

	TSharedPtr<FNpLagCompensationData> GetLatestDataFromComponent(const UJoltNetworkPredictionLagCompensation* LagCompComponent);

	// this does expose getting a specific state a simulation at a specific time,
	// but using this for lag compensation will require more templating.
	// - the driver would need the functions to :
	// "SetActorStateForLagCompensation" set the actor state based on a simulation state in the past
	// we perform rewind/unwind by calling SetActorStateForLagCompensation once from history and then back from pending
	// - Need a service that only registers instances that implement the needed functions and have right role
	// - Finally sync state will need to have CanRewind further bool to stop rewinding someone further when gameplay wants to.
	// the issue arises from the states and instances of simulations being completely separate
	// and there's no way of knowing when multiple simulation effect lag compensation,
	// say ability simulation wants to stop rewinding at a specific state "has tag Ability.Dash",
	// movement simulation has no idea of this and location will still be rewound,
	// this why lag compensation is implemented the way it is with its own buffer and filling its own state directly from actor state.
	template<typename ModelDef,typename TSyncType>
	bool GetSyncStateAtTime(int32 ID , const float TimeMS, TSyncType& OutState)
	{
		TJoltModelDataStore<ModelDef>* DataStore = Services.GetDataStore<ModelDef>();
		if (!DataStore)
		{
			return false;
		}
		TJoltInstanceFrameState<ModelDef>* Frames = DataStore->Frames.Find(FJoltNetworkPredictionID(ID));
		if (!Frames)
		{
			return false;
		}

		
		// The total number of elapsed frames, as a floating-point value.
		float TotalFrames = TimeMS / FixedTickState.FixedStepMS;

		// The frame that has just completed (the integer part).
		const int32 OutFrameBefore = FMath::FloorToInt(TotalFrames);

		// The next frame.
		const int32 OutFrameAfter = OutFrameBefore + 1;

		// The alpha is the fractional part of the total frames.
		float Alpha = TotalFrames - OutFrameBefore;

		if (FMath::IsNearlyEqual(Alpha,1.f))
		{
			Frames->Buffer[OutFrameAfter].SyncState.CopyTo(&OutState);
			return true; 
		}
		if (FMath::IsNearlyEqual(Alpha,0.f))
		{
			Frames->Buffer[OutFrameBefore].SyncState.CopyTo(&OutState);
			return true; 
		}
		
		Frames->Buffer[OutFrameBefore].SyncState.CopyTo(&OutState);
		OutState.Interpolate(Frames->Buffer[OutFrameBefore].SyncState,Frames->Buffer[OutFrameAfter].SyncState,Alpha);
		return true;
		
	}
private:
	
	static float GetInterpolationDelayMS(const UJoltNetworkPredictionWorldManager* NetworkPredictionWorldManager);
	
	float ClampRewindingTime(const float CurrentTime ,const float InTargetRewindTime) const;
	
	float GetMaxRewindDuration(const UJoltNetworkPredictionWorldManager* NetworkPredictionWorldManager) const;
	
protected:

	struct FLagCompensationRegistrationLock
	{
		FLagCompensationRegistrationLock(UJoltNetworkPredictionWorldManager* InManager)
		{
			Manager = InManager;
			Manager->LagCompRegistrationLock++;
		}
		~FLagCompensationRegistrationLock()
		{
			Manager->LagCompRegistrationLock--;
			if (Manager->LagCompRegistrationLock == 0)
			{
				for (UJoltNetworkPredictionLagCompensation* PendingRemove : Manager->PendingRemoveLagCompComponents)
				{
					Manager->UnregisterRewindableComponent(PendingRemove);
				}
				Manager->PendingRemoveLagCompComponents.Empty();

				for (UJoltNetworkPredictionLagCompensation* PendingAdd : Manager->PendingAddLagCompComponents)
				{
					Manager->RegisterRewindableComponent(PendingAdd);
				}
				Manager->PendingAddLagCompComponents.Empty();
			}
		}

		UJoltNetworkPredictionWorldManager* Manager;
	};

	UPROPERTY(Transient)
	TArray<UJoltNetworkPredictionLagCompensation*> RegisteredLagCompComponents;

	UPROPERTY(Transient)
	TArray<UJoltNetworkPredictionLagCompensation*> PendingAddLagCompComponents;

	UPROPERTY(Transient)
	TArray<UJoltNetworkPredictionLagCompensation*> PendingRemoveLagCompComponents;

	int32 LagCompRegistrationLock = 0;
};


// This is the function that takes the config and current network state (role/connection) and subscribes to 
// the appropriate internal NetworkPrediction services.
template<typename ModelDef>
void UJoltNetworkPredictionWorldManager::ConfigureInstance(FJoltNetworkPredictionID ID, const FJoltNetworkPredictionInstanceArchetype& Archetype, const FJoltNetworkPredictionInstanceConfig& Config, FJoltReplicationProxySet RepProxies, ENetRole Role, bool bHasNetConnection
	, UJoltNetworkPredictionPlayerControllerComponent* RPCHandler)
{
	static constexpr FJoltNetworkPredictionModelDefCapabilities Capabilities = FJoltNetworkPredictionDriver<ModelDef>::GetCapabilities();

	jnpCheckSlow((int32)ID > 0);
	jnpEnsure(Role != ROLE_None);

	TJoltModelDataStore<ModelDef>* DataStore = Services.GetDataStore<ModelDef>();
	TInstanceData<ModelDef>& InstanceData = *DataStore->Instances.Find(ID);
	TJoltInstanceFrameState<ModelDef>& FrameData = DataStore->Frames.FindOrAdd(ID);

	const int32 PrevPendingFrame = InstanceData.Info.View->PendingFrame;
	const bool bNewInstance = (InstanceData.NetRole == ROLE_None);
	InstanceData.NetRole = Role;
	InstanceData.Info.RPCHandler = RPCHandler;
	
	TJoltRemoteInputService<ModelDef>::SetMaxFaultLimit(Settings.MaximumRemoteInputFaultLimit);
	TJoltRemoteInputService<ModelDef>::SetDesiredBufferedInputs(Settings.FixedTickDesiredBufferedInputCount);
	TJoltFixedSmoothingService<ModelDef>::SetSmoothingSpeed(Settings.SmoothingSpeed);
	

	EJoltNetworkPredictionService ServiceMask = EJoltNetworkPredictionService::None;
	
	if (Archetype.TickingMode == EJoltNetworkPredictionTickingPolicy::Independent)
	{
		// Point cached view to the VariableTickState's pending frame
		InstanceData.Info.View->UpdateView(VariableTickState.PendingFrame, 
			VariableTickState.GetNextTimeStep().TotalSimulationTime,
			&FrameData.Buffer[VariableTickState.PendingFrame].InputCmd, 
			&FrameData.Buffer[VariableTickState.PendingFrame].SyncState, 
			&FrameData.Buffer[VariableTickState.PendingFrame].AuxState);
		
		switch (Role)
		{
			case ENetRole::ROLE_Authority:
			{
				if (bHasNetConnection)
				{
					// Remotely controlled
					BindServerNetRecv_Independent<ModelDef>(ID, RepProxies.ServerRPC, DataStore);
					BindNetSend_IndependentRemote<TIndependentTickReplicator_AP<ModelDef>>(ID, RepProxies.AutonomousProxy, DataStore);
					BindNetSend_IndependentRemote<TIndependentTickReplicator_SP<ModelDef>>(ID, RepProxies.SimulatedProxy, DataStore);
					BindReplayNetSendRecv_IndependentRemote<TIndependentTickReplicator_SP<ModelDef>>(ID, RepProxies.Replay, DataStore, Role);

					ServiceMask |= EJoltNetworkPredictionService::IndependentRemoteTick;
					ServiceMask |= EJoltNetworkPredictionService::IndependentRemotePhysics;
					ServiceMask |= EJoltNetworkPredictionService::IndependentRemoteFinalize;

					// Point view to the ServerRecv PendingFrame instead
					TJoltServerRecvData_Independent<ModelDef>* ServerRecvData = DataStore->ServerRecv_IndependentTick.Find(ID);
					jnpCheckSlow(ServerRecvData);

					const int32 ServerRecvPendingFrame = ServerRecvData->PendingFrame;

					auto& PendingFrameData = FrameData.Buffer[ServerRecvPendingFrame];
					InstanceData.Info.View->UpdateView(ServerRecvPendingFrame,
						ServerRecvData->TotalSimTimeMS,
						&PendingFrameData.InputCmd, 
						&PendingFrameData.SyncState, 
						&PendingFrameData.AuxState);
				}
				else
				{
					// Locally controlled
					BindNetSend_IndependentLocal<TIndependentTickReplicator_SP<ModelDef>>(ID, RepProxies.SimulatedProxy, DataStore);
					BindReplayNetSendRecv_IndependentLocal<TIndependentTickReplicator_SP<ModelDef>>(ID, RepProxies.Replay, DataStore, Role);

					if (FJoltNetworkPredictionDriver<ModelDef>::HasSimulation())
					{
						if (FJoltNetworkPredictionDriver<ModelDef>::HasInput())
						{
							ServiceMask |= EJoltNetworkPredictionService::IndependentLocalInput;
						}
						ServiceMask |= EJoltNetworkPredictionService::IndependentLocalTick;
						ServiceMask |= EJoltNetworkPredictionService::IndependentLocalPhysics;
						ServiceMask |= EJoltNetworkPredictionService::IndependentLocalFinalize;
					}
				}

				break;
			}
			case ENetRole::ROLE_AutonomousProxy:
			{
				BindClientNetRecv_Independent<TIndependentTickReplicator_AP<ModelDef>>(ID, RepProxies.AutonomousProxy, DataStore, Role);
				BindNetSend_IndependentLocal<TIndependentTickReplicator_Server<ModelDef>>(ID, RepProxies.ServerRPC, DataStore);
				BindReplayNetSendRecv_IndependentLocal<TIndependentTickReplicator_SP<ModelDef>>(ID, RepProxies.Replay, DataStore, Role);
				
				jnpCheckf(FJoltNetworkPredictionDriver<ModelDef>::HasSimulation(), TEXT("AP must have Simulation."));
				jnpCheckf(FJoltNetworkPredictionDriver<ModelDef>::HasInput(), TEXT("AP sim doesn't have Input?"));

				ServiceMask |= EJoltNetworkPredictionService::IndependentLocalInput;
				ServiceMask |= EJoltNetworkPredictionService::IndependentLocalTick;
				ServiceMask |= EJoltNetworkPredictionService::IndependentLocalPhysics;
				ServiceMask |= EJoltNetworkPredictionService::IndependentLocalFinalize;

				ServiceMask |= EJoltNetworkPredictionService::ServerRPC;
				ServiceMask |= EJoltNetworkPredictionService::IndependentRollback;
				break;
			}
			case ENetRole::ROLE_SimulatedProxy:
			{
				BindClientNetRecv_Independent<TIndependentTickReplicator_AP<ModelDef>>(ID, RepProxies.AutonomousProxy, DataStore, Role);
				BindClientNetRecv_Independent<TIndependentTickReplicator_SP<ModelDef>>(ID, RepProxies.SimulatedProxy, DataStore, Role);
				BindReplayNetSendRecv_IndependentLocal<TIndependentTickReplicator_SP<ModelDef>>(ID, RepProxies.Replay, DataStore, Role);

				// Interpolation is the only supported mode for independently ticked SP simulations
				// (will add support for sim-extrapolate eventually)
				ServiceMask |= EJoltNetworkPredictionService::IndependentInterpolate;
				break;
			}
		};
	}
	else if (Archetype.TickingMode == EJoltNetworkPredictionTickingPolicy::Fixed)
	{
		// Point cached view to the FixedTickState's pending frame
		InstanceData.Info.View->UpdateView(FixedTickState.PendingFrame + FixedTickState.Offset,
			FixedTickState.GetTotalSimTimeMS(),
			&FrameData.Buffer[FixedTickState.PendingFrame].InputCmd, 
			&FrameData.Buffer[FixedTickState.PendingFrame].SyncState, 
			&FrameData.Buffer[FixedTickState.PendingFrame].AuxState);
		InstanceData.Info.View->UpdateInterpolationTime(&FrameData.Buffer[FixedTickState.PendingFrame].InterpolationTimeMS);
		// Bind NetSend/Recv and role-dependent services
		switch (Role)
		{
			case ENetRole::ROLE_Authority:
			{
				if (FJoltNetworkPredictionDriver<ModelDef>::HasSimulation())
				{
					BindServerNetRecv_Fixed<ModelDef>(ID, RepProxies.ServerRPC, DataStore);
					BindNetSend_Fixed<TFixedTickReplicator_AP<ModelDef>>(ID, RepProxies.AutonomousProxy, DataStore);

					if (FJoltNetworkPredictionDriver<ModelDef>::HasInput())
					{
						ServiceMask |= bHasNetConnection ? EJoltNetworkPredictionService::FixedInputRemote : EJoltNetworkPredictionService::FixedInputLocal;
					}
				}
				
				BindNetSend_Fixed<TFixedTickReplicator_SP<ModelDef>>(ID, RepProxies.SimulatedProxy, DataStore);
				BindReplayNetSendRecv_Fixed<TFixedTickReplicator_SP<ModelDef>>(ID, RepProxies.Replay, DataStore, Role);
				break;
			}
			case ENetRole::ROLE_AutonomousProxy:
			{
				jnpCheckf(FJoltNetworkPredictionDriver<ModelDef>::HasSimulation(), TEXT("AP must have Simulation."));
				jnpCheckf(FJoltNetworkPredictionDriver<ModelDef>::HasInput(), TEXT("AP sim doesn't have Input?"));

				BindClientNetRecv_Fixed<TFixedTickReplicator_AP<ModelDef>>(ID, RepProxies.AutonomousProxy, DataStore, Role);
				BindClientNetRecv_Fixed<TFixedTickReplicator_SP<ModelDef>>(ID, RepProxies.SimulatedProxy, DataStore, Role);

				BindNetSend_Fixed<TFixedTickReplicator_Server<ModelDef>>(ID, RepProxies.ServerRPC, DataStore);
				BindReplayNetSendRecv_Fixed<TFixedTickReplicator_SP<ModelDef>>(ID, RepProxies.Replay, DataStore, Role);
				
				// Poll local input and send to server services
				ServiceMask |= EJoltNetworkPredictionService::FixedInputLocal;
				ServiceMask |= EJoltNetworkPredictionService::FixedServerRPC;
				break;
			}
			case ENetRole::ROLE_SimulatedProxy:
			{
				BindClientNetRecv_Fixed<TFixedTickReplicator_AP<ModelDef>>(ID, RepProxies.AutonomousProxy, DataStore, Role);
				BindClientNetRecv_Fixed<TFixedTickReplicator_SP<ModelDef>>(ID, RepProxies.SimulatedProxy, DataStore, Role);

				BindReplayNetSendRecv_Fixed<TFixedTickReplicator_SP<ModelDef>>(ID, RepProxies.Replay, DataStore, Role);
				break;
			}
		};

		// Authority vs Non-Authority services
		if (Role == ROLE_Authority)
		{
			if (FJoltNetworkPredictionDriver<ModelDef>::HasSimulation())
			{
				ServiceMask |= EJoltNetworkPredictionService::FixedTick;
				ServiceMask |= EJoltNetworkPredictionService::FixedPhysics;
				ServiceMask |= EJoltNetworkPredictionService::FixedFinalize;

				if (FJoltNetworkPredictionDriver<ModelDef>::HasFinalizeSmoothingFrame && Settings.bEnableFixedTickSmoothing)
				{
					ServiceMask |= EJoltNetworkPredictionService::FixedSmoothing;
				}
				
			}
		}
		else
		{
			// These services depend on NetworkLOD for non authority cases
			switch(Config.NetworkLOD)
			{
			case EJoltNetworkLOD::ForwardPredict:
				ServiceMask |= EJoltNetworkPredictionService::FixedRollback;
				ServiceMask |= EJoltNetworkPredictionService::FixedPhysics;
				ServiceMask |= EJoltNetworkPredictionService::FixedPhysicsRollback;

				if (FJoltNetworkPredictionDriver<ModelDef>::HasSimulation())
				{
					ServiceMask |= EJoltNetworkPredictionService::FixedTick;
					ServiceMask |= EJoltNetworkPredictionService::FixedFinalize;

					if (FJoltNetworkPredictionDriver<ModelDef>::HasFinalizeSmoothingFrame && Settings.bEnableFixedTickSmoothing)
					{
						ServiceMask |= EJoltNetworkPredictionService::FixedSmoothing;
					}
				}
				
				break;

			case EJoltNetworkLOD::Interpolated:
				ServiceMask |= EJoltNetworkPredictionService::FixedInterpolate;
				break;
			}
		}
	}

	// Net Cues: set which replicated cues we should accept based on if we are FP or interpolated
	if (Role != ROLE_Authority)
	{
		if (EnumHasAnyFlags(ServiceMask, EJoltNetworkPredictionService::FixedInterpolate | EJoltNetworkPredictionService::IndependentInterpolate))
		{
			InstanceData.CueDispatcher->SetReceiveReplicationTarget(EJoltNetSimCueReplicationTarget::Interpolators);
		}
		else
		{
			InstanceData.CueDispatcher->SetReceiveReplicationTarget( Role == ROLE_AutonomousProxy ? EJoltNetSimCueReplicationTarget::AutoProxy : EJoltNetSimCueReplicationTarget::SimulatedProxy);
		}
	}

	// Register with selected services
	if (!bLockServices)
	{
		Services.RegisterInstance<ModelDef>(ID, InstanceData, ServiceMask);
	}
	else
	{
		/// Keep track of the deferred register call so we can clean it up when Unregistering in case it's still around
		FDelegateHandle& DeferredRegisterHandle = DataStore->DeferredRegisterHandle.FindOrAdd(ID);
        
		DeferredRegisterHandle = DeferredServiceConfigDelegate.AddLambda([ID, ServiceMask](UJoltNetworkPredictionWorldManager* Manager)
		{
			TJoltModelDataStore<ModelDef>* DataStore = Manager->Services.GetDataStore<ModelDef>();
			TInstanceData<ModelDef>* InstanceData = DataStore->Instances.Find(ID);
			if (InstanceData)
			{
				Manager->Services.RegisterInstance<ModelDef>(ID, *InstanceData, ServiceMask);
			}

			DataStore->DeferredRegisterHandle.Remove(ID);
		});
	}
	
	// Call into driver to seed initial state if this is a new instance
	if (bNewInstance)
	{
		UE_JNP_TRACE_SIM_CREATED(ID, InstanceData.Info.Driver, ModelDef);
		if (FJoltNetworkPredictionDriver<ModelDef>::HasNpState())
		{
			FJoltNetworkPredictionDriver<ModelDef>::InitializeSimulationState(InstanceData.Info.Driver, InstanceData.Info.View);
		}
	}
	else if (PrevPendingFrame != InstanceData.Info.View->PendingFrame)
	{
		// Not a new instance but PendingFrame changed, so copy contents from previous PendingFrame
		if (FJoltNetworkPredictionDriver<ModelDef>::HasNpState())
		{
			FrameData.Buffer[InstanceData.Info.View->PendingFrame] = FrameData.Buffer[PrevPendingFrame];
		}
	}

	UE_JNP_TRACE_SIM_CONFIG(ID.GetTraceID(), Role, bHasNetConnection, Archetype, Config, ServiceMask);
}

// ---------------------------------------------------------------------------------------
//	Server Receive Bindings
//		-Binds RepProxy serialize lambda to a Replicator function
//		-2 versions for Fixed/Independent
//		-Binds directly to TFixedTickReplicator_Server/TIndependentTickReplicator_Server
// ---------------------------------------------------------------------------------------
template<typename ModelDef>
void UJoltNetworkPredictionWorldManager::BindServerNetRecv_Fixed(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore)
{
	if (!RepProxy)
		return;

	const int32 ServerRecvIdx = DataStore->ServerRecv.GetIndex(ID);

	TFixedTickReplicator_Server<ModelDef>::SetNumInputsPerSend(Settings.FixedTickInputSendCount);

	TJoltServerRecvData_Fixed<ModelDef>& ServerRecvData = DataStore->ServerRecv.GetByIndexChecked(ServerRecvIdx);
	ServerRecvData.TraceID = ID.GetTraceID();
	ServerRecvData.ID = ID;
	FJoltFixedTickState* TickState = &this->FixedTickState;
	RepProxy->NetSerializeFunc = [DataStore, ServerRecvIdx, TickState](const FJoltNetSerializeParams& P)
	{
		jnpEnsure(P.Ar.IsLoading());
		TJoltServerRecvData_Fixed<ModelDef>& ServerRecvData = DataStore->ServerRecv.GetByIndexChecked(ServerRecvIdx);
		
		UE_JNP_TRACE_SIM(ServerRecvData.TraceID);
		TFixedTickReplicator_Server<ModelDef>::NetRecv(P, ServerRecvData, DataStore, TickState);
	};
}

template<typename ModelDef>
void UJoltNetworkPredictionWorldManager::BindServerNetRecv_Independent(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore)
{
	if (!RepProxy)
		return;

	const int32 ServerRecvIdx = DataStore->ServerRecv_IndependentTick.GetIndex(ID);

	TIndependentTickReplicator_Server<ModelDef>::SetNumInputsPerSend(Settings.IndependentTickInputSendCount);

	TJoltServerRecvData_Independent<ModelDef>& ServerRecvData = DataStore->ServerRecv_IndependentTick.GetByIndexChecked(ServerRecvIdx);
	ServerRecvData.TraceID = ID.GetTraceID();
	ServerRecvData.InstanceIdx = DataStore->Instances.GetIndex(ID);
	ServerRecvData.FramesIdx = DataStore->Frames.GetIndex(ID);
	ServerRecvData.PendingFrame = 0;
	ServerRecvData.TotalSimTimeMS = 0;
	ServerRecvData.UnspentTimeMS = 0.f;
	ServerRecvData.LastConsumedFrame = INDEX_NONE;
	ServerRecvData.LastRecvFrame = INDEX_NONE;

	RepProxy->NetSerializeFunc = [DataStore, ServerRecvIdx](const FJoltNetSerializeParams& P)
	{
		jnpEnsure(P.Ar.IsLoading());		
		TJoltServerRecvData_Independent<ModelDef>& ServerRecvData = DataStore->ServerRecv_IndependentTick.GetByIndexChecked(ServerRecvIdx);

		UE_JNP_TRACE_SIM(ServerRecvData.TraceID);
		TIndependentTickReplicator_Server<ModelDef>::NetRecv(P, ServerRecvData, DataStore);
	};
}

// ---------------------------------------------------------------------------------------
//	Client Receive Bindings
//		-Binds RepProxy serialize lambda to a Replicator function
//		-2 versions for Fixed/Independent
//		-Bind to either AP, or SP versions based on ReplicatorType
//			TFixedTickReplicator_AP
//			TFixedTickReplicator_SP
//			TIndependentTickReplicator_AP
//			TIndependentTickReplicator_SP
// ---------------------------------------------------------------------------------------

template<typename ReplicatorType, typename ModelDef>
void UJoltNetworkPredictionWorldManager::BindClientNetRecv_Fixed(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore, ENetRole NetRole)
{
	if (!RepProxy) 
		return;

	TFixedTickReplicator_Server<ModelDef>::SetNumInputsPerSend(Settings.FixedTickInputSendCount);

	const int32 ClientRecvIdx = DataStore->ClientRecv.GetIndex(ID);
	JnpResizeAndSetBit(DataStore->ClientRecvBitMask, ClientRecvIdx, false);

	TJoltClientRecvData<ModelDef>& ClientRecvData = DataStore->ClientRecv.GetByIndexChecked(ClientRecvIdx);
	InitClientRecvData<ModelDef>(ID, ClientRecvData, DataStore, NetRole);

	FJoltFixedTickState* TickState = &this->FixedTickState;
	RepProxy->NetSerializeFunc = [DataStore, ClientRecvIdx, TickState](const FJoltNetSerializeParams& P)
	{
		jnpEnsure(P.Ar.IsLoading());
		DataStore->ClientRecvBitMask[ClientRecvIdx] = true;
		auto& ClientRecvData = DataStore->ClientRecv.GetByIndexChecked(ClientRecvIdx);

		UE_JNP_TRACE_SIM(ClientRecvData.TraceID);
		ReplicatorType::NetRecv(P, ClientRecvData, DataStore, TickState);
	};
}

template<typename ReplicatorType, typename ModelDef>
void UJoltNetworkPredictionWorldManager::BindClientNetRecv_Independent(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore, ENetRole NetRole)
{
	if (!RepProxy) 
		return;

	TIndependentTickReplicator_Server<ModelDef>::SetNumInputsPerSend(Settings.IndependentTickInputSendCount);

	const int32 ClientRecvIdx = DataStore->ClientRecv.GetIndex(ID);
	JnpResizeAndSetBit(DataStore->ClientRecvBitMask, ClientRecvIdx, false);

	TJoltClientRecvData<ModelDef>& ClientRecvData = DataStore->ClientRecv.GetByIndexChecked(ClientRecvIdx);
	InitClientRecvData<ModelDef>(ID, ClientRecvData, DataStore, NetRole);

	FJoltVariableTickState* TickState = &this->VariableTickState;
	RepProxy->NetSerializeFunc = [DataStore, ClientRecvIdx, TickState](const FJoltNetSerializeParams& P)
	{
		jnpEnsure(P.Ar.IsLoading());
		DataStore->ClientRecvBitMask[ClientRecvIdx] = true;
		auto& ClientRecvData = DataStore->ClientRecv.GetByIndexChecked(ClientRecvIdx);

		UE_JNP_TRACE_SIM(ClientRecvData.TraceID);
		ReplicatorType::NetRecv(P, ClientRecvData, DataStore, TickState);
	};
}

// ---------------------------------------------------------------------------------------
//	Send Bindings
//		-Binds RepProxy serialize lambda to a Replicator function
//		-3 versions: Fixed, Independent (Local Tick), Independent (Remote Ticked)
//		-Bind to either Server, AP, or SP versions based on ReplicatorType:
//			TFixedTickReplicator_Server
//			TIndependetTickReplicator_Server
//			TFixedTickReplicator_AP
//			TFixedTickReplicator_SP
//			TIndependentTickReplicator_AP
//			TIndependentTickReplicator_SP
// ---------------------------------------------------------------------------------------

template<typename ReplicatorType, typename ModelDef>
void UJoltNetworkPredictionWorldManager::BindNetSend_Fixed(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore)
{
	if (!RepProxy) 
		return;

	// Tick is based on local FixedTickState
	FJoltFixedTickState* TickState = &this->FixedTickState;
	RepProxy->NetSerializeFunc = [ID, DataStore, TickState](const FJoltNetSerializeParams& P)
	{
		jnpEnsure(P.Ar.IsSaving());
		UE_JNP_TRACE_SIM(ID.GetTraceID());
		ReplicatorType::NetSend(P, ID, DataStore, TickState);
	};
}

template<typename ReplicatorType, typename ModelDef>
void UJoltNetworkPredictionWorldManager::BindNetSend_IndependentLocal(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore)
{
	if (!RepProxy) 
		return;

	// Tick is based on local VariableTickState
	FJoltVariableTickState* TickState = &this->VariableTickState;
	RepProxy->NetSerializeFunc = [ID, DataStore, TickState](const FJoltNetSerializeParams& P)
	{
		jnpEnsure(P.Ar.IsSaving());
		UE_JNP_TRACE_SIM(ID.GetTraceID());
		ReplicatorType::NetSend(P, ID, DataStore, TickState);
	};
}

template<typename ReplicatorType, typename ModelDef>
void UJoltNetworkPredictionWorldManager::BindNetSend_IndependentRemote(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore)
{
	if (!RepProxy) 
		return;
	
	// Tick is based on ServerRecv_IndependentTick data
	const int32 ServerRecvIdx = DataStore->ServerRecv_IndependentTick.GetIndex(ID);
	RepProxy->NetSerializeFunc = [ID, this, DataStore, ServerRecvIdx](const FJoltNetSerializeParams& P)
	{
		jnpEnsureSlow(P.Ar.IsSaving());
		TJoltServerRecvData_Independent<ModelDef>& ServerRecv = DataStore->ServerRecv_IndependentTick.GetByIndexChecked(ServerRecvIdx);
		UE_JNP_TRACE_SIM(ID.GetTraceID());
		ReplicatorType::NetSend(P, ID, DataStore, ServerRecv, &this->VariableTickState);
	};
}

// ---------------------------------------------------------------------------------------
//	Replay Bindings
//		-Binds RepProxy serialize lambda to a Replicator function
//		-3 versions: Fixed, Independent (Local Tick), Independent (Remote Ticked)
// ---------------------------------------------------------------------------------------

template<typename ReplicatorType, typename ModelDef>
void UJoltNetworkPredictionWorldManager::BindReplayNetSendRecv_Fixed(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore, ENetRole NetRole)
{
	if (!RepProxy)
	{
		return;
	}

	const int32 ClientRecvIdx = DataStore->ClientRecv.GetIndex(ID);
	JnpResizeAndSetBit(DataStore->ClientRecvBitMask, ClientRecvIdx, false);

	TJoltClientRecvData<ModelDef>& ClientRecvData = DataStore->ClientRecv.GetByIndexChecked(ClientRecvIdx);
	InitClientRecvData<ModelDef>(ID, ClientRecvData, DataStore, NetRole);

	FJoltFixedTickState* TickState = &this->FixedTickState;
	RepProxy->NetSerializeFunc = [ID, DataStore, ClientRecvIdx, TickState](const FJoltNetSerializeParams& P)
	{
		if (P.Ar.IsLoading()) // Receiving replay data
		{
			DataStore->ClientRecvBitMask[ClientRecvIdx] = true;
			auto& ClientRecvData = DataStore->ClientRecv.GetByIndexChecked(ClientRecvIdx);

			UE_JNP_TRACE_SIM(ClientRecvData.TraceID);
			ReplicatorType::NetRecv(P, ClientRecvData, DataStore, TickState);
		}
		else // Sending replay data
		{
			UE_JNP_TRACE_SIM(ID.GetTraceID());
			ReplicatorType::NetSend(P, ID, DataStore, TickState);
		}
	};
}


template<typename ReplicatorType, typename ModelDef>
void UJoltNetworkPredictionWorldManager::BindReplayNetSendRecv_IndependentLocal(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore, ENetRole NetRole)
{
	if (!RepProxy)
	{
		return;
	}

	const int32 ClientRecvIdx = DataStore->ClientRecv.GetIndex(ID);
	JnpResizeAndSetBit(DataStore->ClientRecvBitMask, ClientRecvIdx, false);

	TJoltClientRecvData<ModelDef>& ClientRecvData = DataStore->ClientRecv.GetByIndexChecked(ClientRecvIdx);
	InitClientRecvData<ModelDef>(ID, ClientRecvData, DataStore, NetRole);

	FJoltVariableTickState* TickState = &this->VariableTickState;
	RepProxy->NetSerializeFunc = [ID, DataStore, ClientRecvIdx, TickState](const FJoltNetSerializeParams& P)
	{
		if (P.Ar.IsLoading())	// Receiving replay data
		{
			DataStore->ClientRecvBitMask[ClientRecvIdx] = true;
			auto& ClientRecvData = DataStore->ClientRecv.GetByIndexChecked(ClientRecvIdx);

			UE_JNP_TRACE_SIM(ClientRecvData.TraceID);
			ReplicatorType::NetRecv(P, ClientRecvData, DataStore, TickState);
		}
		else // Sending replay data
		{
			UE_JNP_TRACE_SIM(ID.GetTraceID());
			ReplicatorType::NetSend(P, ID, DataStore, TickState);
		}
	};
}


template<typename ReplicatorType, typename ModelDef>
void UJoltNetworkPredictionWorldManager::BindReplayNetSendRecv_IndependentRemote(FJoltNetworkPredictionID ID, FJoltReplicationProxy* RepProxy, TJoltModelDataStore<ModelDef>* DataStore, ENetRole NetRole)
{
	if (!RepProxy)
	{
		return;
	}

	const int32 ClientRecvIdx = DataStore->ClientRecv.GetIndex(ID);
	JnpResizeAndSetBit(DataStore->ClientRecvBitMask, ClientRecvIdx, false);

	TJoltClientRecvData<ModelDef>& ClientRecvData = DataStore->ClientRecv.GetByIndexChecked(ClientRecvIdx);
	InitClientRecvData<ModelDef>(ID, ClientRecvData, DataStore, NetRole);

	FJoltVariableTickState* TickState = &this->VariableTickState;

	const int32 ServerRecvIdx = DataStore->ServerRecv_IndependentTick.GetIndex(ID);

	RepProxy->NetSerializeFunc = [ID, DataStore, ClientRecvIdx, TickState, ServerRecvIdx](const FJoltNetSerializeParams& P)
	{
		if (P.Ar.IsLoading())	// Receiving replay data
		{
			DataStore->ClientRecvBitMask[ClientRecvIdx] = true;
			auto& ClientRecvData = DataStore->ClientRecv.GetByIndexChecked(ClientRecvIdx);

			UE_JNP_TRACE_SIM(ClientRecvData.TraceID);
			ReplicatorType::NetRecv(P, ClientRecvData, DataStore, TickState);
		}
		else // Sending replay data
		{
			TJoltServerRecvData_Independent<ModelDef>& ServerRecv = DataStore->ServerRecv_IndependentTick.GetByIndexChecked(ServerRecvIdx);
			UE_JNP_TRACE_SIM(ID.GetTraceID());
			ReplicatorType::NetSend(P, ID, DataStore, ServerRecv, TickState);
		}
	};
}

// -----------------

template<typename ModelDef>
void UJoltNetworkPredictionWorldManager::InitClientRecvData(FJoltNetworkPredictionID ID, TJoltClientRecvData<ModelDef>& ClientRecvData, TJoltModelDataStore<ModelDef>* DataStore, ENetRole NetRole)
{
	ClientRecvData.ID = ID; //Added By Kai For Delta Serialization Support
	ClientRecvData.TraceID = ID.GetTraceID();
	ClientRecvData.InstanceIdx = DataStore->Instances.GetIndexChecked(ID);
	ClientRecvData.FramesIdx = DataStore->Frames.GetIndexChecked(ID);
	ClientRecvData.NetRole = NetRole;
}
