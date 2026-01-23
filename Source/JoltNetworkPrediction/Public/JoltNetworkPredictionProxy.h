// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JoltNetworkPredictionID.h"
#include "JoltNetworkPredictionStateView.h"
#include "JoltNetworkPredictionConfig.h"
#include "JoltNetworkPredictionWorldManager.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/EngineTypes.h"
#include "JoltNetworkPredictionProxy.generated.h"

// -------------------------------------------------------------------------------------------------
//	FJoltNetworkPredictionProxy
//
//	Proxy struct for interfacing with the NetworkPrediction system. 
//
//	Call Init<YourModelDef>(...) to bind to the system. Config(...) will change the current settings.
//	Include NetworkPredictionProxyInit.h in your cpp file to do this. (Don't include it in your class's header).
//
// -------------------------------------------------------------------------------------------------

class UJoltNetworkPredictionPlayerControllerComponent;
struct FJoltReplicationProxySet;
class UJoltNetworkPredictionWorldManager;

UENUM()
enum class EJoltNetworkPredictionStateRead
{
	// The authoritative, networked state values.
	Simulation,
	// The local "smoothed" or "corrected" state values. If no explicit presentation value is set, Simulation value is implied.
	// Presentation values never feed back into the simulation.
	Presentation,
};

USTRUCT()
struct FJoltNetworkPredictionProxy
{
	GENERATED_BODY();

	// Parameter struct to be used with the Init function overload below, when a UWorld isn't available, such as in unit tests.
	template<typename ModelDef>
	struct FInitParams
	{
		UJoltNetworkPredictionWorldManager* WorldManager;
		ENetMode Mode;
		const FJoltReplicationProxySet& RepProxies;
		typename ModelDef::Simulation* Simulation = nullptr;
		typename ModelDef::Driver* Driver = nullptr;
	};

	// The init function that you need to call. This is defined in NetworkPredictionProxyInit.h (which should only be included by your .cpp file)
	template<typename ModelDef>
	void Init(UWorld* World, const FJoltReplicationProxySet& RepProxies, typename ModelDef::Simulation* Simulation=nullptr, typename ModelDef::Driver* Driver=nullptr);

	template<typename ModelDef>
	void Init(const FInitParams<ModelDef>& Params);

	// When network role changes, initializes role storage and logic controller
	void InitForNetworkRole(ENetRole Role, bool bHasNetConnection,UJoltNetworkPredictionPlayerControllerComponent* RPCHandler)
	{
		CachedNetRole = Role;
		bCachedHasNetConnection = bHasNetConnection;
		CachedRPCHandler = RPCHandler;
		if (ConfigFunc)
		{
			ConfigFunc(this, FJoltNetworkPredictionID(), EConfigAction::UpdateConfigWithDefault);
		}
	}

	// Should only be called on the authority. Changes what how this instance is allowed to be configured
	void SetArchetype(const FJoltNetworkPredictionInstanceArchetype& Archetype, const FJoltNetworkPredictionInstanceConfig& Config)
	{
		ArchetypeDirtyCount++;
		Configure(Config);
	}

	// Call to change local configuration of proxy. Not networked.
	void Configure(const FJoltNetworkPredictionInstanceConfig& Config)
	{
		CachedConfig = Config;
		if (ConfigFunc)
		{
			ConfigFunc(this, FJoltNetworkPredictionID(), EConfigAction::None);
		}
	}

	// Unregisters from NetworkPrediction System
	void EndPlay()
	{
		if (ConfigFunc)
		{
			ConfigFunc(this, FJoltNetworkPredictionID(), EConfigAction::EndPlay);
		}
	}

	// --------------------------------------------------------------------------------------------------------------------------
	//	Read/Write access for the current states (these are the states that will be used as input into next simulation frame
	// --------------------------------------------------------------------------------------------------------------------------

