// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "JoltNetworkPredictionCues.h"
#include "JoltNetworkPredictionConditionalState.h"
#include "JoltNetworkPredictionSettings.h"
#include "JoltNetworkPredictionDebug.h"
#include "JoltNetworkPredictionStateTypes.h"
#include "JoltNetworkPredictionStateView.h"
#include "Components/PrimitiveComponent.h"

class UJoltNetworkPredictionPlayerControllerComponent;
struct FJoltNetSerializeParams;

struct FBodyInstance;

namespace Chaos
{
	class FRewindData;
};

// The "driver" represents the UE side of the NetworkPrediction System. Typically the driver will be an Unreal Actor or ActorComponent.
// This driver class is defined in the ModelDef (your version of FJoltNetworkPredictionModelDef). For example, AActor or AMyPawn class.
// While the Simulation class is agnostic to all of this, the driver is responsible for specifying exactly how things should work, 
// generating input and handling output from the system.
//
// Its worth pointing out that a new ModelDef with a new Driver class is only required when you want to actually change behavior at the driver
// level. For example, if you define a ModelDef {MySimulation, AMyPawn_Base} you can still use this ModelDef with both AMyPawn_Player, AMyPawn_AI.
// You would only need to create a ModelDef {MySimulation, AMyPawn_Player} if your player class wanted to customize Driver calls for example.
// (And even then: making the Driver functions virtual on AMyPawn_Base would be enough, at the cost of virtual function calls).
//
// FJoltNetworkPredictionDriver is how calls to the driver are made. FJoltNetworkPredictionDriverBase is a default implementation that will be used.
// You can customize any of these calls by specializing FJoltNetworkPredictionDriver to your ModelDef type. If you do this, you should inherit from 
// FJoltNetworkPredictionDriverBase<YourModelDef> or FJoltNetworkPredictionDriver<ParentModelDef> if you are extending an existing ModelDef.
// (otherwise you will need to implement every required function yourself).
//
//
// The default implementations can be broken down into a few categories:
//
//	1.	Simple stuff like ::GetDebugString() - we provide generic implemenations for AActor and UActorComponent. In general you won't need 
//		to implement these yourself unless you want to include extra information in the debug string that the system prints to logs.
//
//	2.	Calls that get forwarded to the Driver itself. For example ::InitializeSimulationState(FSyncState*, FAuxState*). We can't provide
//		a generic implementation because the state type is defined by the user. We forward this to the Driver because that is ultimately
//		where the initial simulation state is seeded from. Defining InitializeSimulationState on the Driver itself is the simplest way
//		of doing this and will make the most sense to anyone looking at the Driver class.
//
//		However there may be cases when this is not an option. For example if you want to create a Simulation in this system that can be driven
//		by an AActor. You wouldn't want to modify AActor itself to implement InitializeSimulationState. In those cases, you can specialize
//		FJoltNetworkPredictionDriver<YourModelDef>::InitializeSimulationState(AActor*, FSyncState*, FAuxState*) and implement it there.
//
//	3.	Calls that get forwarded to the underlying StateTypes. For example ::ShouldReconcile(FSyncState*, FSyncState*). The default 
//		implementation for these calls will get forwarded to member functions on the state type itself. This allows the user struct
//		to define the default behavior while still giving the Driver type the option to override.
//
//
//	High level goals: maximize non-intrusive extendability, shield users from templated boiler plate where possible.

template<typename ModelDef>
struct FJoltNetworkPredictionDriver;

template<typename ModelDef>
struct FJoltNetworkPredictionDriverBase
{
	using DriverType = typename ModelDef::Driver;
	using Simulation = typename ModelDef::Simulation;
	using StateTypes = typename ModelDef::StateTypes;
	using InputType = typename StateTypes::InputType;
	using SyncType = typename StateTypes::SyncType;
	using AuxType = typename StateTypes::AuxType;

	static constexpr bool HasNpState() { return !std::is_void_v<InputType> || !std::is_void_v<SyncType> || !std::is_void_v<AuxType>; }
	static constexpr bool HasDriver() { return !std::is_void_v<DriverType>; }
	static constexpr bool HasSimulation() { return !std::is_void_v<Simulation>; }
	static constexpr bool HasInput() { return !std::is_void_v<InputType>; }
	

	// Defines what the ModelDef can do. This is a compile time thing only.
	static constexpr FJoltNetworkPredictionModelDefCapabilities GetCapabilities()
	{
		FJoltNetworkPredictionModelDefCapabilities Capabilities;

		if (HasSimulation() == false)
		{
			// We have nothing to tick, so no SimExtrapolation or ForwardPrediction
			Capabilities.FixedNetworkLODs.AP = EJoltNetworkLOD::Interpolated;
			Capabilities.FixedNetworkLODs.SP = EJoltNetworkLOD::Interpolated;
			Capabilities.IndependentNetworkLODs.AP = EJoltNetworkLOD::Interpolated;
			Capabilities.IndependentNetworkLODs.SP = EJoltNetworkLOD::Interpolated;
		}
		return Capabilities;
	}

