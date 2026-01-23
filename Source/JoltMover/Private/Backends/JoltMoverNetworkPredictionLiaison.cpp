// Copyright Epic Games, Inc. All Rights Reserved.

#include "Backends/JoltMoverNetworkPredictionLiaison.h"
#include "JoltNetworkPredictionModelDefRegistry.h"
#include "JoltNetworkPredictionProxyInit.h"
#include "JoltNetworkPredictionProxyWrite.h"
#include "GameFramework/Actor.h"
#include "JoltMoverComponent.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include "Core/Singletons/JoltPhysicsWorldSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMoverNetworkPredictionLiaison)


#define LOCTEXT_NAMESPACE "JoltMover"

// ----------------------------------------------------------------------------------------------------------
//	FJoltMoverActorModelDef: the piece that ties everything together that we use to register with the NP system.
// ----------------------------------------------------------------------------------------------------------

class FJoltMoverActorModelDef : public FJoltNetworkPredictionModelDef
{
public:

	JNP_MODEL_BODY();

	using Simulation = UJoltMoverNetworkPredictionLiaisonComponent;
	using StateTypes = KinematicMoverStateTypes;
	using Driver = UJoltMoverNetworkPredictionLiaisonComponent;

	static const TCHAR* GetName() { return TEXT("JoltMoverActor"); }
	static constexpr int32 GetSortPriority() { return (int32)EJoltNetworkPredictionSortPriority::PreKinematicMovers; }
};

JNP_MODEL_REGISTER(FJoltMoverActorModelDef);



UJoltMoverNetworkPredictionLiaisonComponent::UJoltMoverNetworkPredictionLiaisonComponent()
{
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bCanEverTick = true;

	bWantsInitializeComponent = true;
	bAutoActivate = true;
	SetIsReplicatedByDefault(true);
}

void UJoltMoverNetworkPredictionLiaisonComponent::ProduceInput(const int32 DeltaTimeMS, FJoltMoverInputCmdContext* Cmd)
{
	check(MoverComp);
	MoverComp->ProduceInput(DeltaTimeMS, Cmd);
}

void UJoltMoverNetworkPredictionLiaisonComponent::RestoreFrame(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState)
{
	check(MoverComp);

	int32 NewBaseSimTimeMs = 0;
	int32 NextFrameNum = 0;

	switch (UJoltNetworkPredictionWorldManager::ActiveInstance->PreferredDefaultTickingPolicy())
	{
		default:	// fall through
		case EJoltNetworkPredictionTickingPolicy::Fixed:
		{
			const FJoltFixedTickState& FixedTickState = UJoltNetworkPredictionWorldManager::ActiveInstance->GetFixedTickState();
			FJoltNetSimTimeStep TimeStep = FixedTickState.GetNextTimeStep();
			NewBaseSimTimeMs = TimeStep.TotalSimulationTime;
			NextFrameNum = TimeStep.Frame;
		}
		break; 

		case EJoltNetworkPredictionTickingPolicy::Independent:
		{
			const FJoltVariableTickState& VariableTickState = UJoltNetworkPredictionWorldManager::ActiveInstance->GetVariableTickState();
			const FJoltNetSimTimeStep NextVariableTimeStep = VariableTickState.GetNextTimeStep(VariableTickState.Frames[VariableTickState.ConfirmedFrame]);
			NewBaseSimTimeMs = NextVariableTimeStep.TotalSimulationTime;
			NextFrameNum = NextVariableTimeStep.Frame;

		}
		break;
	}

	FJoltMoverTimeStep MoverTimeStep;

	MoverTimeStep.ServerFrame = NextFrameNum;
	MoverTimeStep.BaseSimTimeMs = NewBaseSimTimeMs;
	MoverTimeStep.StepMs = 0;

	MoverComp->RestoreFrame(SyncState, AuxState, MoverTimeStep);
}