	// Returns pending InputCmd. (Note there is no Presentation version of InputCmds)
	template<typename TInputCmd>
	const TInputCmd* ReadInputCmd() const
	{
		return static_cast<TInputCmd*>(View.PendingInputCmd);
	}

	// Returns Presentation SyncState by default, if it is set. Otherwise returns pending Simulation value.
	template<typename TSyncState>
	const TSyncState* ReadSyncState(EJoltNetworkPredictionStateRead ReadType = EJoltNetworkPredictionStateRead::Presentation) const
	{
		if (ReadType == EJoltNetworkPredictionStateRead::Presentation && View.PresentationSyncState)
		{
			return static_cast<TSyncState*>(View.PresentationSyncState);
		}

		return static_cast<TSyncState*>(View.PendingSyncState);
	}

	// Returns Prev Presentation SyncState by default, if it is set. Otherwise returns pending Simulation value.
	template<typename TSyncState>
	const TSyncState* ReadPrevPresentationSyncState() const
	{
		
		if (View.PrevPresentationSyncState)
		{
			return static_cast<TSyncState*>(View.PrevPresentationSyncState);
		}
		
		return static_cast<TSyncState*>(View.PendingSyncState);
	}

	// Returns Presentation AuxState by default, if it is set. Otherwise returns pending Simulation value.
	template<typename TAuxState>
	const TAuxState* ReadAuxState(EJoltNetworkPredictionStateRead ReadType = EJoltNetworkPredictionStateRead::Presentation) const
	{
		if (ReadType == EJoltNetworkPredictionStateRead::Presentation && View.PresentationAuxState)
		{
			return static_cast<TAuxState*>(View.PresentationAuxState);
		}
		
		return static_cast<TAuxState*>(View.PendingAuxState);
	}

	// Returns Prev Presentation AuxState by default, if it is set. Otherwise returns pending Simulation value.
	template<typename TAuxState>
	const TAuxState* ReadPrevPresentationAuxState() const
	{
		if (View.PrevPresentationAuxState)
		{
			return static_cast<TAuxState*>(View.PrevPresentationAuxState);
		}
		
		return static_cast<TAuxState*>(View.PendingAuxState);
	}
	template<typename ModelDef,typename TSyncState>
	bool ReadStateAtTime(float TimeMS , TSyncState& OutState)
	{
		
		if (ID < 0)
		{
			return false;
		}
		return WorldManager->GetSyncStateAtTime<ModelDef,TSyncState>(ID,TimeMS,OutState);
	}
	

	// Writes - must include NetworkPredictionProxyWrite.h in places that call this
	// Note that writes are implicitly done on the simulation state. It is not valid to modify the presentation value out of band.
	template<typename TInputCmd>
	const TInputCmd* WriteInputCmd(TFunctionRef<void(TInputCmd&)> WriteFunc, const FAnsiStringView& TraceMsg=FAnsiStringView());

	template<typename TSyncState>
	const TSyncState* WriteSyncState(TFunctionRef<void(TSyncState&)> WriteFunc, const FAnsiStringView& TraceMsg=FAnsiStringView());
	
	template<typename TSyncState>
	const TSyncState* WritePresentationSyncState(TFunctionRef<void(TSyncState&)> WriteFunc, const FAnsiStringView& TraceMsg=FAnsiStringView());
	
	template<typename TSyncState>
	const TSyncState* WritePrevPresentationSyncState(TFunctionRef<void(TSyncState&)> WriteFunc, const FAnsiStringView& TraceMsg=FAnsiStringView());

	template<typename TAuxState>
	const TAuxState* WriteAuxState(TFunctionRef<void(TAuxState&)> WriteFunc, const FAnsiStringView& TraceMsg=FAnsiStringView());

	template<typename TAuxState>
	const TAuxState* WritePresentationAuxState(TFunctionRef<void(TAuxState&)> WriteFunc, const FAnsiStringView& TraceMsg=FAnsiStringView());