	// Defines the default settings for a spawn instance.
	static bool GetDefaultArchetype(FJoltNetworkPredictionInstanceArchetype& Archetype, EJoltNetworkPredictionTickingPolicy PreferredTickingPolicy)
	{
		static constexpr FJoltNetworkPredictionModelDefCapabilities Capabilities = FJoltNetworkPredictionDriver<ModelDef>::GetCapabilities();

		// Use preferred ticking policy if we support it
		if (EnumHasAnyFlags(Capabilities.SupportedTickingPolicies, PreferredTickingPolicy))
		{
			Archetype.TickingMode = PreferredTickingPolicy;
		}
		else
		{
			// else use the one we support (assumes only 2 modes)
			Archetype.TickingMode = Capabilities.SupportedTickingPolicies;
		}
		
		return true;
	}

	// Defines the default config for an instance, given their archetype and Role/NetConnection
	static FJoltNetworkPredictionInstanceConfig GetConfig(const FJoltNetworkPredictionInstanceArchetype& Archetype, const FJoltNetworkPredictionSettings& GlobalSettings, ENetRole Role, bool bHasNetConnection)
	{
		static constexpr FJoltNetworkPredictionModelDefCapabilities Capabilities = FJoltNetworkPredictionDriver<ModelDef>::GetCapabilities();
		FJoltNetworkPredictionInstanceConfig Config;
		switch (Role)
		{
		case ROLE_Authority:
			Config.InputPolicy = bHasNetConnection ? EJoltNetworkPredictionLocalInputPolicy::Passive : EJoltNetworkPredictionLocalInputPolicy::PollPerSimFrame;
			break;
		case ROLE_AutonomousProxy:
			Config.InputPolicy = EJoltNetworkPredictionLocalInputPolicy::PollPerSimFrame;
			Config.NetworkLOD = Archetype.TickingMode == EJoltNetworkPredictionTickingPolicy::Fixed ? GetHighestNetworkLOD(Capabilities.FixedNetworkLODs.AP) : GetHighestNetworkLOD(Capabilities.IndependentNetworkLODs.AP);
			break;
		case ROLE_SimulatedProxy:
			Config.InputPolicy = EJoltNetworkPredictionLocalInputPolicy::Passive;

			// Use preferred SP NetworkLOD if we support it
			const EJoltNetworkLOD CapableLODs = (Archetype.TickingMode == EJoltNetworkPredictionTickingPolicy::Fixed) ? Capabilities.FixedNetworkLODs.SP : Capabilities.IndependentNetworkLODs.SP;
			if (EnumHasAnyFlags(CapableLODs, GlobalSettings.SimulatedProxyNetworkLOD))
			{
				Config.NetworkLOD = GlobalSettings.SimulatedProxyNetworkLOD;
			}
			else
			{
				// Use highest allowed LOD otherwise
				Config.NetworkLOD = GetHighestNetworkLOD(CapableLODs);
			}
			break;
		};

		return Config;
	}

	// -----------------------------------------------------------------------------------------------------------------------------------
	//	Basic string/debug info
	// -----------------------------------------------------------------------------------------------------------------------------------