void UJoltMoverNetworkPredictionLiaisonComponent::RestorePhysicsFrame(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UJoltMoverNetworkPredictionLiaisonComponent::RestorePhysicsFrame);
	int32 NewBaseSimTimeMs = 0;
	int32 NextFrameNum = 0;

	switch (UJoltNetworkPredictionWorldManager::ActiveInstance->PreferredDefaultTickingPolicy())
	{
	default:	// fall through
	case EJoltNetworkPredictionTickingPolicy::Fixed:
		{
			const FJoltFixedTickState& FixedTickState = UJoltNetworkPredictionWorldManager::ActiveInstance->GetFixedTickState();
			FJoltNetSimTimeStep TimeStep = FixedTickState.GetNextTimeStep();
			NewBaseSimTimeMs = TimeStep.TotalSimulationTime;
			NextFrameNum = TimeStep.Frame;
		}
		break; 

	case EJoltNetworkPredictionTickingPolicy::Independent:
		{
			const FJoltVariableTickState& VariableTickState = UJoltNetworkPredictionWorldManager::ActiveInstance->GetVariableTickState();
			const FJoltNetSimTimeStep NextVariableTimeStep = VariableTickState.GetNextTimeStep(VariableTickState.Frames[VariableTickState.ConfirmedFrame]);
			NewBaseSimTimeMs = NextVariableTimeStep.TotalSimulationTime;
			NextFrameNum = NextVariableTimeStep.Frame;

		}
		break;
	}

	FJoltMoverTimeStep MoverTimeStep;

	MoverTimeStep.ServerFrame = NextFrameNum;
	MoverTimeStep.BaseSimTimeMs = NewBaseSimTimeMs;
	MoverTimeStep.StepMs = 0;
	
	//TODO:@GreggoryAddison::CodeCompletion || This should set the physics state of all mover bodies back to their authoritative state. Static colliders don't need to be reset
	if (UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>())
	{
		if (!MoverComp) return;
		const UPrimitiveComponent* P = MoverComp->GetUpdatedComponent<UPrimitiveComponent>();
		if (!P) return;

		const FJoltUpdatedMotionState* S = SyncState->Collection.FindDataByType<FJoltUpdatedMotionState>(); 
		const FJoltMoverTargetSyncState* T = SyncState->Collection.FindDataByType<FJoltMoverTargetSyncState>(); 
		if (S && T)
		{
			Subsystem->K2_SetPhysicsState(P, S->GetTransform_WorldSpace(), S->GetVelocity_WorldSpace(), S->GetAngularVelocityDegrees_WorldSpace());
		}
	}
	
}

void UJoltMoverNetworkPredictionLiaisonComponent::FinalizeFrame(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UJoltMoverNetworkPredictionLiaisonComponent::FinalizeFrame);
	check(MoverComp);

	const FJoltNetworkPredictionSettings NetworkPredictionSettings = UJoltNetworkPredictionWorldManager::ActiveInstance->GetSettings();
	if (MoverComp->GetOwnerRole() == ROLE_SimulatedProxy && NetworkPredictionSettings.SimulatedProxyNetworkLOD == EJoltNetworkLOD::Interpolated)
	{
		FJoltMoverInputCmdContext InputCmd;
		MoverComp->TickInterpolatedSimProxy(MoverComp->GetLastTimeStep(), InputCmd, MoverComp, MoverComp->GetSyncState(), *SyncState, *AuxState);
	}
	
	MoverComp->FinalizeFrame(SyncState, AuxState);
}

void UJoltMoverNetworkPredictionLiaisonComponent::FinalizeSmoothingFrame(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState)
{
	check(MoverComp);
	MoverComp->FinalizeSmoothingFrame(SyncState, AuxState);
}

void UJoltMoverNetworkPredictionLiaisonComponent::InitializeSimulationState(FJoltMoverSyncState* OutSync, FJoltMoverAuxStateContext* OutAux)
{
	check(MoverComp);
	StartingOutSync = OutSync;
	StartingOutAux = OutAux;
	MoverComp->InitializeSimulationState(StartingOutSync, StartingOutAux);
}