	template<typename TAuxState>
	const TAuxState* WritePrevPresentationAuxState(TFunctionRef<void(TAuxState&)> WriteFunc, const FAnsiStringView& TraceMsg=FAnsiStringView());
	

	float GetFixedInterpolationTime() const
	{
		if (View.InterpolationTimeMS)
		{
			return View.LatestInterpTimeMS;
		}
		if (WorldManager)
		{
			return WorldManager->GetFixedTickState().Interpolation.InterpolatedTimeMS;
		}
		return 0.0f;
	}
	// ------------------------------------------------------------------------------------

	FJoltNetSimCueDispatcher* GetCueDispatcher() const
	{
		return View.CueDispatcher;
	}

	const FJoltNetworkPredictionInstanceConfig& GetConfig() const
	{
		return CachedConfig;
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		uint32 RawID=(uint32)ID;
		Ar.SerializeIntPacked(RawID);
		CachedArchetype.NetSerialize(Ar);

		if (Ar.IsLoading())
		{
			if ((int32)ID != RawID)
			{
				if (ConfigFunc)
				{
					// We've already been init, so have to go through ConfigFunc to remap new id
					FJoltNetworkPredictionID NewID(RawID, ID.GetTraceID());
					ConfigFunc(this, NewID, EConfigAction::UpdateConfigWithDefault);
				}
				else
				{
					// haven't been init yet so just set the replicated ID so we don't create a client side one
					ID = FJoltNetworkPredictionID(RawID);
				}
			}
			else
			{
				// Archetype change, call ConfigFunc but don't change ID
				ConfigFunc(this, FJoltNetworkPredictionID(), EConfigAction::UpdateConfigWithDefault);
			}
		}
		return true;
	}

	bool Identical(const FJoltNetworkPredictionProxy* Other, uint32 PortFlags) const
	{
		return ID == Other->ID && ArchetypeDirtyCount == Other->ArchetypeDirtyCount;
	}

	// ------------------------------------------------------------------------------------

	int32 GetPendingFrame() const { return View.PendingFrame; }
	int32 GetTotalSimTimeMS() const { return View.SimTimeMS; }
	ENetRole GetCachedNetRole() const { return CachedNetRole; }
	bool GetCachedHasNetConnection() const { return bCachedHasNetConnection; }
	int32 GetID() const {return ID;}
	UJoltNetworkPredictionPlayerControllerComponent* GetCachedRPCHandler() const { return CachedRPCHandler; }

private:

	// Allows ConfigFunc to be invoked to "do a thing" instead of set a new config/id.
	// This is useful because ConfigFunc can make the untemplated caller -> ModelDef jump.
	enum class EConfigAction : uint8
	{
		None,
		EndPlay,
		UpdateConfigWithDefault,
		TraceInput,
		TraceSync,
		TraceAux
	};

	void TraceViaConfigFunc(EConfigAction Action);

	FJoltNetworkPredictionID ID;
	FJoltNetworkPredictionStateView View;

	ENetRole CachedNetRole = ROLE_None;
	bool bCachedHasNetConnection;
	FJoltNetworkPredictionInstanceConfig CachedConfig;
	FJoltNetworkPredictionInstanceArchetype CachedArchetype;
	uint8 ArchetypeDirtyCount = 0;

	TFunction<void(FJoltNetworkPredictionProxy* const, FJoltNetworkPredictionID NewID, EConfigAction Action)>	ConfigFunc;

	UPROPERTY()
	TObjectPtr<UJoltNetworkPredictionWorldManager> WorldManager = nullptr;

	UPROPERTY()
	TObjectPtr<UJoltNetworkPredictionPlayerControllerComponent> CachedRPCHandler = nullptr;
};

template<>
struct TStructOpsTypeTraits<FJoltNetworkPredictionProxy> : public TStructOpsTypeTraitsBase2<FJoltNetworkPredictionProxy>
{
	enum
	{
		WithNetSerializer = true,
		WithIdentical = true,
	};
};