	static void GetDebugString(AActor* Actor, FStringBuilderBase& Builder)
	{
		Builder.Appendf(TEXT("%s %s"), ModelDef::GetName(), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), Actor->GetLocalRole()));
	}
	
	static void GetDebugString(UActorComponent* ActorComp, FStringBuilderBase& Builder)
	{
		Builder.Appendf(TEXT("%s %s"), ModelDef::GetName(), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), ActorComp->GetOwnerRole()));
	}

	static void GetDebugString(void* NoDriver, FStringBuilderBase& Builder)
	{
		Builder.Append(ModelDef::GetName());
	}

	static void GetDebugStringFull(AActor* Actor, FStringBuilderBase& Builder)
	{
		Builder.Appendf(TEXT("%s. Driver: %s. Role: %s."), ModelDef::GetName(), *Actor->GetPathName(), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), Actor->GetLocalRole()));
	}

	static void GetDebugStringFull(UActorComponent* ActorComp, FStringBuilderBase& Builder)
	{
		Builder.Appendf(TEXT("%s. Driver: %s. Role: %s."), ModelDef::GetName(), *ActorComp->GetPathName(), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), ActorComp->GetOwnerRole()));
	}

	static void GetDebugStringFull(void* NoDriver, FStringBuilderBase& Builder)
	{
		Builder.Append(ModelDef::GetName());
	}

	static void GetTraceString(UActorComponent* ActorComp, FStringBuilderBase& Builder)
	{
		Builder.Appendf(TEXT("%s: %s %s"), ModelDef::GetName(), *ActorComp->GetOwner()->GetName(), *ActorComp->GetName());
	}

	static void GetTraceString(AActor* Actor, FStringBuilderBase& Builder)
	{
		Builder.Appendf(TEXT("%s: %s."), ModelDef::GetName(), *Actor->GetName());
	}
	
	static void GetTraceString(void* NoDriver, FStringBuilderBase& Builder)
	{
		Builder.Append(ModelDef::GetName());
	}

	static FTransform GetDebugWorldTransform(AActor* DriverActor)
	{
		return DriverActor->GetTransform();
	}

	static FTransform GetDebugWorldTransform(UActorComponent* DriverComponent)
	{
		return GetDebugWorldTransform(DriverComponent->GetOwner());
	}

	static FTransform GetDebugWorldTransform(void* NoDriver)
	{
		jnpEnsure(false);
		return FTransform::Identity;
	}

	static FBox GetDebugBoundingBox(AActor* DriverActor)
	{
		return DriverActor->CalculateComponentsBoundingBoxInLocalSpace();
	}

	static FBox GetDebugBoundingBox(UActorComponent* DriverComponent)
	{
		return GetDebugBoundingBox(DriverComponent->GetOwner());
	}

	static FBox GetDebugBoundingBox(void* NoDriver)
	{
		jnpEnsure(false);
		return FBox();
	}

	static void DrawDebugOutline(DriverType* Driver, FColor Color, float Lifetime=0.f)
	{
		JoltNetworkPredictionDebug::DrawDebugOutline(GetDebugWorldTransform(Driver), GetDebugBoundingBox(Driver), Color, Lifetime);
	}

	static void DrawDebugText3D(DriverType* Driver, const TCHAR* Str, FColor Color, float Lifetime=0.f, float ZOffset=50.f)
	{
		FTransform Transform = GetDebugWorldTransform(Driver);
		Transform.AddToTranslation(FVector(0.f, 0.f, ZOffset));
		JoltNetworkPredictionDebug::DrawDebugText3D(Str, Transform , Color, Lifetime);
	}

	//	ToDo @Kai : Use this in all templated functions on the driver. this is needed if driver is pending garbage collection
	// and we try to call these.
	template <typename Type>
	static typename TEnableIf<TIsDerivedFrom<Type, UObject>::Value, bool>::Type IsValidObject(Type* Object)
	{
		return IsValid(Object);
	}

	/// @brief Fallback IsValid call for non-UObjects. Simply tests if the passed in Object is not a nullptr.
	template <typename Type>
	static typename TEnableIf<!TIsDerivedFrom<Type, UObject>::Value, bool>::Type IsValidObject(Type* Object)
	{
		jnpCheckSlow(Object);
		return Object != nullptr;
	}
	// -----------------------------------------------------------------------------------------------------------------------------------
	//	InitializeSimulationState
	//
	//	Set the given simulation state to the current state of the driver.
	//	Called whenever the simulation is fully initialized and is ready to have its initial state set.
	//	This will be called if the instance is reconfigured as well (E.g, went from AP->SP, Interpolated->Forward Predicted, etc).
	//
	//	The default implementation will call InitializeSimulationState(FSyncState*, FAuxState*) on the Driver class itself.
	// -----------------------------------------------------------------------------------------------------------------------------------
	static void InitializeSimulationState(DriverType* Driver, FJoltNetworkPredictionStateView* View)
	{
		jnpCheckSlow(View);
		InitializeSimulationState(Driver, (SyncType*)View->PendingSyncState, (AuxType*)View->PendingAuxState);
	}
	
	static void InitializeSimulationState(DriverType* Driver, SyncType* Sync, AuxType* Aux)
	{
		CallInitializeSimulationStateMemberFunc(Driver, Sync, Aux);
	}
	
	struct CInitializeSimulationStateFuncable
	{
		template <typename InDriverType, typename...>
		auto Requires(InDriverType* Driver, SyncType* Sync, AuxType* Aux) -> decltype(Driver->InitializeSimulationState(Sync, Aux));
	};

	static constexpr bool HasInitializeSimulationState = TModels_V<CInitializeSimulationStateFuncable, DriverType, SyncType, AuxType>;
	
	template<bool HasFunc=HasInitializeSimulationState>
	static typename TEnableIf<HasFunc>::Type CallInitializeSimulationStateMemberFunc(DriverType* Driver, SyncType* Sync, AuxType* Aux)
	{
		jnpCheckSlow(Driver);
		Driver->InitializeSimulationState(Sync, Aux);
	}

	template<bool HasFunc=HasInitializeSimulationState>
	static typename TEnableIf<!HasFunc>::Type CallInitializeSimulationStateMemberFunc(DriverType* Driver, SyncType* Sync, AuxType* Aux)
	{
		jnpCheckf(!HasNpState(), TEXT("No InitializeSimulationState implementation found. Implement DriverType::ProduceInput or ModelDef::ProduceInput"));
	}	

	// -----------------------------------------------------------------------------------------------------------------------------------
	//	ProduceInput
	//
	//	Called on locally controlled simulations prior to ticking a new frame. This is to allow input to be as fresh as possible.
	//  submitting input from an Actor tick would be too late in the frame. NOTE: currently input is sampled/broadcast in PC tick and 
	//	this is still causing a frame of latency in the samples. This will be fixed in the future.
	// -----------------------------------------------------------------------------------------------------------------------------------

	static void ProduceInput(DriverType* Driver, int32 DeltaTimeMS, InputType* InputCmd)
	{
		CallProduceInputMemberFunc(Driver, DeltaTimeMS, InputCmd);
	}
	
	struct CProduceInputMemberFuncable
	{
		template <typename InDriverType, typename...>
		auto Requires(InDriverType* Driver, int32 TimeMS, InputType* InputCmd) -> decltype(Driver->ProduceInput(TimeMS, InputCmd));
	};

	static constexpr bool HasProduceInput = TModels_V<CProduceInputMemberFuncable, DriverType, int32, InputType>;

	template<bool HasFunc=HasProduceInput>
	static typename TEnableIf<HasFunc>::Type CallProduceInputMemberFunc(DriverType* Driver, int32 DeltaTimeMS, InputType* InputCmd)
	{
		jnpCheckSlow(Driver);
		Driver->ProduceInput(DeltaTimeMS, InputCmd);
	}

	template<bool HasFunc=HasProduceInput>
	static typename TEnableIf<!HasFunc>::Type CallProduceInputMemberFunc(DriverType* Driver, int32 DeltaTimeMS, InputType* InputCmd)
	{
		jnpCheckf(false, TEXT("No ProduceInput implementation found. Implement DriverType::ProduceInput or ModelDef::ProduceInput"));
	}

	// -----------------------------------------------------------------------------------------------------------------------------------
	//	FinalizeFrame
	//
	//	Called every engine frame to push the final result of the NetworkPrediction system to the driver
	// -----------------------------------------------------------------------------------------------------------------------------------
	static void FinalizeFrame(DriverType* Driver, const SyncType* SyncState, const AuxType* AuxState)
	{
		CallFinalizeFrameMemberFunc(Driver, SyncState, AuxState);
	}
	
	struct CFinalizeFrameMemberFuncable
	{
		template <typename InDriverType, typename...>
		auto Requires(InDriverType* Driver, const SyncType* S, const AuxType* A) -> decltype(Driver->FinalizeFrame(S, A));
	};

	static constexpr bool HasFinalizeFrame = TModels_V<CFinalizeFrameMemberFuncable, DriverType, SyncType, AuxType>;

	template<bool HasFunc=HasFinalizeFrame>
	static typename TEnableIf<HasFunc>::Type CallFinalizeFrameMemberFunc(DriverType* Driver, const SyncType* SyncState, const AuxType* AuxState)
	{
		jnpCheckSlow(Driver);
		Driver->FinalizeFrame(SyncState, AuxState);
	}

	template<bool HasFunc=HasFinalizeFrame>
	static typename TEnableIf<!HasFunc>::Type CallFinalizeFrameMemberFunc(DriverType* Driver, const SyncType* SyncState, const AuxType* AuxState)
	{
		jnpCheckf(!HasNpState(), TEXT("No FinalizeFrame implementation found. Implement DriverType::FinalizeFrame or ModelDef::FinalizeFrame"));
	}

	// **** Modified By Kai - Smoothing Support ****
	static void GetSmoothingStateDelta(DriverType* Driver,  const SyncType* CurrentSyncState, const AuxType* CurrentAuxState,const SyncType* PrevSyncState, const AuxType* PrevAuxState,  SyncType* DeltaSyncState,  AuxType* DeltaAuxState)
	{
		if (IsValidObject(Driver))
		{
			FJoltNetworkPredictionDriver<ModelDef>::CallGetSmoothingStateDeltaMemberFunc(Driver, CurrentSyncState, CurrentAuxState, PrevSyncState,PrevAuxState,DeltaSyncState,DeltaAuxState);
		}
	}

	struct CGetSmoothingStateDeltaMemberFuncable
	{
		template <typename InDriverType, typename...>
		auto Requires(InDriverType* Driver, const SyncType* Cs, const AuxType* Ca,const SyncType* Ps, const AuxType* Pa,  SyncType* Ds,  AuxType* Da) -> decltype(Driver->GetSmoothingStateDelta(Cs, Ca, Ps,Pa,Ds,Da));
	};

	static constexpr bool HasGetSmoothingStateDelta = TModels_V<CGetSmoothingStateDeltaMemberFuncable, DriverType, SyncType*,  AuxType*, SyncType*,  AuxType*,  SyncType*,  AuxType*>;

	template<bool HasFunc=HasGetSmoothingStateDelta>
	static typename TEnableIf<HasFunc>::Type CallGetSmoothingStateDeltaMemberFunc(DriverType* Driver, const SyncType* CurrentSyncState, const AuxType* CurrentAuxState,const SyncType* PrevSyncState, const AuxType* PrevAuxState,  SyncType* DeltaSyncState,  AuxType* DeltaAuxState)
	{
		if (IsValidObject(Driver))
		{
			Driver->GetSmoothingStateDelta(CurrentSyncState, CurrentAuxState, PrevSyncState,PrevAuxState,DeltaSyncState,DeltaAuxState);
		}
		
	}

	template<bool HasFunc=HasGetSmoothingStateDelta>
	static typename TEnableIf<!HasFunc>::Type CallGetSmoothingStateDeltaMemberFunc(DriverType* Driver, const SyncType* CurrentSyncState, const AuxType* CurrentAuxState,const SyncType* PrevSyncState, const AuxType* PrevAuxState,  SyncType* DeltaSyncState,  AuxType* DeltaAuxState)
	{
		// This isn't a problem but we should probably do something if there is no GetSmoothingDelta function:
	}

	// -----------------------------------------------------------------------------------------------------------------------------------
	//	Get Smoothing State Scaled
	//
	//	Called Every Frame To Get Dela Between Each Tick State And Apply Smoothing For Fixed Tick And Corrections
	//		
	//	
	// -----------------------------------------------------------------------------------------------------------------------------------

	static void GetSmoothingStateScaled(DriverType* Driver, const SyncType* SyncState, const AuxType* AuxState, const float Scale,  SyncType* ScaledSyncState,  AuxType* ScaledAuxState)
	{
		if (IsValidObject(Driver))
		{
			FJoltNetworkPredictionDriver<ModelDef>::CallGetSmoothingStateScaledMemberFunc(Driver, SyncState,AuxState, Scale,ScaledSyncState,ScaledAuxState);
		}
	}

	struct CGetSmoothingStateScaledMemberFuncable
	{
		template <typename InDriverType, typename...>
		auto Requires(InDriverType* Driver, const SyncType* Ss, const AuxType* As, const float S,  SyncType* Sss,  AuxType* Sas) -> decltype(Driver->GetSmoothingStateScaled(Ss,As,S,Sss,Sas));
	};

	static constexpr bool HasGetSmoothingStateScaled = TModels_V<CGetSmoothingStateScaledMemberFuncable, DriverType,   SyncType*,  AuxType*,  float, SyncType*, AuxType*>;

	template<bool HasFunc=HasGetSmoothingStateScaled>
	static typename TEnableIf<HasFunc>::Type CallGetSmoothingStateScaledMemberFunc(DriverType* Driver,const SyncType* SyncState, const AuxType* AuxState, const float Scale,  SyncType* ScaledSyncState,  AuxType* ScaledAuxState)
	{
		if (IsValidObject(Driver))
		{
			Driver->GetSmoothingStateScaled(SyncState,AuxState, Scale,ScaledSyncState,ScaledAuxState);
		}
	}

	template<bool HasFunc=HasGetSmoothingStateScaled>
	static typename TEnableIf<!HasFunc>::Type CallGetSmoothingStateScaledMemberFunc(DriverType* Driver, const SyncType* SyncState, const AuxType* AuxState, const float Scale,  SyncType* ScaledSyncState,  AuxType* ScaledAuxState)
	{
		// This isn't a problem but we should probably do something if there is no GetSmoothingDelta function:
	}

	// -----------------------------------------------------------------------------------------------------------------------------------
	//	Get Smoothing Union
	//
	//	Called Every Frame To Get Dela Between Each Tick State And Apply Smoothing For Fixed Tick And Corrections
	//	
	// -----------------------------------------------------------------------------------------------------------------------------------

	static void GetSmoothingStateUnion(DriverType* Driver, const SyncType* CurrentSyncState, const AuxType* CurrentAuxState,const SyncType* DeltaSyncState, const AuxType* DeltaAuxState,  SyncType* UnionSyncState,  AuxType* UnionAuxState)
	{
		if (IsValidObject(Driver))
		{
			FJoltNetworkPredictionDriver<ModelDef>::CallGetSmoothingStateUnionMemberFunc(Driver, CurrentSyncState, CurrentAuxState, DeltaSyncState, DeltaAuxState, UnionSyncState, UnionAuxState);
		}
	}

	struct CGetSmoothingStateUnionMemberFuncable
	{
		template <typename InDriverType, typename...>
		auto Requires(InDriverType* Driver, const SyncType* Cs, const AuxType* Ca,const SyncType* Ds, const AuxType* Da,  SyncType* Us,  AuxType* Ua) -> decltype(Driver->GetSmoothingStateUnion(Cs, Ca, Ds, Da, Us, Ua));
	};

	static constexpr bool HasGetSmoothingStateUnion = TModels_V<CGetSmoothingStateUnionMemberFuncable, DriverType,  SyncType*,  AuxType*, SyncType*,  AuxType*,  SyncType*,  AuxType*>;

	template<bool HasFunc=HasGetSmoothingStateUnion>
	static typename TEnableIf<HasFunc>::Type CallGetSmoothingStateUnionMemberFunc(DriverType* Driver, const SyncType* CurrentSyncState, const AuxType* CurrentAuxState,const SyncType* DeltaSyncState, const AuxType* DeltaAuxState,  SyncType* UnionSyncState,  AuxType* UnionAuxState)
	{
		if (IsValidObject(Driver))
		{
			Driver->GetSmoothingStateUnion(CurrentSyncState, CurrentAuxState, DeltaSyncState, DeltaAuxState, UnionSyncState, UnionAuxState);
		}
	}

	template<bool HasFunc=HasGetSmoothingStateUnion>
	static typename TEnableIf<!HasFunc>::Type CallGetSmoothingStateUnionMemberFunc(DriverType* Driver, const SyncType* CurrentSyncState, const AuxType* CurrentAuxState,const SyncType* DeltaSyncState, const AuxType* DeltaAuxState,  SyncType* UnionSyncState,  AuxType* UnionAuxState)
	{
		
	}

	// -----------------------------------------------------------------------------------------------------------------------------------
	//	FinalizeSmoothingFrame
	//
	//	When a smoothing service is active, this is called every engine frame to push the final smoothed state to the driver. This is optional.
	// -----------------------------------------------------------------------------------------------------------------------------------
	static void FinalizeSmoothingFrame(DriverType* Driver, const SyncType* SyncState, const AuxType* AuxState)
	{
		CallFinalizeSmoothingFrameMemberFunc(Driver, SyncState, AuxState);
	}
	
	struct CFinalizeSmoothingFrameMemberFuncable
	{
		template <typename InDriverType, typename...>
		auto Requires(InDriverType* Driver, const SyncType* S, const AuxType* A) -> decltype(Driver->FinalizeSmoothingFrame(S, A));
	};

	static constexpr bool HasFinalizeSmoothingFrame = TModels_V<CFinalizeSmoothingFrameMemberFuncable, DriverType, SyncType, AuxType>;

	template<bool HasFunc=HasFinalizeSmoothingFrame>
	static typename TEnableIf<HasFunc>::Type CallFinalizeSmoothingFrameMemberFunc(DriverType* Driver, const SyncType* SyncState, const AuxType* AuxState)
	{
		jnpCheckSlow(Driver);
		Driver->FinalizeSmoothingFrame(SyncState, AuxState);
	}

	template<bool HasFunc=HasFinalizeSmoothingFrame>
	static typename TEnableIf<!HasFunc>::Type CallFinalizeSmoothingFrameMemberFunc(DriverType* Driver, const SyncType* SyncState, const AuxType* AuxState)
	{
		// FinalizeSmoothingFrame isn't required, but the driver/model won't get smoothed state
	}

	// -----------------------------------------------------------------------------------------------------------------------------------
	//	RestoreFrame
	//
	//	Called prior to beginning rollback frames. This instance should put itself in whatever state it needs to be in for resimulation to
	//	run. In practice this should mean getting right collision+component states in sync so that any scene queries will get the correct
	//	data.
	//
	//	
	//	
	// -----------------------------------------------------------------------------------------------------------------------------------
	
	static void RestorePhysicsFrame(DriverType* Driver, const SyncType* SyncState, const AuxType* AuxState)
	{
		FJoltNetworkPredictionDriver<ModelDef>::CallRestorePhysicsFrameMemberFunc(Driver, SyncState, AuxState);
	}

	static void RestoreFrame(DriverType* Driver, const SyncType* SyncState, const AuxType* AuxState)
	{
		FJoltNetworkPredictionDriver<ModelDef>::CallRestoreFrameMemberFunc(Driver, SyncState, AuxState);
	}

	struct CRestoreFrameMemberFuncable
	{
		template <typename InDriverType, typename...>
		auto Requires(InDriverType* Driver, const SyncType* S, const AuxType* A) -> decltype(Driver->RestoreFrame(S, A));
	};

	static constexpr bool HasRestoreFrame = TModels_V<CRestoreFrameMemberFuncable, DriverType, SyncType, AuxType>;

	template<bool HasFunc=HasRestoreFrame>
	static typename TEnableIf<HasFunc>::Type CallRestoreFrameMemberFunc(DriverType* Driver, const SyncType* SyncState, const AuxType* AuxState)
	{
		jnpCheckSlow(Driver);
		Driver->RestoreFrame(SyncState, AuxState);
	}

	template<bool HasFunc=HasRestoreFrame>
	static typename TEnableIf<!HasFunc>::Type CallRestoreFrameMemberFunc(DriverType* Driver, const SyncType* SyncState, const AuxType* AuxState)
	{
		// This isn't a problem but we should probably do something if there is no RestoreFrame function:
		//	-Warn/complain (but user may not care in all cases. So may need a trait to opt out?)
		//	-Call FinalizeFrame: less boiler plate to add (but causes confusion and could lead to slow FinalizeFrames being called too often)
		//	-Force both Restore/Finalize Frame to be implemented but always implicitly call RestoreFrame before FinalizeFrame? (nah)
	}
	
	struct CRestorePhysicsFrameMemberFuncable
	{
		template <typename InDriverType, typename...>
		auto Requires(InDriverType* Driver, const SyncType* S, const AuxType* A) -> decltype(Driver->RestorePhysicsFrame(S, A));
	};
	
	static constexpr bool HasRestorePhysicsFrame = TModels_V<CRestorePhysicsFrameMemberFuncable, DriverType, SyncType, AuxType>;
	
	template<bool HasFunc=HasRestorePhysicsFrame>
	static typename TEnableIf<HasFunc>::Type CallRestorePhysicsFrameMemberFunc(DriverType* Driver, const SyncType* SyncState, const AuxType* AuxState)
	{
		jnpCheckSlow(Driver);
		Driver->RestorePhysicsFrame(SyncState, AuxState);
	}
	
	template<bool HasFunc=HasRestorePhysicsFrame>
	static typename TEnableIf<!HasFunc>::Type CallRestorePhysicsFrameMemberFunc(DriverType* Driver, const SyncType* SyncState, const AuxType* AuxState)
	{
		// This isn't a problem but we should probably do something if there is no RestorePhysicsFrame function:
		//	-Warn/complain (but user may not care in all cases. So may need a trait to opt out?)
		//	-Call FinalizeFrame: less boiler plate to add (but causes confusion and could lead to slow FinalizeFrames being called too often)
		//	-Force both Restore/Finalize Frame to be implemented but always implicitly call RestoreFrame before FinalizeFrame? (nah)
	}

	// -----------------------------------------------------------------------------------------------------------------------------------
	//	CallServerRPC
	//
	//	Tells the driver to call the Server RPC to send InputCmds to the server. UJoltNetworkPredictionComponent::CallServerRPC is the default
	//	implementation and shouldn't need to be defined by the user.
	// -----------------------------------------------------------------------------------------------------------------------------------
		
	static void CallServerRPC(DriverType* Driver)
	{
		CallServerRPCMemberFunc(Driver);
	}
	
	struct CCallServerRPCMemberFuncable
	{
		template <typename InDriverType>
		auto Requires(InDriverType* Driver) -> decltype(Driver->CallServerRPC());
	};

	static constexpr bool HasCallServerRPC = TModels_V<CCallServerRPCMemberFuncable, DriverType>;

	template<bool HasFunc=HasCallServerRPC>
	static typename TEnableIf<HasFunc>::Type CallServerRPCMemberFunc(DriverType* Driver)
	{
		jnpCheckSlow(Driver);
		Driver->CallServerRPC();
	}

	template<bool HasFunc=HasCallServerRPC>
	static typename TEnableIf<!HasFunc>::Type CallServerRPCMemberFunc(DriverType* Driver)
	{
		jnpCheckf(false, TEXT("No CallServerRPC implementation found. Implement DriverType::CallServerRPC or ModelDef::CallServerRPC"));
	}
	// -----------------------------------------------------------------------------------------------------------------------------------
	//	Dispatch Cues
	//
	//	Forwards call to CueDispatche's DispatchCueRecord which will invoke the queued HandleCue events to the driver.
	// -----------------------------------------------------------------------------------------------------------------------------------
	
	template<typename InDriverType=DriverType>
	static void DispatchCues(TJoltNetSimCueDispatcher<ModelDef>* CueDispatcher, InDriverType* Driver, int32 SimFrame, int32 SimTimeMS, const int32 FixedStepMS)
	{
		jnpCheckSlow(Driver);
		CueDispatcher-> template DispatchCueRecord<InDriverType>(*Driver, SimFrame, SimTimeMS, FixedStepMS);
	}

	static void DispatchCues(TJoltNetSimCueDispatcher<ModelDef>* CueDispatcher, void* Driver, int32 SimFrame, int32 SimTimeMS, const int32 FixedStepMS)
	{
	}

	// -----------------------------------------------------------------------------------------------------------------------------------
	//	ShouldReconcile
	//
	//	Determines if Sync/Aux state have diverged enough to force a correction.
	//	The default implementation will call ShouldReconcile on the state itself: bool FMySyncState::ShouldReconcile(const FMySyncState& Authority) const.
	// -----------------------------------------------------------------------------------------------------------------------------------
	static bool ShouldReconcile(const TJoltSyncAuxPair<StateTypes>& Predicted, const TJoltSyncAuxPair<StateTypes>& Authority)
	{
		return ShouldReconcile(Predicted.Sync, Authority.Sync) || ShouldReconcile(Predicted.Aux, Authority.Aux);
	}

	template<typename StateType>
	static bool ShouldReconcile(const StateType* Predicted, const StateType* Authority)
	{
		return Predicted->ShouldReconcile(*Authority);
	}
	
	static bool ShouldReconcile(const void* Predicted, const void* Authority) { return false; }

	// -----------------------------------------------------------------------------------------------------------------------------------
	//	Interpolate
	//
	//	Blend between From/To set of Sync/Aux states
	// -----------------------------------------------------------------------------------------------------------------------------------
	static void Interpolate(const TJoltSyncAuxPair<StateTypes>& From, const TJoltSyncAuxPair<StateTypes>& To, const float PCT, SyncType* SyncOut, AuxType* AuxOut)
	{
		InterpolateState(From.Sync, To.Sync, PCT, SyncOut);
		InterpolateState(From.Aux, To.Aux, PCT, AuxOut);
	}

	template<typename StateType>
	static void InterpolateState(const StateType* From, const StateType* To, const float PCT, StateType* Out)
	{
		Out->Interpolate(From, To, PCT);
	}
	
	static void InterpolateState(const void* From, const void* To, const float PCT, void* Out)
	{
	}

	// -----------------------------------------------------------------------------------------------------------------------------------
	//	Show/Hide ForInterpolation
	//
	//	Interpolated sims are initially hidden until there are two valid states to interpolate between	
	// -----------------------------------------------------------------------------------------------------------------------------------

	static void SetHiddenForInterpolation(DriverType* Driver, bool bHide)
	{
		CallSetHiddenForInterpolation(Driver, bHide);
	}

	struct CSetHiddenForInterpolationFuncable
	{
		template <typename InDriverType>
		auto Requires(InDriverType* Driver, bool bHidden) -> decltype(Driver->SetHiddenForInterpolation(bHidden));
	};
	
	static constexpr bool HasSetHiddenForInterpolation = TModels_V<CSetHiddenForInterpolationFuncable, DriverType>;

	template<bool HasFunc=HasSetHiddenForInterpolation>
	static typename TEnableIf<HasFunc>::Type CallSetHiddenForInterpolation(DriverType* Driver, bool bHide)
	{
		jnpCheckSlow(Driver);
		Driver->SetHiddenForInterpolation(bHide);
	}

	template<bool HasFunc=HasSetHiddenForInterpolation>
	static typename TEnableIf<!HasFunc>::Type CallSetHiddenForInterpolation(DriverType* Driver, bool bHide)
	{
		CallSetHiddenForInterpolationFallback(Driver, bHide);
	}

	static void CallSetHiddenForInterpolationFallback(AActor* Driver, bool bHide)
	{
		Driver->SetActorHiddenInGame(bHide);
	}

	static void CallSetHiddenForInterpolationFallback(UActorComponent* Driver, bool bHide)
	{
		Driver->GetOwner()->SetActorHiddenInGame(bHide);
	}	
	
	// -----------------------------------------------------------------------------------------------------------------------------------
	//	ToString
	//
	//	Utility functions for turning user state into strings. User states can should define ToString(FAnsiStringBuilderBase&) and append
	//	*ANSI* strings to the builder. (E.g, don't use the TEXT macro).	
	//
	//	Ansi was chosen because for Trace purposes:
	//	We want tracing user state strings to be as fast and efficient as possible for Insights so that it can be enabled during development.
	//
	//	Logging is primary intended as a last resort for printf style debugging. The system should not output UserStates to the log in 
	//	normal circumstances. (Opting in via cvars or verbose logging categories would be ok).
	//
	//	If you really need to return the string's produced here, use an FStringOutputDevice. Otherwise they will be stacked allocated
	// -----------------------------------------------------------------------------------------------------------------------------------
	static void LogUserStates(const TJoltNetworkPredictionState<StateTypes>& UserStates, FOutputDevice& Ar=*GLog)
	{
		TAnsiStringBuilder<512> Builder;

		Builder << '\n';
		ToString((InputType*)UserStates.Cmd, Builder);
		Builder << '\n';		
		ToString((SyncType*)UserStates.Sync, Builder);
		Builder << '\n';
		ToString((AuxType*)UserStates.Aux, Builder);

		Ar.Log(StringCast<TCHAR>(Builder.ToString()).Get());
	}

	template<typename StateType>
	static void LogUserState(const StateType* State, FOutputDevice& Ar=*GLog)
	{
		TAnsiStringBuilder<256> Builder;

		Builder << '\n';
		ToString(State, Builder);

		Ar.Log(StringCast<TCHAR>(Builder.ToString()).Get());
	}

	template<typename StateType>
	static void TraceUserStateString(const StateType* State, FAnsiStringBuilderBase& Builder)
	{
		ToString(State, Builder);
	}

	// Eventually: TraceUserStateBinary for Insights -> Editor debugging

	template<typename StateType>
	static void ToString(const StateType* State, FAnsiStringBuilderBase& Builder)
	{
		State->ToString(Builder);
	}

	static void ToString(const void* State, FAnsiStringBuilderBase& Builder) { }

	// -----------------------------------------------------------------------------------------------------------------------------------
	//	NetSerialize
	//
	//	Forwards NetSerialize call to the user type.
	// -----------------------------------------------------------------------------------------------------------------------------------
	template<typename StateType>
	static void NetSerialize(TJoltConditionalState<StateType>& State, const FJoltNetSerializeParams& P)
	{
		State->NetSerialize(P);
	}

	static void NetSerialize(TJoltConditionalState<void>& State, const FJoltNetSerializeParams& P) { }
};


// This is the actual template to specialize when wanting to override functions
template<typename ModelDef>
struct FJoltNetworkPredictionDriver : FJoltNetworkPredictionDriverBase<ModelDef>
{

};