void UJoltMoverNetworkPredictionLiaisonComponent::SimulationTick(const FJoltNetSimTimeStep& TimeStep, const TJoltNetSimInput<KinematicMoverStateTypes>& SimInput, const TJoltNetSimOutput<KinematicMoverStateTypes>& SimOutput)
{
	check(MoverComp);

	FJoltMoverTickStartData StartData;
	FJoltMoverTickEndData EndData;

	StartData.InputCmd  = *SimInput.Cmd;
	StartData.SyncState = *SimInput.Sync;
	StartData.AuxState  = *SimInput.Aux;

	// Ensure persistent SyncStates are present in the start state for a SimTick.
	for (const FJoltMoverDataPersistence& PersistentSyncEntry : MoverComp->PersistentSyncStateDataTypes)
	{
		StartData.SyncState.Collection.FindOrAddDataByType(PersistentSyncEntry.RequiredType);
	}
	
	FJoltMoverTimeStep MoverTimeStep;

	MoverTimeStep.ServerFrame	= TimeStep.Frame;
	MoverTimeStep.BaseSimTimeMs = TimeStep.TotalSimulationTime;
	MoverTimeStep.StepMs		= TimeStep.StepMS;

	MoverComp->SimulationTick(MoverTimeStep, StartData, OUT EndData);

	*SimOutput.Sync = EndData.SyncState;
    *SimOutput.Aux.Get() = EndData.AuxState;
}

void UJoltMoverNetworkPredictionLiaisonComponent::PostPhysicsTick(const FJoltNetSimTimeStep& TimeStep, const TJoltNetSimInput<KinematicMoverStateTypes>& SimInput, const TJoltNetSimOutput<KinematicMoverStateTypes>& SimOutput)
{
	check(MoverComp);

	FJoltMoverTickEndData EndData;
	
	EndData.AuxState = *SimOutput.Aux.Get();
	EndData.SyncState = *SimOutput.Sync;
	
	MoverComp->PostPhysicsTick(OUT EndData);

	*SimOutput.Sync = EndData.SyncState;
	*SimOutput.Aux.Get() = EndData.AuxState;
}


double UJoltMoverNetworkPredictionLiaisonComponent::GetCurrentSimTimeMs()
{
	return NetworkPredictionProxy.GetTotalSimTimeMS();
}

int32 UJoltMoverNetworkPredictionLiaisonComponent::GetCurrentSimFrame()
{
	return NetworkPredictionProxy.GetPendingFrame();
}


bool UJoltMoverNetworkPredictionLiaisonComponent::ReadPendingSyncState(OUT FJoltMoverSyncState& OutSyncState)
{
	if (const FJoltMoverSyncState* PendingSyncState = NetworkPredictionProxy.ReadSyncState<FJoltMoverSyncState>(EJoltNetworkPredictionStateRead::Simulation))
	{
		OutSyncState = *PendingSyncState;
		return true;
	}

	return false;
}

bool UJoltMoverNetworkPredictionLiaisonComponent::WritePendingSyncState(const FJoltMoverSyncState& SyncStateToWrite)
{
	bool bDidWriteSucceed = NetworkPredictionProxy.WriteSyncState<FJoltMoverSyncState>([&SyncStateToWrite](FJoltMoverSyncState& PendingSyncStateRef)
		{
			PendingSyncStateRef = SyncStateToWrite;
		}) != nullptr;

	return bDidWriteSucceed;
}


bool UJoltMoverNetworkPredictionLiaisonComponent::ReadPresentationSyncState(OUT FJoltMoverSyncState& OutSyncState)
{
	if (const FJoltMoverSyncState* PendingSyncState = NetworkPredictionProxy.ReadSyncState<FJoltMoverSyncState>(EJoltNetworkPredictionStateRead::Presentation))
	{
		OutSyncState = *PendingSyncState;
		return true;
	}

	return false;
}


bool UJoltMoverNetworkPredictionLiaisonComponent::WritePresentationSyncState(const FJoltMoverSyncState& SyncStateToWrite)
{
	bool bDidWriteSucceed = NetworkPredictionProxy.WritePresentationSyncState<FJoltMoverSyncState>([&SyncStateToWrite](FJoltMoverSyncState& PresentationSyncStateRef)
		{
			PresentationSyncStateRef = SyncStateToWrite;
		}) != nullptr;

	return bDidWriteSucceed;
}


bool UJoltMoverNetworkPredictionLiaisonComponent::ReadPrevPresentationSyncState(FJoltMoverSyncState& OutSyncState)
{
	if (const FJoltMoverSyncState* PrevPresentationSyncState = NetworkPredictionProxy.ReadPrevPresentationSyncState<FJoltMoverSyncState>())
	{
		OutSyncState = *PrevPresentationSyncState;
		return true;
	}

	return false;
}


bool UJoltMoverNetworkPredictionLiaisonComponent::WritePrevPresentationSyncState(const FJoltMoverSyncState& SyncStateToWrite)
{
	bool bDidWriteSucceed = NetworkPredictionProxy.WritePrevPresentationSyncState<FJoltMoverSyncState>([&SyncStateToWrite](FJoltMoverSyncState& PresentationSyncStateRef)
		{
			PresentationSyncStateRef = SyncStateToWrite;
		}) != nullptr;

	return bDidWriteSucceed;
}


#if WITH_EDITOR
EDataValidationResult UJoltMoverNetworkPredictionLiaisonComponent::ValidateData(FDataValidationContext& Context, const UJoltMoverComponent& ValidationMoverComp) const
{
	if (const AActor* OwnerActor = ValidationMoverComp.GetOwner())
	{
		if (OwnerActor->IsReplicatingMovement())
		{
			Context.AddError(FText::Format(LOCTEXT("ConflictingReplicateMovementProperty", "The owning actor ({0}) has the ReplicateMovement property enabled. This will conflict with Network Prediction and cause poor quality movement. Please disable it."),
				FText::FromString(GetNameSafe(OwnerActor))));

			return EDataValidationResult::Invalid;
		}
	}

	return EDataValidationResult::Valid;
}
#endif // WITH_EDITOR

void UJoltMoverNetworkPredictionLiaisonComponent::BeginPlay()
{
	Super::BeginPlay();

	if (StartingOutSync && StartingOutAux)
	{
		if (FJoltUpdatedMotionState* StartingSyncState = StartingOutSync->Collection.FindMutableDataByType<FJoltUpdatedMotionState>())
		{
			const FTransform UpdatedComponentTransform = MoverComp->GetUpdatedComponentTransform();
			// if our location has changed between initialization and begin play (ex: Actors sharing an exact start location and one gets "pushed" to make them fit) lets write the new location to avoid any disagreements
			if (!UpdatedComponentTransform.GetLocation().Equals(StartingSyncState->GetLocation_WorldSpace()))
			{
				StartingSyncState->SetTransforms_WorldSpace(UpdatedComponentTransform.GetLocation(),
													 UpdatedComponentTransform.GetRotation().Rotator(),
													 FVector::ZeroVector,
													 FVector::ZeroVector);	// no initial velocity
			}
		}
	}
}


void UJoltMoverNetworkPredictionLiaisonComponent::InitializeComponent()
{
	Super::InitializeComponent();
}


void UJoltMoverNetworkPredictionLiaisonComponent::UninitializeComponent()
{
	NetworkPredictionProxy.EndPlay();

	Super::UninitializeComponent();
}

void UJoltMoverNetworkPredictionLiaisonComponent::OnRegister()
{
	Super::OnRegister();
}


void UJoltMoverNetworkPredictionLiaisonComponent::RegisterComponentTickFunctions(bool bRegister)
{
	Super::RegisterComponentTickFunctions(bRegister);
}

void UJoltMoverNetworkPredictionLiaisonComponent::InitializeNetworkPredictionProxy()
{
	MoverComp = GetOwner()->FindComponentByClass<UJoltMoverComponent>();


	if (ensureAlwaysMsgf(MoverComp, TEXT("UJoltMoverNetworkPredictionLiaisonComponent on actor %s failed to find associated Mover component. This actor's movement will not be simulated. Verify its setup."), *GetNameSafe(GetOwner())))
	{
		NetworkPredictionProxy.Init<FJoltMoverActorModelDef>(GetWorld(), GetReplicationProxies(), this, this);
	}
}

#undef LOCTEXT_NAMESPACE
