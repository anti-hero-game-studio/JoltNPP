// Copyright Epic Games, Inc. All Rights Reserved.


#include "JoltMoverComponent.h"
#include "JoltMoverSimulationTypes.h"
#include "JoltMovementModeStateMachine.h"
#include "MotionWarpingJoltMoverAdapter.h"
#include "DefaultMovementSet/Modes/JoltKinematicWalkingMode.h"
#include "DefaultMovementSet/Modes/JoltKinematicFallingMode.h"
#include "DefaultMovementSet/Modes/JoltKinematicFlyingMode.h"
#include "MoveLibrary/JoltMovementMixer.h"
#include "MoveLibrary/JoltMovementUtils.h"
#include "MoveLibrary/JoltFloorQueryUtils.h"
#include "MoveLibrary/JoltRollbackBlackboard.h"
#include "JoltInputContainerStruct.h"
#include "JoltMoverLog.h"
#include "JoltInstantMovementEffect.h"
#include "Backends/JoltMoverNetworkPredictionLiaison.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/ScopedMovementUpdate.h"
#include "Engine/World.h"
#include "GameFramework/PhysicsVolume.h"
#include "Misc/AssertionMacros.h"
#include "Misc/TransactionObjectEvent.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "UObject/ObjectSaveContext.h"
#include "Components/SkeletalMeshComponent.h"
#include "MotionWarpingComponent.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_CHAOS_VISUAL_DEBUGGER
#include "ChaosVisualDebugger/ChaosVisualDebuggerTrace.h"
#include "ChaosVisualDebugger/JoltMoverCVDRuntimeTrace.h"
#endif

#include "Components/CapsuleComponent.h"
#include "Core/Singletons/JoltPhysicsWorldSubsystem.h"
#include "DefaultMovementSet/Modes/Physics/JoltPhysicsMovementMode.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMoverComponent)

#define LOCTEXT_NAMESPACE "JoltMover"

namespace JoltMoverComponentCVars
{
	static int32 WarnOnPostSimDifference = 0;
	FAutoConsoleVariableRef CVarMoverWarnOnPostSimDifference(
		TEXT("jolt.mover.debug.WarnOnPostSimDifference"),
		WarnOnPostSimDifference,
		TEXT("If != 0, then any differences between the sim sync state and the component locations just after movement simulation will emit warnings.\n")
	);

} // end JoltMoverComponentCVars



namespace JoltMoverComponentConstants
{
	const FVector DefaultGravityAccel	= FVector(0.0, 0.0, -980.0);
	const FVector DefaultUpDir			= FVector(0.0, 0.0, 1.0);
}


static constexpr float ROTATOR_TOLERANCE = (1e-3);

UJoltMoverComponent::UJoltMoverComponent()
{
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bCanEverTick = false;

	BasedMovementTickFunction.bCanEverTick = true;
	BasedMovementTickFunction.bStartWithTickEnabled = false;
	BasedMovementTickFunction.SetTickFunctionEnable(false);
	BasedMovementTickFunction.TickGroup = TG_PostPhysics;

	bWantsInitializeComponent = true;
	bAutoActivate = true;

	PersistentSyncStateDataTypes.Add(FJoltMoverDataPersistence(FJoltUpdatedMotionState::StaticStruct(), true));
	PersistentSyncStateDataTypes.Add(FJoltMoverDataPersistence(FJoltMoverTargetSyncState::StaticStruct(), true));

	BackendClass = UJoltMoverNetworkPredictionLiaisonComponent::StaticClass();
}


void UJoltMoverComponent::InitializeComponent()
{
	TGuardValue<bool> InInitializeComponentGuard(bInInitializeComponent, true);

	Super::InitializeComponent();

	const UWorld* MyWorld = GetWorld();

	if (MyWorld && MyWorld->IsGameWorld())
	{
		if (SimBlackboard)
		{
			SimBlackboard->InvalidateAll();
		}

		SimBlackboard = NewObject<UJoltMoverBlackboard>(this, TEXT("JoltMoverBlackboard"), RF_Transient);

		RollbackBlackboard = NewObject<UJoltRollbackBlackboard>(this, TEXT("RollbackBlackboard"), RF_Transient);
		RollbackBlackboard_InternalWrapper = NewObject<UJoltRollbackBlackboard_InternalWrapper>(this, TEXT("RollbackBlackboard_Internal"), RF_Transient);
		RollbackBlackboard_InternalWrapper->Init(*RollbackBlackboard);

		// create any internal entries
		static UJoltRollbackBlackboard::EntrySettings ModeChangeRecordSettings;
		ModeChangeRecordSettings.SizingPolicy = EJoltBlackboardSizingPolicy::FixedDeclaredSize;
		ModeChangeRecordSettings.FixedSize = 4;
		ModeChangeRecordSettings.PersistencePolicy = EJoltBlackboardPersistencePolicy::Forever;
		ModeChangeRecordSettings.RollbackPolicy = EJoltBlackboardRollbackPolicy::InvalidatedOnRollback;

		RollbackBlackboard->CreateEntry<FJoltMovementModeChangeRecord>(CommonBlackboard::LastModeChangeRecord, ModeChangeRecordSettings);


		FindDefaultUpdatedComponent();

		// Set up FSM and initial movement states
		ModeFSM = NewObject<UJoltMovementModeStateMachine>(this, TEXT("JoltMoverStateMachine"), RF_Transient);
		ModeFSM->ClearAllMovementModes();
		ModeFSM->ClearAllGlobalTransitions();

		bool bHasMatchingStartingState = false;

		for (const TPair<FName, TObjectPtr<UJoltBaseMovementMode>>& Element : MovementModes)
		{
			if (Element.Value.Get() == nullptr)
			{
				UE_LOG(LogJoltMover, Warning, TEXT("Invalid Movement Mode type '%s' detected on %s. Mover actor will not function correctly."),
					*Element.Key.ToString(), *GetNameSafe(GetOwner()));
				continue;
			}

			ModeFSM->RegisterMovementMode(Element.Key, Element.Value);

			bHasMatchingStartingState |= (StartingMovementMode == Element.Key);
		}

		for (TObjectPtr<UJoltBaseMovementModeTransition>& Transition : Transitions)
		{
			ModeFSM->RegisterGlobalTransition(Transition);
		}

		UE_CLOG(!bHasMatchingStartingState, LogJoltMover, Warning, TEXT("Invalid StartingMovementMode '%s' specified on %s. Mover actor will not function."),
			*StartingMovementMode.ToString(), *GetNameSafe(GetOwner()));

		if (bHasMatchingStartingState && StartingMovementMode != NAME_None)
		{
			ModeFSM->SetDefaultMode(StartingMovementMode);
			ModeFSM->QueueNextMode(StartingMovementMode);
		}

		// Instantiate our sister backend component that will actually talk to the system driving the simulation
		if (BackendClass)
		{
			UActorComponent* NewLiaisonComp = NewObject<UActorComponent>(this, BackendClass, TEXT("BackendLiaisonComponent"));
			BackendLiaisonComp.SetObject(NewLiaisonComp);
			BackendLiaisonComp.SetInterface(CastChecked<IJoltMoverBackendLiaisonInterface>(NewLiaisonComp));
			if (BackendLiaisonComp)
			{
				NewLiaisonComp->RegisterComponent();
				NewLiaisonComp->InitializeComponent();
				NewLiaisonComp->SetNetAddressable();
			}
		}
		else
		{
			UE_LOG(LogJoltMover, Error, TEXT("No backend class set on %s. Mover actor will not function."), *GetNameSafe(GetOwner()));
		}
		
		InitializeWithJolt();
		
	}

	// Gather initial state to fulfill queries
	FJoltMoverSyncState DefaultMoverSyncState;
	CreateDefaultInputAndState(CachedLastProducedInputCmd, DefaultMoverSyncState, CachedLastAuxState);
	MoverSyncStateDoubleBuffer.SetBufferedData(DefaultMoverSyncState);
	CachedLastUsedInputCmd = CachedLastProducedInputCmd;
	LastMoverDefaultSyncState = MoverSyncStateDoubleBuffer.GetReadable().Collection.FindDataByType<FJoltUpdatedMotionState>();
	
	
}


void UJoltMoverComponent::UninitializeComponent()
{
	if (UActorComponent* LiaisonAsComp = Cast<UActorComponent>(BackendLiaisonComp.GetObject()))
	{
		LiaisonAsComp->DestroyComponent();
	}
	BackendLiaisonComp = nullptr;

	if (SimBlackboard)
	{
		SimBlackboard->InvalidateAll();
	}

	if (ModeFSM)
	{
		ModeFSM->ClearAllMovementModes();
		ModeFSM->ClearAllGlobalTransitions();
	}

	Super::UninitializeComponent();
}


void UJoltMoverComponent::OnRegister()
{
	TGuardValue<bool> InOnRegisterGuard(bInOnRegister, true);

	Super::OnRegister();

	FindDefaultUpdatedComponent();
}


void UJoltMoverComponent::RegisterComponentTickFunctions(bool bRegister)
{
	Super::RegisterComponentTickFunctions(bRegister);

	// Super may start up the tick function when we don't want to.
	UpdateTickRegistration();

	// If the owner ticks, make sure we tick first. This is to ensure the owner's location will be up to date when it ticks.
	AActor* Owner = GetOwner();

	if (bRegister && PrimaryComponentTick.bCanEverTick && Owner && Owner->CanEverTick())
	{
		Owner->PrimaryActorTick.AddPrerequisite(this, PrimaryComponentTick);
	}


	if (bRegister)
	{
		if (SetupActorComponentTickFunction(&BasedMovementTickFunction))
		{
			BasedMovementTickFunction.TargetMoverComp = this;
			BasedMovementTickFunction.AddPrerequisite(this, this->PrimaryComponentTick);
		}
	}
	else
	{
		if (BasedMovementTickFunction.IsTickFunctionRegistered())
		{
			BasedMovementTickFunction.UnRegisterTickFunction();
		}
	}
}


void UJoltMoverComponent::PostLoad()
{
	Super::PostLoad();

	RefreshSharedSettings();
}

void UJoltMoverComponent::OnModifyContacts()
{
	/*UPrimitiveComponent* UpdatedPrim = GetUpdatedComponent<UPrimitiveComponent>();
	if (!UpdatedPrim) return;
	UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>();
	if (!Subsystem) return;
	
	if (const UJoltPhysicsMovementMode* M = Cast<UJoltPhysicsMovementMode>(GetMovementMode()))
	{
		for (const FJoltHitEvent& E : Subsystem->GetAllHitEvents())
		{
			if (!E.SelfComp.Get() || E.SelfComp.Get() != UpdatedPrim || !E.OtherComp.Get()) continue;
			
			JPH::Body* SelfRb = Subsystem->GetCollisionBody(E.SelfComp.Get());
			JPH::Body* OtherRb = Subsystem->GetCollisionBody(E.OtherComp.Get());
			if (!SelfRb || !OtherRb) continue;	
			
			bool bOverrideToZero = false;
		
			switch (M->GetFrictionOverrideMode()) 
			{
			case EJoltMoverFrictionOverrideMode::DoNotOverride:
				break;
			case EJoltMoverFrictionOverrideMode::AlwaysOverrideToZero:
				bOverrideToZero = true;
				break;
			case EJoltMoverFrictionOverrideMode::OverrideToZeroWhenMoving:
				constexpr float MinInput = 0.1f;
				bOverrideToZero = GetMovementIntent().SizeSquared() > MinInput* MinInput;
				break;
			}
		
			if (bOverrideToZero)
			{
				// Turn off friction for the contacting bodies?? But won't this affect other local bodies in our sim?
				SelfRb->setFriction(0);
				OtherRb->setFriction(0);
			}
		}
	}*/
}

void UJoltMoverComponent::BeginPlay()
{
	Super::BeginPlay();
	
	
	
	FindDefaultUpdatedComponent();
	ensureMsgf(UpdatedComponent != nullptr, TEXT("No root component found on %s. Simulation initialization will most likely fail."), *GetPathNameSafe(GetOwner()));

	WorldToGravityTransform = FQuat::FindBetweenNormals(FVector::UpVector, GetUpDirection());
	GravityToWorldTransform = WorldToGravityTransform.Inverse();
	
	AActor* MyActor = GetOwner();
	if (MyActor)
	{
		// If no primary visual component is already set, fall back to searching for any kind of mesh,
		// favoring a direct scene child of the UpdatedComponent.
		if (!PrimaryVisualComponent)
		{
			if (UpdatedComponent)
			{
				for (USceneComponent* ChildComp : UpdatedComponent->GetAttachChildren())
				{
					if (ChildComp->IsA<UMeshComponent>())
					{
						SetPrimaryVisualComponent(ChildComp);
						break;
					}
				}
			}

			if (!PrimaryVisualComponent)
			{
				SetPrimaryVisualComponent(MyActor->FindComponentByClass<UMeshComponent>());
			}
		}

		ensureMsgf(UpdatedComponent, TEXT("A Mover actor (%s) must have an UpdatedComponent"), *GetNameSafe(MyActor));

		// Optional motion warping support
		if (UMotionWarpingComponent* WarpingComp = MyActor->FindComponentByClass<UMotionWarpingComponent>())
		{
			UMotionWarpingJoltMoverAdapter* WarpingAdapter = WarpingComp->CreateOwnerAdapter<UMotionWarpingJoltMoverAdapter>();
			WarpingAdapter->SetMoverComp(this);
		}

		// If an InputProducer isn't already set, check if the actor is one
		if (!InputProducer &&
			MyActor->GetClass()->ImplementsInterface(UJoltMoverInputProducerInterface::StaticClass()))
		{
			InputProducer = MyActor;
		}

		if (InputProducer)
		{
			InputProducers.AddUnique(InputProducer);
		}

		TSet<UActorComponent*> Components = MyActor->GetComponents();
		for (UActorComponent* Component : Components)
		{
			if (IsValid(Component) &&
				Component->GetClass()->ImplementsInterface(UJoltMoverInputProducerInterface::StaticClass()))
			{
				InputProducers.AddUnique(Component);
			}
		}
	}
	
	if (!MovementMixer)
	{
		MovementMixer = NewObject<UJoltMovementMixer>(this, TEXT("Default Movement Mixer"));
	}

	// Initialize the fixed delay for event scheduling
	if (BackendLiaisonComp)
	{
		EventSchedulingMinDelaySeconds = BackendLiaisonComp->GetEventSchedulingMinDelaySeconds();
	}
	
	
	if (const UJoltNetworkPredictionWorldManager* M = GetWorld()->GetSubsystem<UJoltNetworkPredictionWorldManager>())
	{
		bIsClientUsingSmoothing = M->GetSettings().bEnableFixedTickSmoothing;
		if ( bIsClientUsingSmoothing && PrimaryVisualComponent)
		{
			PrimaryVisualComponent->SetUsingAbsoluteLocation(true);
			PrimaryVisualComponent->SetUsingAbsoluteRotation(true);
			PrimaryVisualComponent->SetUsingAbsoluteScale(true);
		}
	}
}

void UJoltMoverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>())
	{
		Subsystem->OnModifyContacts.Remove(OnModifyContactsDelegateHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void UJoltMoverComponent::BindProcessGeneratedMovement(FJoltMover_ProcessGeneratedMovement ProcessGeneratedMovementEvent)
{
	ProcessGeneratedMovement = ProcessGeneratedMovementEvent;
}

void UJoltMoverComponent::UnbindProcessGeneratedMovement()
{
	ProcessGeneratedMovement.Clear();
}

void UJoltMoverComponent::ProduceInput(const int32 DeltaTimeMS, FJoltMoverInputCmdContext* Cmd)
{
	Cmd->Collection.Empty();

	if (!bIgnoreAnyInputProducer)
	{
		for (TObjectPtr<UObject> InputProducerComponent : InputProducers)
		{
			if (IsValid(InputProducerComponent))
			{
				if (!bGatherInputFromAllInputProducerComponents && InputProducer != GetOwner())
				{
					continue;
				}
				IJoltMoverInputProducerInterface::Execute_ProduceInput(InputProducerComponent, DeltaTimeMS, IN OUT *Cmd);
			}
		}
	}
	else
	{
		Cmd->Collection.FindOrAddDataByType<FJoltCharacterDefaultInputs>();
	}

	CachedLastProducedInputCmd = *Cmd;
}

void UJoltMoverComponent::RestoreFrame(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState, const FJoltMoverTimeStep& NewBaseTimeStep)
{
	const FJoltMoverSyncState& InvalidSyncState = GetSyncState();
	const FJoltMoverAuxStateContext& InvalidAuxState = CachedLastAuxState;
	OnSimulationPreRollback(&InvalidSyncState, SyncState, &InvalidAuxState, AuxState, NewBaseTimeStep);
	SetFrameStateFromContext(SyncState, AuxState, /* rebase? */ true);
	OnSimulationRollback(SyncState, AuxState, NewBaseTimeStep);
}

void UJoltMoverComponent::FinalizeFrame(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UJoltMoverComponent::FinalizeFrame);
	
	// TODO: Revisit this location check -- it seems simplistic now that we have composable state. Consider supporting a version that allows each sync state data struct a chance to react.
	// The component will often be in the "right place" already on FinalizeFrame, so a comparison check makes sense before setting it.
	
	if (const FJoltUpdatedMotionState* MoverState = SyncState->Collection.FindDataByType<FJoltUpdatedMotionState>())
	{
		const FRotator& ComponentRot =  UpdatedComponent->GetComponentQuat().Rotator();
		const FRotator& StateRot = MoverState->GetOrientation_WorldSpace();
		const FVector& ComponentLoc = UpdatedComponent->GetComponentLocation();	
		const FVector& StateLoc = MoverState->GetLocation_WorldSpace();

		if ((ComponentLoc.Equals(StateLoc) == false ||
			 ComponentRot.Equals(StateRot, ROTATOR_TOLERANCE) == false))
		{
			SetFrameStateFromContext(SyncState, AuxState, /* rebase? */ false);
		}
		else
		{
			UpdateCachedFrameState(SyncState, AuxState);
		}
	}


	// Only allow the server to move this component or the client if they are not smoothing. This removes the double call to update the component
	if (!bIsClientUsingSmoothing || (GetOwner()->HasAuthority() && !GetOwner()->HasLocalNetOwner()) || GetNetMode() == NM_DedicatedServer)
	{
		if (PrimaryVisualComponent)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PrimaryVisualComponent::SetRelativeTransform);
			if (!PrimaryVisualComponent->GetRelativeTransform().Equals(BaseVisualComponentTransform))
			{
				PrimaryVisualComponent->SetRelativeTransform(BaseVisualComponentTransform);
			}
		}
	}
	
	if (OnPostFinalize.IsBound())
	{
		OnPostFinalize.Broadcast(MoverSyncStateDoubleBuffer.GetReadable(), CachedLastAuxState);
	}
}

void UJoltMoverComponent::FinalizeUnchangedFrame()
{
	CachedLastSimTickTimeStep.BaseSimTimeMs = BackendLiaisonComp->GetCurrentSimTimeMs();
	CachedLastSimTickTimeStep.ServerFrame = BackendLiaisonComp->GetCurrentSimFrame();

	if (OnPostFinalize.IsBound())
	{
		OnPostFinalize.Broadcast(MoverSyncStateDoubleBuffer.GetReadable(), CachedLastAuxState);
	}
}


void UJoltMoverComponent::FinalizeSmoothingFrame(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState)
{
	if (PrimaryVisualComponent)
	{
		if (SmoothingMode == EJoltMoverSmoothingMode::VisualComponentOffset && (PrimaryVisualComponent != UpdatedComponent))
		{
			// Offset the visual component so it aligns with the smoothed state transform, while leaving the actual root component in place
			if (const FJoltUpdatedMotionState* MoverState = SyncState->Collection.FindDataByType<FJoltUpdatedMotionState>())
			{
				FTransform ActorTransform = FTransform(MoverState->GetOrientation_WorldSpace(), MoverState->GetLocation_WorldSpace(), FVector::OneVector);
				PrimaryVisualComponent->SetWorldTransform(BaseVisualComponentTransform * ActorTransform);	// smoothed location with base offset applied
			}
		}
	}
}

void UJoltMoverComponent::TickInterpolatedSimProxy(const FJoltMoverTimeStep& TimeStep, const FJoltMoverInputCmdContext& InputCmd, UJoltMoverComponent* MoverComp, const FJoltMoverSyncState& CachedSyncState, const FJoltMoverSyncState& SyncState, const FJoltMoverAuxStateContext& AuxState)
{
	if (bSyncInputsForSimProxy)
	{
		CachedLastUsedInputCmd = InputCmd;

		// Copy any structs that may be inputs from sync state to input cmd - note the use of the special container class that lets the inputs avoid causing rollbacks
		if (FJoltMoverInputContainerDataStruct* InputContainer = static_cast<FJoltMoverInputContainerDataStruct*>(SyncState.Collection.FindDataByType(FJoltMoverInputContainerDataStruct::StaticStruct())))
		{
			for (auto InputStructIt = InputContainer->Collection.GetCollectionDataIterator(); InputStructIt; ++InputStructIt)
			{
				if (const FJoltMoverDataStructBase* InputDataStruct = InputStructIt->Get())
				{
					CachedLastUsedInputCmd.Collection.AddDataByCopy(InputDataStruct);
				}
			}
		}
	}

	TArray<TSharedPtr<FJoltMovementModifierBase>> ModifiersToStart;
	TArray<TSharedPtr<FJoltMovementModifierBase>> ModifiersToEnd;

	for (auto ModifierFromSyncStateIt = SyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierFromSyncStateIt; ++ModifierFromSyncStateIt)
	{
		const TSharedPtr<FJoltMovementModifierBase> ModifierFromSyncState = *ModifierFromSyncStateIt;
		
		bool bContainsModifier = false;
		for (auto ModifierFromCacheIt = CachedSyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierFromCacheIt; ++ModifierFromCacheIt)
		{
			const TSharedPtr<FJoltMovementModifierBase> ModifierFromCache = *ModifierFromCacheIt;
			
			if (ModifierFromSyncState->Matches(ModifierFromCache.Get()))
			{
				bContainsModifier = true;
				break;
			}
		}

		if (!bContainsModifier)
		{
			ModifiersToStart.Add(ModifierFromSyncState);
		}
	}

	for (auto ModifierFromCacheIt = CachedSyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierFromCacheIt; ++ModifierFromCacheIt)
	{
		const TSharedPtr<FJoltMovementModifierBase> ModifierFromCache = *ModifierFromCacheIt;
		
		bool bContainsModifier = false;
		for (auto ModifierFromSyncStateIt = SyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierFromSyncStateIt; ++ModifierFromSyncStateIt)
		{
			const TSharedPtr<FJoltMovementModifierBase> ModifierFromSyncState = *ModifierFromSyncStateIt;
			
			if (ModifierFromSyncState->Matches(ModifierFromCache.Get()))
			{
				bContainsModifier = true;
				break;
			}
		}

		if (!bContainsModifier)
		{
			ModifiersToEnd.Add(ModifierFromCache);
		}
	}

	for (TSharedPtr<FJoltMovementModifierBase> Modifier : ModifiersToStart)
	{
		Modifier->GenerateHandle();
		Modifier->OnStart(MoverComp, TimeStep, SyncState, AuxState);
	}

	for (auto ModifierIt = SyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierIt; ++ModifierIt)
	{
		if (ModifierIt->IsValid())
		{
			ModifierIt->Get()->OnPreMovement(this, TimeStep);
			ModifierIt->Get()->OnPostMovement(this, TimeStep, SyncState, AuxState);
		}
	}

	for (TSharedPtr<FJoltMovementModifierBase> Modifier : ModifiersToEnd)
	{
		Modifier->OnEnd(MoverComp, TimeStep, SyncState, AuxState);
	}
}


void UJoltMoverComponent::InitializeSimulationState(FJoltMoverSyncState* OutSync, FJoltMoverAuxStateContext* OutAux)
{
	jnpCheckSlow(UpdatedComponent);
	jnpCheckSlow(OutSync);
	jnpCheckSlow(OutAux);

	CreateDefaultInputAndState(CachedLastProducedInputCmd, *OutSync, *OutAux);

	CachedLastUsedInputCmd = CachedLastProducedInputCmd;
	MoverSyncStateDoubleBuffer.SetBufferedData(*OutSync);
	LastMoverDefaultSyncState = MoverSyncStateDoubleBuffer.GetReadable().Collection.FindDataByType<FJoltUpdatedMotionState>();
	
	CachedLastAuxState = *OutAux;

}

void UJoltMoverComponent::SimulationTick(const FJoltMoverTimeStep& InTimeStep, const FJoltMoverTickStartData& SimInput, OUT FJoltMoverTickEndData& SimOutput)
{
	// Send mover info to the Chaos Visual Debugger (this will do nothing if CVD is not recording, or the mover info data channel not enabled)
	UE::JoltMoverUtils::FJoltMoverCVDRuntimeTrace::TraceJoltMoverData(this, &SimInput.InputCmd, &SimInput.SyncState);
	
	const bool bIsResimulating = InTimeStep.BaseSimTimeMs <= CachedNewestSimTickTimeStep.BaseSimTimeMs;

	FJoltMoverTimeStep MoverTimeStep(InTimeStep);
	MoverTimeStep.bIsResimulating = bIsResimulating;

	if (bHasRolledBack)
	{
		ProcessFirstSimTickAfterRollback(InTimeStep);
	}

	PreSimulationTick(MoverTimeStep, SimInput.InputCmd);
	
	JoltPreSimulationTick(MoverTimeStep, SimInput, SimOutput);

	if (!ModeFSM)
	{
		SimOutput.SyncState = SimInput.SyncState;
		SimOutput.AuxState = SimInput.AuxState;
		return;
	}

	CheckForExternalMovement(SimInput);

	// Some sync state data should carry over between frames
	for (const FJoltMoverDataPersistence& PersistentSyncEntry : PersistentSyncStateDataTypes)
	{
		bool bShouldAddDefaultData = true;

		if (PersistentSyncEntry.bCopyFromPriorFrame)
		{
			if (const FJoltMoverDataStructBase* PriorFrameData = SimInput.SyncState.Collection.FindDataByType(PersistentSyncEntry.RequiredType))
			{
				SimOutput.SyncState.Collection.AddDataByCopy(PriorFrameData);
				bShouldAddDefaultData = false;
			}
		}

		if (bShouldAddDefaultData)
		{
			SimOutput.SyncState.Collection.FindOrAddDataByType(PersistentSyncEntry.RequiredType);
		}
	}

	// Make sure any other sync state structs that aren't supposed to be persistent are removed
	const TArray<TSharedPtr<FJoltMoverDataStructBase>>& AllSyncStructs = SimOutput.SyncState.Collection.GetDataArray();
	for (int32 i = AllSyncStructs.Num()-1; i >= 0; --i)
	{
		bool bShouldRemoveStructType = true;

		const UScriptStruct* ScriptStruct = AllSyncStructs[i]->GetScriptStruct();

		for (const FJoltMoverDataPersistence& PersistentSyncEntry : PersistentSyncStateDataTypes)
		{
			if (PersistentSyncEntry.RequiredType == ScriptStruct)
			{
				bShouldRemoveStructType = false;
				break;
			}
		}

		if (bShouldRemoveStructType)
		{
			SimOutput.SyncState.Collection.RemoveDataByType(ScriptStruct);
		}	
	}

	SimOutput.AuxState = SimInput.AuxState;

	FJoltCharacterDefaultInputs* Input = SimInput.InputCmd.Collection.FindMutableDataByType<FJoltCharacterDefaultInputs>();
	
	if (Input && !Input->SuggestedMovementMode.IsNone())
	{
		ModeFSM->QueueNextMode(Input->SuggestedMovementMode);
	}

	if (OnPreMovement.IsBound())
	{
		OnPreMovement.Broadcast(MoverTimeStep, SimInput.InputCmd, SimInput.SyncState, SimInput.AuxState);
	}

	RollbackBlackboard_InternalWrapper->BeginSimulationFrame(MoverTimeStep);

	// Tick the actual simulation. This is where the proposed moves are queried and executed, affecting change to the moving actor's gameplay state and captured in the output sim state
	if (IsInGameThread())
	{
		// If we're on the game thread, we can make use of a scoped movement update for better perf of multi-step movements.  If not, then we're definitely not moving the component in immediate mode so the scope would have no effect.
		FScopedMovementUpdate ScopedMovementUpdate(UpdatedComponent, EScopedUpdate::DeferredUpdates);
		ModeFSM->OnSimulationTick(UpdatedComponent, UpdatedCompAsPrimitive, SimBlackboard.Get(), SimInput, MoverTimeStep, SimOutput);
	}
	else
	{
		ModeFSM->OnSimulationTick(UpdatedComponent, UpdatedCompAsPrimitive, SimBlackboard.Get(), SimInput, MoverTimeStep, SimOutput);
	}

	if (FJoltUpdatedMotionState* OutputSyncState = SimOutput.SyncState.Collection.FindMutableDataByType<FJoltUpdatedMotionState>())
	{
		const FName MovementModeAfterTick = ModeFSM->GetCurrentModeName();
		SimOutput.SyncState.MovementMode = MovementModeAfterTick;

		if (JoltMoverComponentCVars::WarnOnPostSimDifference)
		{
			if (UpdatedComponent->GetComponentLocation().Equals(OutputSyncState->GetLocation_WorldSpace()) == false ||
				UpdatedComponent->GetComponentQuat().Equals(OutputSyncState->GetOrientation_WorldSpace().Quaternion(), UE_KINDA_SMALL_NUMBER) == false)
			{
				UE_LOG(LogJoltMover, Warning, TEXT("Detected pos/rot difference between Mover actor (%s) sync state and scene component after sim ticking. This indicates a movement mode may not be authoring the final state correctly."), *GetNameSafe(UpdatedComponent->GetOwner()));
			}
		}
	}

	RollbackBlackboard_InternalWrapper->EndSimulationFrame();

	if (!SimOutput.MoveRecord.GetTotalMoveDelta().IsZero())
	{
		UE_LOG(LogJoltMover, VeryVerbose, TEXT("KinematicSimTick: %s (role %i) frame %d: %s"),
			*GetNameSafe(UpdatedComponent->GetOwner()), UpdatedComponent->GetOwnerRole(), MoverTimeStep.ServerFrame, *SimOutput.MoveRecord.ToString());
	}

	if (OnPostMovement.IsBound())
	{
		OnPostMovement.Broadcast(MoverTimeStep, SimOutput.SyncState, SimOutput.AuxState);
	}

	CachedLastUsedInputCmd = SimInput.InputCmd;

	if (bSupportsKinematicBasedMovement)
	{ 
		UpdateBasedMovementScheduling(SimOutput);
	}

	OnPostSimulationTick.Broadcast(MoverTimeStep);

	CachedLastSimTickTimeStep = MoverTimeStep;

	if (MoverTimeStep.ServerFrame > CachedNewestSimTickTimeStep.ServerFrame || MoverTimeStep.BaseSimTimeMs > CachedNewestSimTickTimeStep.BaseSimTimeMs)
	{
		CachedNewestSimTickTimeStep = MoverTimeStep;
	}	

	if (bSyncInputsForSimProxy)
	{
		// stow all inputs away in a special container struct that avoids causing potential rollbacks 
		// so they can be available to other clients even if they're only interpolated sim proxies
		if (FJoltMoverInputContainerDataStruct* InputContainer = static_cast<FJoltMoverInputContainerDataStruct*>(SimOutput.SyncState.Collection.FindOrAddDataByType(FJoltMoverInputContainerDataStruct::StaticStruct())))
		{	
			for (auto InputCmdIt = SimInput.InputCmd.Collection.GetCollectionDataIterator(); InputCmdIt; ++InputCmdIt)
			{
				if (InputCmdIt->Get())
				{
					InputContainer->Collection.AddDataByCopy(InputCmdIt->Get());
				}
			}
		}
	}
	
	// Get our rigid body and apply central impulse
	if (UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>())
	{
		const FJoltMoverTargetSyncState* OutState = SimOutput.SyncState.Collection.FindDataByType<FJoltMoverTargetSyncState>();
		if (!OutState) return;
		Subsystem->UpdateActorVelocity(GetOwner(), OutState->GetTargetVelocity_WorldSpace(), OutState->GetTargetAngularVelocity_WorldSpace());
	}
	
}

void UJoltMoverComponent::PostPhysicsTick(FJoltMoverTickEndData& SimOutput)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UJoltMoverComponent::PostPhysicsTick);
	if (UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>())
	{
		if (!UpdatedCompAsPrimitive) return;
		FJoltUpdatedMotionState& FinalState = SimOutput.SyncState.Collection.FindOrAddMutableDataByType<FJoltUpdatedMotionState>();
		
		const int Id = Subsystem->GetActorRootShapeId(GetOwner());
		FTransform T;
		FVector V, A, F;
		Subsystem->GetPhysicsState(UpdatedCompAsPrimitive, T, V, A, F);
		
		/*const FString MyRole = GetOwnerRole() == ROLE_Authority ? "Server" : "Client"; 
		UE_LOG(LogJoltMover, Warning, TEXT("[MSL] NetMode = %s : Transform = %s"), *MyRole, *T.ToHumanReadableString());
		UE_LOG(LogJoltMover, Warning, TEXT("[MSL] NetMode = %s : LinearVelocity = %s"), *MyRole, *V.ToCompactString());
		UE_LOG(LogJoltMover, Warning, TEXT("[MSL] NetMode = %s : AngularVelocity = %s"), *MyRole, *A.ToCompactString());*/
		
		//TODO:@GreggoryAddison::CodeCompletion || The current base a player is standing on will need to be passed in... I think.
		FinalState.SetTransforms_WorldSpace(T.GetLocation(), T.GetRotation().Rotator(), V, A, nullptr);
	}
}

UJoltBaseMovementMode* UJoltMoverComponent::FindMovementMode(TSubclassOf<UJoltBaseMovementMode> MovementMode) const
{
	return FindMode_Mutable(MovementMode);
}

void UJoltMoverComponent::K2_FindMovementModifier(FJoltMovementModifierHandle ModifierHandle, bool& bFoundModifier, int32& TargetAsRawBytes) const
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
}

DEFINE_FUNCTION(UJoltMoverComponent::execK2_FindMovementModifier)
{
	P_GET_STRUCT(FJoltMovementModifierHandle, ModifierHandle);
	P_GET_UBOOL_REF(bFoundModifier);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	
	void* ModifierPtr = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	bFoundModifier = false;
	
	if (!ModifierPtr)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("JoltMoverComponent_FindMovementModifier_UnresolvedTarget", "Failed to resolve the OutMovementModifier for FindMovementModifier")
		);
	
		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}
	else if (!StructProp)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("JoltMoverComponent_FindMovementModifier_TargetNotStruct", "FindMovementModifier: Target for OutMovementModifier is not a valid type. It must be a Struct and a child of FJoltMovementModifierBase.")
		);
	
		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}
	else if (!StructProp->Struct || !StructProp->Struct->IsChildOf(FJoltMovementModifierBase::StaticStruct()))
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("JoltMoverComponent_FindMovementModifier_BadType", "FindMovementModifier: Target for OutMovementModifier is not a valid type. Must be a child of FJoltMovementModifierBase.")
		);
	
		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}
	else
	{
		P_NATIVE_BEGIN;
		
		if (const FJoltMovementModifierBase* FoundActiveMove = P_THIS->FindMovementModifier(ModifierHandle))
		{
			StructProp->Struct->CopyScriptStruct(ModifierPtr, FoundActiveMove);
			bFoundModifier = true;
		}

		P_NATIVE_END;
	}
}

bool UJoltMoverComponent::IsModifierActiveOrQueued(const FJoltMovementModifierHandle& ModifierHandle) const
{
	return FindMovementModifier(ModifierHandle) ? true : false;
}

const FJoltMovementModifierBase* UJoltMoverComponent::FindMovementModifier(const FJoltMovementModifierHandle& ModifierHandle) const
{
	if (!ModifierHandle.IsValid())
	{
		return nullptr;
	}
	
	const FJoltMoverSyncState& CachedSyncState = MoverSyncStateDoubleBuffer.GetReadable();
	
	// Check active modifiers for modifier handle
	for (auto ActiveModifierFromSyncStateIt = CachedSyncState.MovementModifiers.GetActiveModifiersIterator(); ActiveModifierFromSyncStateIt; ++ActiveModifierFromSyncStateIt)
	{
		const TSharedPtr<FJoltMovementModifierBase> ActiveModifierFromSyncState = *ActiveModifierFromSyncStateIt;

		if (ModifierHandle == ActiveModifierFromSyncState->GetHandle())
		{
			return ActiveModifierFromSyncState.Get();
		}
	}

	// Check queued modifiers for modifier handle
	for (auto QueuedModifierFromSyncStateIt = CachedSyncState.MovementModifiers.GetQueuedModifiersIterator(); QueuedModifierFromSyncStateIt; ++QueuedModifierFromSyncStateIt)
	{
		const TSharedPtr<FJoltMovementModifierBase> QueuedModifierFromSyncState = *QueuedModifierFromSyncStateIt;

		if (ModifierHandle == QueuedModifierFromSyncState->GetHandle())
		{
			return QueuedModifierFromSyncState.Get();
		}
	}
	
	return ModeFSM->FindQueuedModifier(ModifierHandle);
}

const FJoltMovementModifierBase* UJoltMoverComponent::FindMovementModifierByType(const UScriptStruct* DataStructType) const
{
	const FJoltMoverSyncState& CachedSyncState = MoverSyncStateDoubleBuffer.GetReadable();
	
	// Check active modifiers for modifier handle
	for (auto ActiveModifierFromSyncStateIt = CachedSyncState.MovementModifiers.GetActiveModifiersIterator(); ActiveModifierFromSyncStateIt; ++ActiveModifierFromSyncStateIt)
	{
		const TSharedPtr<FJoltMovementModifierBase> ActiveModifierFromSyncState = *ActiveModifierFromSyncStateIt;

		if (DataStructType == ActiveModifierFromSyncState->GetScriptStruct())
		{
			return ActiveModifierFromSyncState.Get();
		}
	}

	// Check queued modifiers for modifier handle
	for (auto QueuedModifierFromSyncStateIt = CachedSyncState.MovementModifiers.GetQueuedModifiersIterator(); QueuedModifierFromSyncStateIt; ++QueuedModifierFromSyncStateIt)
	{
		const TSharedPtr<FJoltMovementModifierBase> QueuedModifierFromSyncState = *QueuedModifierFromSyncStateIt;

		if (DataStructType == QueuedModifierFromSyncState->GetScriptStruct())
		{
			return QueuedModifierFromSyncState.Get();
		}
	}
	
	return ModeFSM->FindQueuedModifierByType(DataStructType);
}

bool UJoltMoverComponent::HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const
{
	return HasGameplayTagInState(MoverSyncStateDoubleBuffer.GetReadable(), TagToFind, bExactMatch);
}

bool UJoltMoverComponent::HasGameplayTagInState(const FJoltMoverSyncState& SyncState, FGameplayTag TagToFind, bool bExactMatch) const 
{
	// Check loose / external tags
	if (bExactMatch)
	{
		if (ExternalGameplayTags.HasTagExact(TagToFind))
		{
			return true;
		}
	}
	else
	{
		if (ExternalGameplayTags.HasTag(TagToFind))
		{
			return true;
		}
	}

	// Check active Movement Mode
	if (const UJoltBaseMovementMode* ActiveMovementMode = FindMovementModeByName(SyncState.MovementMode))
	{
		if (ActiveMovementMode->HasGameplayTag(TagToFind, bExactMatch))
		{
			return true;
		}
	}

	// Search Movement Modifiers
	for (auto ModifierFromSyncStateIt = SyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierFromSyncStateIt; ++ModifierFromSyncStateIt)
	{
		if (const TSharedPtr<FJoltMovementModifierBase> ModifierFromSyncState = *ModifierFromSyncStateIt)
		{
			if (ModifierFromSyncState.IsValid() && ModifierFromSyncState->HasGameplayTag(TagToFind, bExactMatch))
			{
				return true;
			}
		}
	}

	// Search Layered Moves
	for (const TSharedPtr<FJoltLayeredMoveBase>& LayeredMove : SyncState.LayeredMoves.GetActiveMoves())
	{
		if (LayeredMove->HasGameplayTag(TagToFind, bExactMatch))
		{
			return true;
		}
	}

	return false;
}


void UJoltMoverComponent::AddGameplayTag(FGameplayTag TagToAdd)
{
	ExternalGameplayTags.AddTag(TagToAdd);
}

void UJoltMoverComponent::AddGameplayTags(const FGameplayTagContainer& TagsToAdd)
{
	ExternalGameplayTags.AppendTags(TagsToAdd);
}

void UJoltMoverComponent::RemoveGameplayTag(FGameplayTag TagToRemove)
{
	ExternalGameplayTags.RemoveTag(TagToRemove);
}

void UJoltMoverComponent::RemoveGameplayTags(const FGameplayTagContainer& TagsToRemove)
{
	ExternalGameplayTags.RemoveTags(TagsToRemove);
}

void UJoltMoverComponent::PreSimulationTick(const FJoltMoverTimeStep& TimeStep, const FJoltMoverInputCmdContext& InputCmd)
{
	if (OnPreSimulationTick.IsBound())
	{
		OnPreSimulationTick.Broadcast(TimeStep, InputCmd);
	}

	for (const TSubclassOf<UJoltLayeredMoveLogic>& PendingRegistrantClass : MovesPendingRegistration)
	{
		TObjectPtr<UJoltLayeredMoveLogic> RegisteredMove = NewObject<UJoltLayeredMoveLogic>(this, PendingRegistrantClass);
		RegisteredMoves.Add(RegisteredMove);
	}

	for (const TSubclassOf<UJoltLayeredMoveLogic>& PendingUnregistrantClass : MovesPendingUnregistration)
	{
		RegisteredMoves.RemoveAll([&PendingUnregistrantClass, this]
			(const TObjectPtr<UJoltLayeredMoveLogic>& MoveLogic)
			{
				if (MoveLogic->GetClass() == PendingUnregistrantClass)
				{
					return true;
				}
				
				return false;
			});
	}
	
	MovesPendingRegistration.Empty();
	MovesPendingUnregistration.Empty();
}

void UJoltMoverComponent::UpdateCachedFrameState(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UJoltMoverComponent::UpdateCachedFrameState);
	// TODO integrate dirty tracking
	FJoltMoverSyncState& BufferedSyncState = MoverSyncStateDoubleBuffer.GetWritable();
	BufferedSyncState = *SyncState;
	LastMoverDefaultSyncState = BufferedSyncState.Collection.FindDataByType<FJoltUpdatedMotionState>();
	MoverSyncStateDoubleBuffer.Flip();

	// TODO: when AuxState starts getting used we need to double buffer it here as well
	CachedLastAuxState = *AuxState;
	CachedLastSimTickTimeStep.BaseSimTimeMs = BackendLiaisonComp->GetCurrentSimTimeMs();
	CachedLastSimTickTimeStep.ServerFrame = BackendLiaisonComp->GetCurrentSimFrame();
}

void UJoltMoverComponent::SetFrameStateFromContext(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState, bool bRebaseBasedState)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UJoltMoverComponent::SetFrameStateFromContext);
	UpdateCachedFrameState(SyncState, AuxState);

	if (FJoltUpdatedMotionState* MoverState = const_cast<FJoltUpdatedMotionState*>(LastMoverDefaultSyncState))
	{
		if (bRebaseBasedState && MoverState->GetMovementBase())
		{
			// Note that this is modifying our cached mover state from what we received from Network Prediction. We are resampling
			// the transform of the movement base, in case it has changed as well during the rollback.
			MoverState->UpdateCurrentMovementBase();
		}

		// The state's properties are usually worldspace already, but may need to be adjusted to match the current movement base
		const FVector WorldLocation = MoverState->GetLocation_WorldSpace();
		const FRotator WorldOrientation = MoverState->GetOrientation_WorldSpace();
		const FVector WorldVelocity = MoverState->GetVelocity_WorldSpace();

		// Apply the desired transform to the scene component

		// If we can, then we can utilize grouped movement updates to reduce the number of calls to SendPhysicsTransform
		if (IsUsingDeferredGroupMovement())
		{
			// Signal to the USceneComponent that we are moving that this should be in a grouped update
			// and not apply changes on the physics thread immediately
			FScopedMovementUpdate MovementUpdate(
				UpdatedComponent,
				EScopedUpdate::DeferredGroupUpdates,
				/*bRequireOverlapsEventFlagToQueueOverlaps*/ true);

			FTransform Transform(WorldOrientation, WorldLocation, UpdatedComponent->GetComponentTransform().GetScale3D());
			UpdatedComponent->SetWorldTransform(Transform, /*bSweep*/false, nullptr, ETeleportType::None);
			UpdatedComponent->ComponentVelocity = WorldVelocity;
		}
		else
		{
			FTransform Transform(WorldOrientation, WorldLocation, UpdatedComponent->GetComponentTransform().GetScale3D());
			UpdatedComponent->SetWorldTransform(Transform, /*bSweep*/false, nullptr, ETeleportType::None);
			UpdatedComponent->ComponentVelocity = WorldVelocity;
		}
	}
}


void UJoltMoverComponent::CreateDefaultInputAndState(FJoltMoverInputCmdContext& OutInputCmd, FJoltMoverSyncState& OutSyncState, FJoltMoverAuxStateContext& OutAuxState) const
{
	OutInputCmd = FJoltMoverInputCmdContext();
	// TODO: here is where we'd add persistent input cmd struct types once they're supported

	OutSyncState = FJoltMoverSyncState();

	// Add all initial persistent sync state types
	for (const FJoltMoverDataPersistence& PersistentSyncEntry : PersistentSyncStateDataTypes)
	{
		if (PersistentSyncEntry.RequiredType.Get()) // This can happen if a previously existing required type was removed, causing a crash
		{
			OutSyncState.Collection.FindOrAddDataByType(PersistentSyncEntry.RequiredType);
		}
	}

	// Mirror the scene component transform if we have one, otherwise it will be left at origin
	FJoltUpdatedMotionState* MoverState = OutSyncState.Collection.FindMutableDataByType<FJoltUpdatedMotionState>();
	if (MoverState && UpdatedComponent)
	{
		MoverState->SetTransforms_WorldSpace(
			UpdatedComponent->GetComponentLocation(),
			UpdatedComponent->GetComponentRotation(),
			FVector::ZeroVector, // no initial velocity
			FVector::ZeroVector);
	}

	OutSyncState.MovementMode = StartingMovementMode;
	
	OutAuxState = FJoltMoverAuxStateContext();

}

void UJoltMoverComponent::HandleImpact(FJoltMoverOnImpactParams& ImpactParams)
{
	if (ImpactParams.MovementModeName.IsNone())
	{
		ImpactParams.MovementModeName = ModeFSM->GetCurrentModeName();
	}
	
	OnHandleImpact(ImpactParams);
}

void UJoltMoverComponent::OnHandleImpact(const FJoltMoverOnImpactParams& ImpactParams)
{
	// TODO: Handle physics impacts here - ie when player runs into box, impart force onto box
}

void UJoltMoverComponent::UpdateBasedMovementScheduling(const FJoltMoverTickEndData& SimOutput)
{
	// If we have a dynamic movement base, enable later based movement tick
	UPrimitiveComponent* SyncStateDynamicBase = nullptr;
	if (const FJoltUpdatedMotionState* OutputSyncState = SimOutput.SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>())
	{
		if (UJoltBasedMovementUtils::IsADynamicBase(OutputSyncState->GetMovementBase()))
		{
			SyncStateDynamicBase = OutputSyncState->GetMovementBase();
		}
	}

	// Remove any stale dependency
	if (MovementBaseDependency && (MovementBaseDependency != SyncStateDynamicBase))
	{
		UJoltBasedMovementUtils::RemoveTickDependency(BasedMovementTickFunction, MovementBaseDependency);
		MovementBaseDependency = nullptr;
	}

	// Set up current dependencies
	if (SyncStateDynamicBase)
	{
		BasedMovementTickFunction.SetTickFunctionEnable(true);

		if (UJoltBasedMovementUtils::IsBaseSimulatingPhysics(SyncStateDynamicBase))
		{
			BasedMovementTickFunction.TickGroup = TG_PostPhysics;
		}
		else
		{
			BasedMovementTickFunction.TickGroup = TG_PrePhysics;
		}

		if (MovementBaseDependency == nullptr)
		{
			UJoltBasedMovementUtils::AddTickDependency(BasedMovementTickFunction, SyncStateDynamicBase);
			MovementBaseDependency = SyncStateDynamicBase;
		}
	}
	else
	{
		BasedMovementTickFunction.SetTickFunctionEnable(false);
		MovementBaseDependency = nullptr;

		SimBlackboard->Invalidate(CommonBlackboard::LastFoundDynamicMovementBase);
		SimBlackboard->Invalidate(CommonBlackboard::LastAppliedDynamicMovementBase);
	}
}


void UJoltMoverComponent::FindDefaultUpdatedComponent()
{
	if (!IsValid(UpdatedComponent))
	{
		USceneComponent* NewUpdatedComponent = nullptr;

		const AActor* MyActor = GetOwner();
		const UWorld* MyWorld = GetWorld();

		if (MyActor && MyWorld && MyWorld->IsGameWorld())
		{
			NewUpdatedComponent = MyActor->GetRootComponent();
		}

		SetUpdatedComponent(NewUpdatedComponent);
	}
}


void UJoltMoverComponent::UpdateTickRegistration()
{
	const bool bHasUpdatedComponent = (UpdatedComponent != NULL);
	SetComponentTickEnabled(bHasUpdatedComponent && bAutoActivate);
}

void UJoltMoverComponent::OnSimulationPreRollback(const FJoltMoverSyncState* InvalidSyncState, const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* InvalidAuxState, const FJoltMoverAuxStateContext* AuxState, const FJoltMoverTimeStep& NewBaseTimeStep)
{
	ModeFSM->OnSimulationPreRollback(InvalidSyncState, SyncState, InvalidAuxState, AuxState, NewBaseTimeStep);
}


void UJoltMoverComponent::OnSimulationRollback(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState, const FJoltMoverTimeStep& NewBaseTimeStep)
{
	SimBlackboard->Invalidate(EJoltInvalidationReason::Rollback);

	RollbackBlackboard_InternalWrapper->BeginRollback(NewBaseTimeStep);

	ModeFSM->OnSimulationRollback(SyncState, AuxState, NewBaseTimeStep);

	RollbackBlackboard_InternalWrapper->EndRollback();
	bHasRolledBack = true;
}


void UJoltMoverComponent::ProcessFirstSimTickAfterRollback(const FJoltMoverTimeStep& TimeStep)
{
	OnPostSimulationRollback.Broadcast(TimeStep, CachedLastSimTickTimeStep);
	bHasRolledBack = false;
}


#if WITH_EDITOR

void UJoltMoverComponent::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);

	RefreshSharedSettings();
}

void UJoltMoverComponent::PostCDOCompiled(const FPostCDOCompiledContext& Context)
{
	Super::PostCDOCompiled(Context);

	RefreshSharedSettings();
}


void UJoltMoverComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if ((PropertyChangedEvent.Property) && (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UJoltMoverComponent, MovementModes)))
	{
		RefreshSharedSettings();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}


void UJoltMoverComponent::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	Super::PostTransacted(TransactionEvent);

	if ((TransactionEvent.GetEventType() == ETransactionObjectEventType::Finalized || TransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo) &&
		TransactionEvent.HasPropertyChanges() &&
		TransactionEvent.GetChangedProperties().Contains(GET_MEMBER_NAME_CHECKED(UJoltMoverComponent, MovementModes)))
	{
		RefreshSharedSettings();		
	}
}


EDataValidationResult UJoltMoverComponent::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (!ValidateSetup(Context))
	{
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}


bool UJoltMoverComponent::ValidateSetup(FDataValidationContext& Context) const
{
	bool bHasMatchingStartingMode = false;
	bool bDidFindAnyProblems = false;
	bool bIsAsyncBackend = false;

	// Verify backend liaison
	if (!BackendClass)
	{
		Context.AddError(FText::Format(LOCTEXT("MissingBackendClassError", "No BackendClass property specified on {0}. Mover actor will not function."),
			FText::FromString(GetNameSafe(GetOwner()))));

		bDidFindAnyProblems = true;
	}
	else if (!BackendClass->ImplementsInterface(UJoltMoverBackendLiaisonInterface::StaticClass()))
	{
		Context.AddError(FText::Format(LOCTEXT("InvalidBackendClassError", "BackendClass {0} on {1} does not implement IJoltMoverBackendLiaisonInterface. Mover actor will not function."),
			FText::FromString(BackendClass->GetName()),
			FText::FromString(GetNameSafe(GetOwner()))));

		bDidFindAnyProblems = true;
	}
	else
	{
		IJoltMoverBackendLiaisonInterface* BackendCDOAsInterface = Cast<IJoltMoverBackendLiaisonInterface>(BackendClass->GetDefaultObject());
		if (BackendCDOAsInterface)
		{
			bIsAsyncBackend = BackendCDOAsInterface->IsAsync();
			if (BackendCDOAsInterface->ValidateData(Context, *this) == EDataValidationResult::Invalid)
			{
				bDidFindAnyProblems = true;
			}
		}
	}

	// Verify all movement modes
	for (const TPair<FName, TObjectPtr<UJoltBaseMovementMode>>& Element : MovementModes)
	{
		if (StartingMovementMode == Element.Key)
		{
			bHasMatchingStartingMode = true;
		}

		// Verify movement mode is valid
		if (!Element.Value)
		{
			Context.AddError(FText::Format(LOCTEXT("InvalidMovementModeError", "Invalid movement mode on {0}, mapped as {1}. Mover actor will not function."),
				FText::FromString(GetNameSafe(GetOwner())),
				FText::FromName(Element.Key)));

			bDidFindAnyProblems = true;
		}
		else if (Element.Value->IsDataValid(Context) == EDataValidationResult::Invalid)
		{
			bDidFindAnyProblems = true;
		}

		// Verify that the movement mode's shared settings object exists (if any)
		if (Element.Value)
		{
			if (bIsAsyncBackend && !Element.Value->bSupportsAsync)
			{
				Context.AddError(FText::Format(LOCTEXT("InvalidModeAsyncSupportsError", "Movement mode on {0}, mapped as {1} does not support asynchrony but its backend is asynchronous"),
						FText::FromString(GetNameSafe(GetOwner())),
						FText::FromName(Element.Key)));

				bDidFindAnyProblems = true;
			}

			for (TSubclassOf<UObject>& Type : Element.Value->SharedSettingsClasses)
			{
				if (Type.Get() == nullptr)
				{
					Context.AddError(FText::Format(LOCTEXT("InvalidModeSettingsError", "Movement mode on {0}, mapped as {1}, has an invalid SharedSettingsClass. You may need to remove the invalid settings class."),
						FText::FromString(GetNameSafe(GetOwner())),
						FText::FromName(Element.Key)));

					bDidFindAnyProblems = true;
				}
				else if (FindSharedSettings(Type) == nullptr)
				{
					Context.AddError(FText::Format(LOCTEXT("MissingModeSettingsError", "Movement mode on {0}, mapped as {1}, is missing its desired SharedSettingsClass {2}. You may need to save the asset and/or recompile."),
						FText::FromString(GetNameSafe(GetOwner())),
						FText::FromName(Element.Key),
						FText::FromString(Type->GetName())));

					bDidFindAnyProblems = true;
				}
			}

			for (const UJoltBaseMovementModeTransition* Transition : Element.Value->Transitions)
			{
				if (!IsValid(Transition))
				{
					continue;
				}

				if (bIsAsyncBackend && !Transition->bSupportsAsync)
				{
					Context.AddError(FText::Format(LOCTEXT("InvalidModeTransitionAsyncSupportError", "Transition on mode {0} on {1} does not support asynchrony but its backend is asynchronous"),
						FText::FromName(Element.Key),
						FText::FromString(GetNameSafe(GetOwner()))));

					bDidFindAnyProblems = true;
				}

				for (const TSubclassOf<UObject>& Type : Transition->SharedSettingsClasses)
				{
					if (Type.Get() == nullptr)
					{
						Context.AddError(FText::Format(LOCTEXT("InvalidModeTransitionSettingsError", "Transition on mode {0} on {1}, has an invalid SharedSettingsClass. You may need to remove the invalid settings class."),
							FText::FromName(Element.Key),
							FText::FromString(GetNameSafe(GetOwner()))));

						bDidFindAnyProblems = true;
					}
					else if (FindSharedSettings(Type) == nullptr)
					{
						Context.AddError(FText::Format(LOCTEXT("MissingModeTransitionSettingsError", "Transition on mode {0} on {1}, is missing its desired SharedSettingsClass {2}. You may need to save the asset and/or recompile."),
							FText::FromName(Element.Key),
							FText::FromString(GetNameSafe(GetOwner())),
							FText::FromString(Type->GetName())));

						bDidFindAnyProblems = true;
					}
				}
			}
		}
	}

	// Verify we have a matching starting mode
	if (!bHasMatchingStartingMode && StartingMovementMode != NAME_None)
	{
		Context.AddError(FText::Format(LOCTEXT("InvalidStartingModeError", "Invalid StartingMovementMode {0} specified on {1}. Mover actor will not function."),
			FText::FromName(StartingMovementMode),
			FText::FromString(GetNameSafe(GetOwner()))));

		bDidFindAnyProblems = true;
	}

	// Verify transitions
	for (const UJoltBaseMovementModeTransition* Transition : Transitions)
	{
		if (!IsValid(Transition))
		{
			Context.AddError(FText::Format(LOCTEXT("InvalidTransitionError", "Invalid or missing transition object on {0}. Clean up the Transitions array."),
				FText::FromString(GetNameSafe(GetOwner()))));

			bDidFindAnyProblems = true;
			continue;
		}

		for (const TSubclassOf<UObject>& Type : Transition->SharedSettingsClasses)
		{
			if (Type.Get() == nullptr)
			{
				Context.AddError(FText::Format(LOCTEXT("InvalidTransitionSettingsError", "Transition on {0}, has an invalid SharedSettingsClass. You may need to remove the invalid settings class."),
					FText::FromString(GetNameSafe(GetOwner()))));

				bDidFindAnyProblems = true;
			}
			else if (FindSharedSettings(Type) == nullptr)
			{
				Context.AddError(FText::Format(LOCTEXT("MissingTransitionSettingsError", "Transition on {0}, is missing its desired SharedSettingsClass {2}. You may need to save the asset and/or recompile."),
					FText::FromString(GetNameSafe(GetOwner())),
					FText::FromString(Type->GetName())));

				bDidFindAnyProblems = true;
			}
		}
	}

	// Verify persistent types
	for (const FJoltMoverDataPersistence& PersistentSyncEntry : PersistentSyncStateDataTypes)
	{
		if (!PersistentSyncEntry.RequiredType || !PersistentSyncEntry.RequiredType->IsChildOf(FJoltMoverDataStructBase::StaticStruct()))
		{
			Context.AddError(FText::Format(LOCTEXT("InvalidSyncStateTypeError", "RequiredType '{0}' is not a valid type or is missing. Must be a child of FJoltMoverDataStructBase."),
				FText::FromString(GetNameSafe(PersistentSyncEntry.RequiredType))));

			bDidFindAnyProblems = true;
		}
	}

	// Verify that the up direction override is a normalized vector
	if (bHasUpDirectionOverride)
	{
		if (!UpDirectionOverride.IsNormalized())
		{
			Context.AddError(FText::Format(LOCTEXT("InvalidUpDirectionOverrideError", "UpDirectionOverride {0} needs to be a normalized vector, but it is not. {1}"),
				FText::FromString(UpDirectionOverride.ToString()),
				FText::FromString(GetNameSafe(GetOwner()))));

			bDidFindAnyProblems = true;
		}
	}

	return !bDidFindAnyProblems;
}

TArray<FString> UJoltMoverComponent::GetStartingMovementModeNames()
{
	TArray<FString> PossibleModeNames;

	PossibleModeNames.Add(TEXT(""));

	for (const TPair<FName, TObjectPtr<UJoltBaseMovementMode>>& Element : MovementModes)
	{
		FString ModeNameAsString;
		Element.Key.ToString(ModeNameAsString);
		PossibleModeNames.Add(ModeNameAsString);
	}

	return PossibleModeNames;
}

#endif // WITH_EDITOR


void UJoltMoverComponent::PhysicsVolumeChanged(APhysicsVolume* NewVolume)
{
	// This itself feels bad. When will this be called? Its impossible to know what is allowed and not allowed to be done in this callback.
	// Callbacks instead should be trapped within the simulation update function. This isn't really possible though since the UpdateComponent
	// is the one that will call this.
}


void UJoltMoverComponent::RefreshSharedSettings()
{
	TArray<TObjectPtr<UObject>> UnreferencedSettingsObjs = SharedSettings;

	// Add any missing settings
	for (const TPair<FName, TObjectPtr<UJoltBaseMovementMode>>& Element : MovementModes)
	{
		if (UJoltBaseMovementMode* Mode = Element.Value.Get())
		{
			for (TSubclassOf<UObject>& SharedSettingsType : Mode->SharedSettingsClasses)
			{
				if (SharedSettingsType.Get() == nullptr)
				{
					UE_LOG(LogJoltMover, Warning, TEXT("Invalid shared setting class detected on Movement Mode %s."), *Mode->GetName());
					continue;
				}

				bool bFoundMatchingClass = false;
				for (const TObjectPtr<UObject>& SettingsObj : SharedSettings)
				{
					if (SettingsObj && SettingsObj->IsA(SharedSettingsType))
					{
						bFoundMatchingClass = true;
						UnreferencedSettingsObjs.Remove(SettingsObj);
						break;
					}
				}

				if (!bFoundMatchingClass)
				{
					UObject* NewSettings = NewObject<UObject>(this, SharedSettingsType, NAME_None, GetMaskedFlags(RF_PropagateToSubObjects) | RF_Transactional);
					SharedSettings.Add(NewSettings);
				}
			}

			for (const UJoltBaseMovementModeTransition* Transition : Mode->Transitions)
			{
				if (!IsValid(Transition))
				{
					continue;
				}

				for (const TSubclassOf<UObject>& SharedSettingsType : Transition->SharedSettingsClasses)
				{
					if (SharedSettingsType.Get() == nullptr)
					{
						UE_LOG(LogJoltMover, Warning, TEXT("Invalid shared setting class detected on Transition on Movement Mode %s."), *Mode->GetName());
						continue;
					}

					bool bFoundMatchingClass = false;
					for (const TObjectPtr<UObject>& SettingsObj : SharedSettings)
					{
						if (SettingsObj && SettingsObj->IsA(SharedSettingsType))
						{
							bFoundMatchingClass = true;
							UnreferencedSettingsObjs.Remove(SettingsObj);
							break;
						}
					}

					if (!bFoundMatchingClass)
					{
						UObject* NewSettings = NewObject<UObject>(this, SharedSettingsType, NAME_None, GetMaskedFlags(RF_PropagateToSubObjects) | RF_Transactional);
						SharedSettings.Add(NewSettings);
					}
				}
			}
		}
	}

	for (const UJoltBaseMovementModeTransition* Transition : Transitions)
	{
		if (!IsValid(Transition))
		{
			continue;
		}

		for (const TSubclassOf<UObject>& SharedSettingsType : Transition->SharedSettingsClasses)
		{
			if (SharedSettingsType.Get() == nullptr)
			{
				UE_LOG(LogJoltMover, Warning, TEXT("Invalid shared setting class detected on Transition."));
				continue;
			}

			bool bFoundMatchingClass = false;
			for (const TObjectPtr<UObject>& SettingsObj : SharedSettings)
			{
				if (SettingsObj && SettingsObj->IsA(SharedSettingsType))
				{
					bFoundMatchingClass = true;
					UnreferencedSettingsObjs.Remove(SettingsObj);
					break;
				}
			}

			if (!bFoundMatchingClass)
			{
				UObject* NewSettings = NewObject<UObject>(this, SharedSettingsType, NAME_None, GetMaskedFlags(RF_PropagateToSubObjects) | RF_Transactional);
				SharedSettings.Add(NewSettings);
			}
		}
	}

	// Remove any settings that are no longer used
	for (const TObjectPtr<UObject>& SettingsObjToRemove : UnreferencedSettingsObjs)
	{
		SharedSettings.Remove(SettingsObjToRemove);
	}

	// Sort by name for array order consistency
	Algo::Sort(SharedSettings, [](const TObjectPtr<UObject>& LHS, const TObjectPtr<UObject>& RHS) 
		{ return (LHS->GetClass()->GetPathName() < RHS.GetClass()->GetPathName()); });
}


const TArray<TObjectPtr<UJoltLayeredMoveLogic>>* UJoltMoverComponent::GetRegisteredMoves() const
{
	return &RegisteredMoves;
}

void UJoltMoverComponent::K2_RegisterMove(TSubclassOf<UJoltLayeredMoveLogic> MoveClass)
{
	MovesPendingUnregistration.Remove(MoveClass);
	if (!MovesPendingRegistration.Contains(MoveClass))
	{
		const bool bAlreadyRegistered = RegisteredMoves.ContainsByPredicate([MoveClass](const TObjectPtr<UJoltLayeredMoveLogic>& Move) { return Move->GetClass() == MoveClass; });
		if (!bAlreadyRegistered)
		{
			MovesPendingRegistration.AddUnique(MoveClass);
		}
	}
}

void UJoltMoverComponent::K2_RegisterMoves(TArray<TSubclassOf<UJoltLayeredMoveLogic>> MoveClasses)
{
	for (const TSubclassOf<UJoltLayeredMoveLogic>& MoveClass : MoveClasses)
	{
		K2_RegisterMove(MoveClass);
	}
}

void UJoltMoverComponent::K2_UnregisterMove(TSubclassOf<UJoltLayeredMoveLogic> MoveClass)
{
	MovesPendingRegistration.Remove(MoveClass);
	if (!MovesPendingUnregistration.Contains(MoveClass))
	{
		const bool bAlreadyUnregistered = RegisteredMoves.ContainsByPredicate([MoveClass](const TObjectPtr<UJoltLayeredMoveLogic>& Move) { return Move->GetClass() == MoveClass; });
		if (!bAlreadyUnregistered)
		{
			MovesPendingUnregistration.AddUnique(MoveClass);
		}
	}
}

bool UJoltMoverComponent::K2_QueueLayeredMoveActivationWithContext(TSubclassOf<UJoltLayeredMoveLogic> MoveLogicClass, const int32& MoveAsRawData)
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(UJoltMoverComponent::execK2_QueueLayeredMoveActivationWithContext)
{
	P_GET_OBJECT(UClass, MoveLogicClass);
	
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);

	const FStructProperty* MoveActivationProperty = CastField<FStructProperty>(Stack.MostRecentProperty);
	uint8* MoveActivationPtr = Stack.MostRecentPropertyAddress;
	
	P_FINISH
	
	P_NATIVE_BEGIN;

	// TODO NS: throw some helpful warnings of what wasn't valid
	const bool bHasValidActivationStructProp = MoveActivationProperty && MoveActivationProperty->Struct && MoveActivationProperty->Struct->IsChildOf(FJoltLayeredMoveActivationParams::StaticStruct());

	bool bHasValidMoveData = MoveLogicClass && bHasValidActivationStructProp; 
	if (bHasValidMoveData)
	{
		const FJoltLayeredMoveActivationParams* MoveActivationContext = reinterpret_cast<FJoltLayeredMoveActivationParams*>(MoveActivationPtr);
		bHasValidMoveData = P_THIS->MakeAndQueueLayeredMove(MoveLogicClass, MoveActivationContext);
	}
	
	*(bool*)RESULT_PARAM = bHasValidMoveData;
	
	P_NATIVE_END;
}

bool UJoltMoverComponent::QueueLayeredMoveActivation(TSubclassOf<UJoltLayeredMoveLogic> MoveLogicClass)
{
	return MakeAndQueueLayeredMove(MoveLogicClass, nullptr);
}

void UJoltMoverComponent::K2_QueueLayeredMove(const int32& MoveAsRawData)
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
}

DEFINE_FUNCTION(UJoltMoverComponent::execK2_QueueLayeredMove)
{
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	void* MovePtr = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	P_NATIVE_BEGIN;

	const bool bHasValidStructProp = StructProp && StructProp->Struct && StructProp->Struct->IsChildOf(FJoltLayeredMoveBase::StaticStruct());

	if (ensureMsgf((bHasValidStructProp && MovePtr), TEXT("An invalid type (%s) was sent to a QueueLayeredMove node. A struct derived from FJoltLayeredMoveBase is required. No layered move will be queued."),
		StructProp ? *GetNameSafe(StructProp->Struct) : *Stack.MostRecentProperty->GetClass()->GetName()))
	{
		// Could we steal this instead of cloning? (move semantics)
		FJoltLayeredMoveBase* MoveAsBasePtr = reinterpret_cast<FJoltLayeredMoveBase*>(MovePtr);
		FJoltLayeredMoveBase* ClonedMove = MoveAsBasePtr->Clone();

		P_THIS->QueueLayeredMove(TSharedPtr<FJoltLayeredMoveBase>(ClonedMove));
	}

	P_NATIVE_END;
}


void UJoltMoverComponent::QueueLayeredMove(TSharedPtr<FJoltLayeredMoveBase> LayeredMove)
{	
	ModeFSM->QueueLayeredMove(LayeredMove);
}

FJoltMovementModifierHandle UJoltMoverComponent::K2_QueueMovementModifier(const int32& MoveAsRawData)
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
	return 0;
}

DEFINE_FUNCTION(UJoltMoverComponent::execK2_QueueMovementModifier)
{
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	void* MovePtr = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	P_NATIVE_BEGIN;

	const bool bHasValidStructProp = StructProp && StructProp->Struct && StructProp->Struct->IsChildOf(FJoltMovementModifierBase::StaticStruct());

	if (ensureMsgf((bHasValidStructProp && MovePtr), TEXT("An invalid type (%s) was sent to a QueueMovementModifier node. A struct derived from FJoltMovementModifierBase is required. No modifier will be queued."),
		StructProp ? *GetNameSafe(StructProp->Struct) : *Stack.MostRecentProperty->GetClass()->GetName()))
	{
		// Could we steal this instead of cloning? (move semantics)
		FJoltMovementModifierBase* MoveAsBasePtr = reinterpret_cast<FJoltMovementModifierBase*>(MovePtr);
		FJoltMovementModifierBase* ClonedMove = MoveAsBasePtr->Clone();

		FJoltMovementModifierHandle ModifierID = P_THIS->QueueMovementModifier(TSharedPtr<FJoltMovementModifierBase>(ClonedMove));
		*static_cast<FJoltMovementModifierHandle*>(RESULT_PARAM) = ModifierID;
	}

	P_NATIVE_END;
}

FJoltMovementModifierHandle UJoltMoverComponent::QueueMovementModifier(TSharedPtr<FJoltMovementModifierBase> Modifier)
{
	return ModeFSM->QueueMovementModifier(Modifier);
}

void UJoltMoverComponent::CancelModifierFromHandle(FJoltMovementModifierHandle ModifierHandle)
{
	ModeFSM->CancelModifierFromHandle(ModifierHandle);
}


void UJoltMoverComponent::CancelFeaturesWithTag(FGameplayTag TagToCancel, bool bRequireExactMatch)
{
	ModeFSM->CancelFeaturesWithTag(TagToCancel, bRequireExactMatch);
}


void UJoltMoverComponent::K2_QueueInstantMovementEffect(const int32& EffectAsRawData)
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
}

DEFINE_FUNCTION(UJoltMoverComponent::execK2_QueueInstantMovementEffect)
{
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	void* EffectPtr = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	P_NATIVE_BEGIN;

	const bool bHasValidStructProp = StructProp && StructProp->Struct && StructProp->Struct->IsChildOf(FJoltInstantMovementEffect::StaticStruct());

	if (ensureMsgf((bHasValidStructProp && EffectPtr), TEXT("An invalid type (%s) was sent to a QueueInstantMovementEffect node. A struct derived from FJoltInstantMovementEffect is required. No Movement Effect will be queued."),
		StructProp ? *GetNameSafe(StructProp->Struct) : *Stack.MostRecentProperty->GetClass()->GetName()))
	{
		// Could we steal this instead of cloning? (move semantics)
		FJoltInstantMovementEffect* EffectAsBasePtr = reinterpret_cast<FJoltInstantMovementEffect*>(EffectPtr);
		FJoltInstantMovementEffect* ClonedMove = EffectAsBasePtr->Clone();

		P_THIS->QueueInstantMovementEffect(TSharedPtr<FJoltInstantMovementEffect>(ClonedMove));
	}

	P_NATIVE_END;
}

void UJoltMoverComponent::K2_ScheduleInstantMovementEffect(const int32& EffectAsRawData)
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
}

DEFINE_FUNCTION(UJoltMoverComponent::execK2_ScheduleInstantMovementEffect)
{
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	void* EffectPtr = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	P_NATIVE_BEGIN;

	const bool bHasValidStructProp = StructProp && StructProp->Struct && StructProp->Struct->IsChildOf(FJoltInstantMovementEffect::StaticStruct());

	if (ensureMsgf((bHasValidStructProp && EffectPtr), TEXT("An invalid type (%s) was sent to a QueueInstantMovementEffect node. A struct derived from FJoltInstantMovementEffect is required. No Movement Effect will be queued."),
		StructProp ? *GetNameSafe(StructProp->Struct) : *Stack.MostRecentProperty->GetClass()->GetName()))
	{
		// Could we steal this instead of cloning? (move semantics)
		FJoltInstantMovementEffect* EffectAsBasePtr = reinterpret_cast<FJoltInstantMovementEffect*>(EffectPtr);
		FJoltInstantMovementEffect* ClonedMove = EffectAsBasePtr->Clone();

		P_THIS->ScheduleInstantMovementEffect(TSharedPtr<FJoltInstantMovementEffect>(ClonedMove));
	}

	P_NATIVE_END;
}

void UJoltMoverComponent::ScheduleInstantMovementEffect(TSharedPtr<FJoltInstantMovementEffect> InstantMovementEffect)
{
	ensureMsgf(IsInGameThread(), TEXT("UJoltMoverComponent::ScheduleInstantMovementEffect should only be called from the game thread. Inspect code for incorrect calls."));
	FJoltMoverTimeStep TimeStep;
	if (ensureMsgf(BackendLiaisonComp, TEXT("UJoltMoverComponent::ScheduleInstantMovementEffect was unexpectedly called with a null backend liaison component. The instant movement effect will be ignored.")))
	{
		TimeStep.BaseSimTimeMs = BackendLiaisonComp->GetCurrentSimTimeMs();
		TimeStep.ServerFrame = BackendLiaisonComp->GetCurrentSimFrame();
		// TimeStep.StepMs is not used by FJoltScheduledInstantMovementEffect::ScheduleEffect
		QueueInstantMovementEffect(FJoltScheduledInstantMovementEffect::ScheduleEffect(GetWorld(), TimeStep, InstantMovementEffect, /* SchedulingDelaySeconds = */ EventSchedulingMinDelaySeconds));
	}
}

void UJoltMoverComponent::QueueInstantMovementEffect_Internal(const FJoltMoverTimeStep& TimeStep, TSharedPtr<FJoltInstantMovementEffect> InstantMovementEffect)
{
	QueueInstantMovementEffect(FJoltScheduledInstantMovementEffect::ScheduleEffect(GetWorld(), TimeStep, InstantMovementEffect, /* SchedulingDelaySeconds = */ 0.0f));
}

void UJoltMoverComponent::QueueInstantMovementEffect(TSharedPtr<FJoltInstantMovementEffect> InstantMovementEffect)
{
	ensureMsgf(IsInGameThread(), TEXT("UJoltMoverComponent::QueueInstantMovementEffect(TSharedPtr<FJoltInstantMovementEffect>) should only be called from the game thread. Inspect code for incorrect calls."));
	FJoltMoverTimeStep TimeStep;
	if (ensureMsgf(BackendLiaisonComp, TEXT("UJoltMoverComponent::ScheduleInstantMovementEffect was unexpectedly called with a null backend liaison component. The instant movement effect will be ignored.")))
	{
		TimeStep.BaseSimTimeMs = BackendLiaisonComp->GetCurrentSimTimeMs();
		TimeStep.ServerFrame = BackendLiaisonComp->GetCurrentSimFrame();
		// TimeStep.StepMs is not used by FJoltScheduledInstantMovementEffect::ScheduleEffect
		QueueInstantMovementEffect(FJoltScheduledInstantMovementEffect::ScheduleEffect(GetWorld(), TimeStep, InstantMovementEffect, /* SchedulingDelaySeconds = */ 0.0f));
	}
}

void UJoltMoverComponent::QueueInstantMovementEffect(const FJoltScheduledInstantMovementEffect& InstantMovementEffect)
{
	// TODO Move QueueInstantMovementEffect to UJoltMoverSimulation and implement differently in sync or async mode
	if (IsInGameThread())
	{
		QueuedInstantMovementEffects.Add(InstantMovementEffect);
	}
	else
	{
		ModeFSM->QueueInstantMovementEffect_Internal(InstantMovementEffect);
	}	

#if !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
	ENetMode NetMode = GetWorld() ? GetWorld()->GetNetMode() : NM_MAX;
	UE_LOG(LogJoltMover, Verbose, TEXT("(%s) UJoltMoverComponent::QueueInstantMovementEffect: Game Thread queueing an instant movement effect scheduled for frame %d: %s."),
		*ToString(NetMode), InstantMovementEffect.ExecutionServerFrame, InstantMovementEffect.Effect.IsValid() ? *InstantMovementEffect.Effect->ToSimpleString() : TEXT("INVALID INSTANT EFFECT"));
#endif // !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
}

const TArray<FJoltScheduledInstantMovementEffect>& UJoltMoverComponent::GetQueuedInstantMovementEffects() const
{
	return QueuedInstantMovementEffects;
}

void UJoltMoverComponent::ClearQueuedInstantMovementEffects()
{
	QueuedInstantMovementEffects.Empty();
}

UJoltBaseMovementMode* UJoltMoverComponent::FindMovementModeByName(FName MovementModeName) const
{
	if (const TObjectPtr<UJoltBaseMovementMode>* FoundMode = MovementModes.Find(MovementModeName))
	{
		return *FoundMode;
	}
	return nullptr;
}

void UJoltMoverComponent::K2_FindActiveLayeredMove(bool& DidSucceed, int32& TargetAsRawBytes) const
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
}

DEFINE_FUNCTION(UJoltMoverComponent::execK2_FindActiveLayeredMove)
{
	P_GET_UBOOL_REF(DidSucceed);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	
	void* MovePtr = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	DidSucceed = false;
	
	if (!MovePtr)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("JoltMoverComponent_GetActiveLayeredMove_UnresolvedTarget", "Failed to resolve the OutLayeredMove for GetActiveLayeredMove")
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}
	else if (!StructProp)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("JoltMoverComponent_GetActiveLayeredMove_TargetNotStruct", "GetActiveLayeredMove: Target for OutLayeredMove is not a valid type. It must be a Struct and a child of FJoltLayeredMoveBase.")
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}
	else if (!StructProp->Struct || !StructProp->Struct->IsChildOf(FJoltLayeredMoveBase::StaticStruct()))
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("JoltMoverComponent_GetActiveLayeredMove_BadType", "GetActiveLayeredMove: Target for OutLayeredMove is not a valid type. Must be a child of FJoltLayeredMoveBase.")
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}
	else
	{
		P_NATIVE_BEGIN;
		
		if (const FJoltLayeredMoveBase* FoundActiveMove = P_THIS->FindActiveLayeredMoveByType(StructProp->Struct))
		{
			StructProp->Struct->CopyScriptStruct(MovePtr, FoundActiveMove);
			DidSucceed = true;
		}

		P_NATIVE_END;
	}
}

const FJoltLayeredMoveBase* UJoltMoverComponent::FindActiveLayeredMoveByType(const UScriptStruct* LayeredMoveStructType) const
{
	const FJoltMoverSyncState& CachedSyncState = MoverSyncStateDoubleBuffer.GetReadable();
	return CachedSyncState.LayeredMoves.FindActiveMove(LayeredMoveStructType);
}

void UJoltMoverComponent::QueueNextMode(FName DesiredModeName, bool bShouldReenter)
{
	DoQueueNextMode(DesiredModeName, bShouldReenter);
}

void UJoltMoverComponent::DoQueueNextMode(FName DesiredModeName, bool bShouldReenter)
{
	ModeFSM->QueueNextMode(DesiredModeName, bShouldReenter);
}

UJoltBaseMovementMode* UJoltMoverComponent::AddMovementModeFromClass(FName ModeName, TSubclassOf<UJoltBaseMovementMode> MovementMode)
{
	if (!MovementMode)
	{
		UE_LOG(LogJoltMover, Warning, TEXT("Attempted to add a movement mode that wasn't valid. AddMovementModeFromClass will not add anything. (%s)"), *GetNameSafe(GetOwner()));
		return nullptr;
	}
	if (MovementMode->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogJoltMover, Warning, TEXT("The Movement Mode class (%s) is abstract and is not a valid class to instantiate. AddMovementModeFromClass will not do anything. (%s)"), *GetNameSafe(MovementMode), *GetNameSafe(GetOwner()));
		return nullptr;
	}

	TObjectPtr<UJoltBaseMovementMode> AddedMovementMode =  NewObject<UJoltBaseMovementMode>(this, MovementMode);
	return AddMovementModeFromObject(ModeName, AddedMovementMode) ? AddedMovementMode : nullptr;
}

bool UJoltMoverComponent::AddMovementModeFromObject(FName ModeName, UJoltBaseMovementMode* MovementMode)
{
	if (MovementMode)
	{
		if (MovementMode->GetClass()->HasAnyClassFlags(CLASS_Abstract))
		{
			UE_LOG(LogJoltMover, Warning, TEXT("The Movement Mode class (%s) is abstract and is not a valid class to instantiate. AddMovementModeFromObject will not do anything. (%s)"), *GetNameSafe(MovementMode), *GetNameSafe(GetOwner()));
			return false;
		}
		
		if (TObjectPtr<UJoltBaseMovementMode>* FoundMovementMode = MovementModes.Find(ModeName))
		{
			if (FoundMovementMode->Get()->GetClass() == MovementMode->GetClass())
			{
				UE_LOG(LogJoltMover, Warning, TEXT("Added the same movement mode (%s) for a movement mode name (%s). AddMovementModeFromObject will add the mode but is likely unwanted/unnecessary behavior. (%s)"), *GetNameSafe(MovementMode), *ModeName.ToString(), *GetNameSafe(GetOwner()));
			}

			RemoveMovementMode(ModeName);
		}
		
		if (MovementMode->GetOuter() != this)
		{
			UE_LOG(LogJoltMover, Verbose, TEXT("Movement modes are expected to be parented to the MoverComponent. The %s movement mode was reparented to %s! (%s)"), *GetNameSafe(MovementMode), *GetNameSafe(this), *GetNameSafe(GetOwner()));
			MovementMode->Rename(nullptr, this, REN_DoNotDirty | REN_NonTransactional);
		}
		
		MovementModes.Add(ModeName, MovementMode);
		ModeFSM->RegisterMovementMode(ModeName, MovementMode);
	}
	else
	{
		UE_LOG(LogJoltMover, Warning, TEXT("Attempted to add %s movement mode that wasn't valid to %s. AddMovementModeFromObject did not add anything. (%s)"), *GetNameSafe(MovementMode), *GetNameSafe(this), *GetNameSafe(GetOwner()));
		return false;
	}

	return true;
}

bool UJoltMoverComponent::RemoveMovementMode(FName ModeName)
{
	if (ModeFSM->GetCurrentModeName() == ModeName)
	{
		UE_LOG(LogJoltMover, Warning, TEXT("The mode being removed (%s Movement Mode) is the mode this actor (%s) is currently in. It was removed but may cause issues. Consider waiting to remove the mode or queueing a different valid mode to avoid issues."), *ModeName.ToString(), *GetNameSafe(GetOwner()));
	}
	
	TObjectPtr<UJoltBaseMovementMode>* ModeToRemove = MovementModes.Find(ModeName);
	const bool ModeRemoved = MovementModes.Remove(ModeName) > 0;
	if (ModeRemoved && ModeToRemove)
	{
		ModeFSM->UnregisterMovementMode(ModeName);
		ModeToRemove->Get()->ConditionalBeginDestroy();
	}
	
	return ModeRemoved; 
}


/** Converts localspace root motion to a specific alternate worldspace location, taking the relative transform of the localspace component into account. */
static FTransform ConvertLocalRootMotionToAltWorldSpace(const FTransform& LocalRootMotionTransform, const FTransform& AltWorldspaceTransform, const USceneComponent& RelativeComp)
{
	const FTransform TrueActorToWorld = RelativeComp.GetOwner()->GetTransform();
	const FTransform RelativeCompToActor = TrueActorToWorld.GetRelativeTransform(RelativeComp.GetComponentTransform());

	const FTransform AltComponentWorldTransform = RelativeCompToActor.Inverse() * AltWorldspaceTransform;

	const FTransform NewComponentToWorld = LocalRootMotionTransform * AltComponentWorldTransform;
	const FTransform NewActorTransform = RelativeCompToActor * NewComponentToWorld;

	FTransform ActorDeltaTransform = NewActorTransform.GetRelativeTransform(AltWorldspaceTransform);
	
	return FTransform(ActorDeltaTransform.GetRotation(), NewActorTransform.GetTranslation() - AltWorldspaceTransform.GetTranslation());
}

FTransform UJoltMoverComponent::ConvertLocalRootMotionToWorld(const FTransform& LocalRootMotionTransform, float DeltaSeconds, const FTransform* AlternateActorToWorld, const FMotionWarpingUpdateContext* OptionalWarpingContext) const
{
	// Optionally process/warp localspace root motion
	const FTransform ProcessedLocalRootMotion = ProcessLocalRootMotionDelegate.IsBound()
		? ProcessLocalRootMotionDelegate.Execute(LocalRootMotionTransform, DeltaSeconds, OptionalWarpingContext)
		: LocalRootMotionTransform;

	// Convert processed localspace root motion to worldspace
	FTransform WorldSpaceRootMotion;

	if (USkeletalMeshComponent* SkeletalMesh = GetPrimaryVisualComponent<USkeletalMeshComponent>())
	{
		if (AlternateActorToWorld)
		{
			WorldSpaceRootMotion = ConvertLocalRootMotionToAltWorldSpace(ProcessedLocalRootMotion, *AlternateActorToWorld, *SkeletalMesh);
		}
		else
		{
			WorldSpaceRootMotion = SkeletalMesh->ConvertLocalRootMotionToWorld(ProcessedLocalRootMotion);
		}
	}
	else
	{
		const FTransform PresentationActorToWorldTransform = AlternateActorToWorld ? *AlternateActorToWorld : GetOwner()->GetTransform();
		const FVector DeltaWorldTranslation = ProcessedLocalRootMotion.GetTranslation() - PresentationActorToWorldTransform.GetTranslation();

		const FQuat NewWorldRotation = PresentationActorToWorldTransform.GetRotation() * ProcessedLocalRootMotion.GetRotation();
		const FQuat DeltaWorldRotation = NewWorldRotation * PresentationActorToWorldTransform.GetRotation().Inverse();

		WorldSpaceRootMotion.SetComponents(DeltaWorldRotation, DeltaWorldTranslation, FVector::OneVector);
	}

	// Optionally process/warp worldspace root motion
	return ProcessWorldRootMotionDelegate.IsBound()
		? ProcessWorldRootMotionDelegate.Execute(WorldSpaceRootMotion, DeltaSeconds, OptionalWarpingContext)
		: WorldSpaceRootMotion;
}


FTransform UJoltMoverComponent::GetUpdatedComponentTransform() const
{
	if (UpdatedComponent)
	{
		return UpdatedComponent->GetComponentTransform();
	}
	return FTransform::Identity;
}


void UJoltMoverComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	// Remove delegates from old component
	if (UpdatedComponent)
	{
		UpdatedComponent->SetShouldUpdatePhysicsVolume(false);
		UpdatedComponent->SetPhysicsVolume(nullptr, true);
		UpdatedComponent->PhysicsVolumeChangedDelegate.RemoveDynamic(this, &UJoltMoverComponent::PhysicsVolumeChanged);

		// remove from tick prerequisite
		UpdatedComponent->PrimaryComponentTick.RemovePrerequisite(this, PrimaryComponentTick);
	}

	if (UpdatedCompAsPrimitive)
	{
		UpdatedCompAsPrimitive->OnComponentBeginOverlap.RemoveDynamic(this, &UJoltMoverComponent::OnBeginOverlap);
	}

	// Don't assign pending kill components, but allow those to null out previous UpdatedComponent.
	UpdatedComponent = GetValid(NewUpdatedComponent);
	UpdatedCompAsPrimitive = Cast<UPrimitiveComponent>(UpdatedComponent);

	// Assign delegates
	if (IsValid(UpdatedComponent))
	{
		UpdatedComponent->SetShouldUpdatePhysicsVolume(true);
		UpdatedComponent->PhysicsVolumeChangedDelegate.AddUniqueDynamic(this, &UJoltMoverComponent::PhysicsVolumeChanged);

		if (!bInOnRegister && !bInInitializeComponent)
		{
			// UpdateOverlaps() in component registration will take care of this.
			UpdatedComponent->UpdatePhysicsVolume(true);
		}

		// force ticks after movement component updates
		UpdatedComponent->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
	}

	if (IsValid(UpdatedCompAsPrimitive))
	{
		UpdatedCompAsPrimitive->OnComponentBeginOverlap.AddDynamic(this, &UJoltMoverComponent::OnBeginOverlap);
	}

	UpdateTickRegistration();
}


USceneComponent* UJoltMoverComponent::GetUpdatedComponent() const
{
	return UpdatedComponent.Get();
}

USceneComponent* UJoltMoverComponent::GetPrimaryVisualComponent() const
{
	return PrimaryVisualComponent.Get();
}

void UJoltMoverComponent::SetPrimaryVisualComponent(USceneComponent* SceneComponent)
{
	if (SceneComponent && 
		ensureMsgf(SceneComponent->GetOwner() == GetOwner(), TEXT("Primary visual component must be owned by the same actor. MoverComp owner: %s  VisualComp owner: %s"), *GetNameSafe(GetOwner()), *GetNameSafe(SceneComponent->GetOwner())))
	{
		PrimaryVisualComponent = SceneComponent;
		BaseVisualComponentTransform = SceneComponent->GetRelativeTransform();
	}
	else
	{
		PrimaryVisualComponent = nullptr;
		BaseVisualComponentTransform = FTransform::Identity;
	}
}

FVector UJoltMoverComponent::GetVelocity() const
{ 
	if (LastMoverDefaultSyncState)
	{
		return LastMoverDefaultSyncState->GetVelocity_WorldSpace();
	}

	return FVector::ZeroVector;
}


FVector UJoltMoverComponent::GetMovementIntent() const
{ 
	if (LastMoverDefaultSyncState)
	{
		return LastMoverDefaultSyncState->GetIntent_WorldSpace();
	}

	return FVector::ZeroVector; 
}


FRotator UJoltMoverComponent::GetTargetOrientation() const
{
	// Prefer the input's intended orientation, but if it can't be determined, assume it matches the actual orientation
	const FJoltMoverInputCmdContext& LastInputCmd = GetLastInputCmd();
	if (const FJoltCharacterDefaultInputs* MoverInputs = LastInputCmd.Collection.FindDataByType<FJoltCharacterDefaultInputs>())
	{
		const FVector TargetOrientationDir = MoverInputs->GetOrientationIntentDir_WorldSpace();

		if (!TargetOrientationDir.IsNearlyZero())
		{
			return TargetOrientationDir.ToOrientationRotator();
		}
	}
	
	if (LastMoverDefaultSyncState)
	{
		return LastMoverDefaultSyncState->GetOrientation_WorldSpace();
	}

	return GetOwner() ? GetOwner()->GetActorRotation() : FRotator::ZeroRotator;
}


void UJoltMoverComponent::SetGravityOverride(bool bOverrideGravity, FVector NewGravityAcceleration)
{
	bHasGravityOverride = bOverrideGravity;
	GravityAccelOverride = NewGravityAcceleration;
	
	WorldToGravityTransform = FQuat::FindBetweenNormals(FVector::UpVector, -GravityAccelOverride.GetSafeNormal());
	GravityToWorldTransform = WorldToGravityTransform.Inverse();
}


FVector UJoltMoverComponent::GetGravityAcceleration() const
{
	if (bHasGravityOverride)
	{
		return GravityAccelOverride;
	}

	if (UpdatedComponent)
	{
		APhysicsVolume* CurPhysVolume = UpdatedComponent->GetPhysicsVolume();
		if (CurPhysVolume)
		{
			return CurPhysVolume->GetGravityZ() * FVector::UpVector;
		}
	}

	return JoltMoverComponentConstants::DefaultGravityAccel;
}

void UJoltMoverComponent::SetUpDirectionOverride(bool bOverrideUpDirection, FVector UpDirection)
{
	bHasUpDirectionOverride = bOverrideUpDirection;
	if (bOverrideUpDirection)
	{
		if (UpDirection.IsNearlyZero())
		{
			UE_LOG(LogJoltMover, Warning, TEXT("Ignoring the provided UpDirection (%s) override because it is a zero vector. (%s)"), *UpDirection.ToString(), *GetNameSafe(GetOwner()));
			bHasGravityOverride = false;
			return;
		}
		UpDirectionOverride = UpDirection.GetSafeNormal();
	}
}

FVector UJoltMoverComponent::GetUpDirection() const
{
	// Use the up direction override if enabled
	if (bHasUpDirectionOverride)
	{
		return UpDirectionOverride;
	}
	
	return UJoltMovementUtils::DeduceUpDirectionFromGravity(GetGravityAcceleration());
}

const FJoltPlanarConstraint& UJoltMoverComponent::GetPlanarConstraint() const
{
	return PlanarConstraint;
}

void UJoltMoverComponent::SetPlanarConstraint(const FJoltPlanarConstraint& InConstraint)
{
	PlanarConstraint = InConstraint;
}

void UJoltMoverComponent::SetBaseVisualComponentTransform(const FTransform& ComponentTransform)
{
	BaseVisualComponentTransform = ComponentTransform;
}

FTransform UJoltMoverComponent::GetBaseVisualComponentTransform() const
{
	return BaseVisualComponentTransform;
}

void UJoltMoverComponent::SetUseDeferredGroupMovement(bool bEnable)
{
	bUseDeferredGroupMovement = bEnable;

	// TODO update any necessary dependencies as needed
}

bool UJoltMoverComponent::IsUsingDeferredGroupMovement() const
{
	return bUseDeferredGroupMovement && USceneComponent::IsGroupedComponentMovementEnabled();
}

TArray<FJoltTrajectorySampleInfo> UJoltMoverComponent::GetFutureTrajectory(float FutureSeconds, float SamplesPerSecond)
{
	FJoltMoverPredictTrajectoryParams PredictionParams;
	PredictionParams.NumPredictionSamples = FMath::Max(1, FutureSeconds * SamplesPerSecond);
	PredictionParams.SecondsPerSample = FutureSeconds / (float)PredictionParams.NumPredictionSamples;

	return GetPredictedTrajectory(PredictionParams);
}

TArray<FJoltTrajectorySampleInfo> UJoltMoverComponent::GetPredictedTrajectory(FJoltMoverPredictTrajectoryParams PredictionParams)
{
	if (ModeFSM)
	{
		FJoltMoverTickStartData StepState;

		// Use the last-known input if none are specified.
		if (PredictionParams.OptionalInputCmds.IsEmpty())
		{
			StepState.InputCmd = GetLastInputCmd();
		}

		// Use preferred starting sync/aux state. Fall back to last-known state if not set.
		if (PredictionParams.OptionalStartSyncState.IsSet())
		{
			StepState.SyncState = PredictionParams.OptionalStartSyncState.GetValue();
		}
		else
		{
			StepState.SyncState = MoverSyncStateDoubleBuffer.GetReadable();
		}

		if (PredictionParams.OptionalStartAuxState.IsSet())
		{
			StepState.AuxState = PredictionParams.OptionalStartAuxState.GetValue();
		}
		else
		{
			StepState.AuxState = CachedLastAuxState;
		}


		FJoltMoverTimeStep FutureTimeStep;
		FutureTimeStep.StepMs = (PredictionParams.SecondsPerSample * 1000.f);
		FutureTimeStep.BaseSimTimeMs = CachedLastSimTickTimeStep.BaseSimTimeMs;
		FutureTimeStep.ServerFrame = 0;

		if (const UJoltBaseMovementMode* CurrentMovementMode = GetMovementMode())
		{
			if (FJoltUpdatedMotionState* StepSyncState = StepState.SyncState.Collection.FindMutableDataByType<FJoltUpdatedMotionState>())
			{
				const bool bOrigHasGravityOverride = bHasGravityOverride;
				const FVector OrigGravityAccelOverride = GravityAccelOverride;

				if (PredictionParams.bDisableGravity)
				{
					SetGravityOverride(true, FVector::ZeroVector);
				}

				TArray<FJoltTrajectorySampleInfo> OutSamples;
				OutSamples.SetNumUninitialized(PredictionParams.NumPredictionSamples);

				FVector PriorLocation = StepSyncState->GetLocation_WorldSpace();
				FRotator PriorOrientation = StepSyncState->GetOrientation_WorldSpace();
				FVector PriorVelocity = StepSyncState->GetVelocity_WorldSpace();

				for (int32 i = 0; i < PredictionParams.NumPredictionSamples; ++i)
				{
					// If no further inputs are specified, the previous input cmd will continue to be used
					if (i < PredictionParams.OptionalInputCmds.Num())
					{
						StepState.InputCmd = PredictionParams.OptionalInputCmds[i];
					}

					// Capture sample from current step state
					FJoltTrajectorySampleInfo& Sample = OutSamples[i];

					Sample.Transform.SetTranslationAndScale3D(StepSyncState->GetLocation_WorldSpace(), FVector::OneVector);
					Sample.Transform.SetRotation(StepSyncState->GetOrientation_WorldSpace().Quaternion());
					Sample.LinearVelocity = StepSyncState->GetVelocity_WorldSpace();
					Sample.InstantaneousAcceleration = (StepSyncState->GetVelocity_WorldSpace() - PriorVelocity) / PredictionParams.SecondsPerSample;
					Sample.AngularVelocity = (StepSyncState->GetOrientation_WorldSpace() - PriorOrientation) * (1.f / PredictionParams.SecondsPerSample);

					Sample.SimTimeMs = FutureTimeStep.BaseSimTimeMs;

					// Cache prior values
					PriorLocation = StepSyncState->GetLocation_WorldSpace();
					PriorOrientation = StepSyncState->GetOrientation_WorldSpace();
					PriorVelocity = StepSyncState->GetVelocity_WorldSpace();

					// Generate next move from current step state
					FJoltProposedMove StepMove;
					CurrentMovementMode->GenerateMove(StepState, FutureTimeStep, StepMove);

					// Advance state based on move
					StepSyncState->SetTransforms_WorldSpace(StepSyncState->GetLocation_WorldSpace() + (StepMove.LinearVelocity * PredictionParams.SecondsPerSample),
						UJoltMovementUtils::ApplyAngularVelocityToRotator(StepSyncState->GetOrientation_WorldSpace(),StepMove.AngularVelocityDegrees, PredictionParams.SecondsPerSample),
						StepMove.LinearVelocity,
						StepMove.AngularVelocityDegrees,
						StepSyncState->GetMovementBase(),
						StepSyncState->GetMovementBaseBoneName());

					FutureTimeStep.BaseSimTimeMs += FutureTimeStep.StepMs;
					++FutureTimeStep.ServerFrame;
				}

				// Put sample locations at visual root location if requested
				if (PredictionParams.bUseVisualComponentRoot)
				{
					if (const USceneComponent* VisualComp = GetPrimaryVisualComponent())
					{
						const FVector VisualCompOffset = VisualComp->GetRelativeLocation();
						const FTransform VisualCompRelativeTransform = VisualComp->GetRelativeTransform();

						for (int32 i=0; i < PredictionParams.NumPredictionSamples; ++i)
						{
							OutSamples[i].Transform = VisualCompRelativeTransform * OutSamples[i].Transform;
						}
					}
				}
				
				if (PredictionParams.bDisableGravity)
				{
					SetGravityOverride(bOrigHasGravityOverride, OrigGravityAccelOverride);
				}

				return OutSamples;
			}
		}
	}

	TArray<FJoltTrajectorySampleInfo> BlankDefaultSamples;
	BlankDefaultSamples.AddDefaulted(PredictionParams.NumPredictionSamples);
	return BlankDefaultSamples;
}


FName UJoltMoverComponent::GetMovementModeName() const
{ 
	return MoverSyncStateDoubleBuffer.GetReadable().MovementMode;
}

const UJoltBaseMovementMode* UJoltMoverComponent::GetMovementMode() const
{
	return GetActiveModeInternal(UJoltBaseMovementMode::StaticClass());
}

UPrimitiveComponent* UJoltMoverComponent::GetMovementBase() const
{
	if (LastMoverDefaultSyncState)
	{
		return LastMoverDefaultSyncState->GetMovementBase();
	}

	return nullptr;
}

FName UJoltMoverComponent::GetMovementBaseBoneName() const
{
	if (LastMoverDefaultSyncState)
	{
		return LastMoverDefaultSyncState->GetMovementBaseBoneName();
	}

	return NAME_None;
}

bool UJoltMoverComponent::HasValidCachedState() const
{
	return true;
}

const FJoltMoverSyncState& UJoltMoverComponent::GetSyncState() const
{
	return MoverSyncStateDoubleBuffer.GetReadable();
}

bool UJoltMoverComponent::TryGetFloorCheckHitResult(FHitResult& OutHitResult) const
{
	FJoltFloorCheckResult FloorCheck;
	if (SimBlackboard != nullptr && SimBlackboard->TryGet(CommonBlackboard::LastFloorResult, FloorCheck))
	{
		OutHitResult = FloorCheck.HitResult;
		return true;
	}
	return false;
}

const UJoltMoverBlackboard* UJoltMoverComponent::GetSimBlackboard() const
{
	return SimBlackboard;
}

UJoltMoverBlackboard* UJoltMoverComponent::GetSimBlackboard_Mutable() const
{
	return SimBlackboard;
}

bool UJoltMoverComponent::HasValidCachedInputCmd() const
{
	return true;
}

const FJoltMoverInputCmdContext& UJoltMoverComponent::GetLastInputCmd() const
{
	return CachedLastUsedInputCmd;
}

const FJoltMoverTimeStep& UJoltMoverComponent::GetLastTimeStep() const
{
	return CachedLastSimTickTimeStep;
}

IJoltMovementSettingsInterface* UJoltMoverComponent::FindSharedSettings_Mutable(const UClass* ByType) const
{
	check(ByType);

	for (const TObjectPtr<UObject>& SettingsObj : SharedSettings)
	{
		if (SettingsObj && SettingsObj->IsA(ByType))
		{
			return Cast<IJoltMovementSettingsInterface>(SettingsObj);
		}
	}

	return nullptr;
}

UObject* UJoltMoverComponent::FindSharedSettings_Mutable_BP(TSubclassOf<UObject> SharedSetting) const
{
	if (SharedSetting->ImplementsInterface(UJoltMovementSettingsInterface::StaticClass()))
    {
    	return Cast<UObject>(FindSharedSettings_Mutable(SharedSetting));
    }
    
    return nullptr;
}

const UObject* UJoltMoverComponent::FindSharedSettings_BP(TSubclassOf<UObject> SharedSetting) const
{
	if (SharedSetting->ImplementsInterface(UJoltMovementSettingsInterface::StaticClass()))
	{
		return Cast<UObject>(FindSharedSettings(SharedSetting));
	}

	return nullptr;
}

UJoltBaseMovementMode* UJoltMoverComponent::FindMode_Mutable(TSubclassOf<UJoltBaseMovementMode> ModeType, bool bRequireExactClass) const
{
	if (ModeType)
	{
		for (const TPair<FName, TObjectPtr<UJoltBaseMovementMode>>& NameModePair : MovementModes)
		{
			if ( (!bRequireExactClass && NameModePair.Value->IsA(ModeType)) || 
				 (NameModePair.Value->GetClass() == ModeType) )
			{
				return NameModePair.Value.Get();
			}
		}		
	}

	return nullptr;
}

UJoltBaseMovementMode* UJoltMoverComponent::FindMode_Mutable(TSubclassOf<UJoltBaseMovementMode> ModeType, FName ModeName, bool bRequireExactClass) const
{
	if (!ModeName.IsNone())
	{
		if (const TObjectPtr<UJoltBaseMovementMode>* FoundMode = MovementModes.Find(ModeName))
		{
			if ((!bRequireExactClass && FoundMode->IsA(ModeType)) || FoundMode->GetClass() == ModeType)
			{
				return *FoundMode;
			} 
		}
	}
	return nullptr;
}

UJoltBaseMovementMode* UJoltMoverComponent::GetActiveModeInternal(TSubclassOf<UJoltBaseMovementMode> ModeType, bool bRequireExactClass) const
{
	if (const TObjectPtr<UJoltBaseMovementMode>* CurrentMode = MovementModes.Find(GetMovementModeName()))
	{
		if ((!bRequireExactClass && CurrentMode->IsA(ModeType)) ||
			CurrentMode->GetClass() == ModeType)
		{
			return CurrentMode->Get();
		}
	}

	return nullptr;
}

bool UJoltMoverComponent::MakeAndQueueLayeredMove(const TSubclassOf<UJoltLayeredMoveLogic>& MoveLogicClass, const FJoltLayeredMoveActivationParams* ActivationParams)
{
	// Find registered type for class passed in
	TObjectPtr<UJoltLayeredMoveLogic> FoundRegisteredMoveLogic = nullptr;
	for (TObjectPtr<UJoltLayeredMoveLogic> RegisteredMoveLogic : RegisteredMoves)
	{
		if (RegisteredMoveLogic.GetClass()->IsChildOf(MoveLogicClass))
		{
			FoundRegisteredMoveLogic = RegisteredMoveLogic;
			break;
		}
	}
			
	UJoltLayeredMoveLogic* ActiveMoveLogic;
	TSharedPtr<FJoltLayeredMoveInstancedData> QueuedInstancedData;

	if (FoundRegisteredMoveLogic)
	{
		ActiveMoveLogic = FoundRegisteredMoveLogic;

		const UScriptStruct* InstancedDataType = FoundRegisteredMoveLogic->GetInstancedDataType();
		if (InstancedDataType && InstancedDataType->IsChildOf(FJoltLayeredMoveInstancedData::StaticStruct()))
		{
			TCheckedObjPtr<UScriptStruct> DataStructType = FoundRegisteredMoveLogic->GetInstancedDataType();
			FJoltLayeredMoveInstancedData* NewMove = (FJoltLayeredMoveInstancedData*)FMemory::Malloc(DataStructType->GetCppStructOps()->GetSize());
			DataStructType->InitializeStruct(NewMove);

			struct FAllocatedLayeredMoveDataDeleter
			{
				FORCEINLINE void operator()(FJoltLayeredMoveInstancedData* MoveData) const
				{
					check(MoveData);
					UScriptStruct* ScriptStruct = MoveData->GetScriptStruct();
					check(ScriptStruct);
					ScriptStruct->DestroyStruct(MoveData);
					FMemory::Free(MoveData);
				}
			};
				
			QueuedInstancedData = TSharedRef<FJoltLayeredMoveInstancedData>(NewMove, FAllocatedLayeredMoveDataDeleter());
			QueuedInstancedData->ActivateFromContext(ActivationParams);
		}
		else
		{
			UE_LOG(LogJoltMover, Warning, TEXT("%s activation was queued on %s but the move was NOT queued since it did not have valid data. InstancedDataStructType on Move Logic needs to be a FJoltLayeredMoveInstancedData or child struct of."),
				*MoveLogicClass->GetName(),
				*GetOwner()->GetName());
			
			return false;
		}
	}
	else
	{
		UE_LOG(LogJoltMover, Warning, TEXT("%s activation was queued on %s and the move was not registered. Any move activated on a MoverComponent Needs to be Registered with the MoverCompoent. The layered move will not be queued for activation."),
			*MoveLogicClass->GetName(),
			*GetOwner()->GetName());
			
		return false;
	}
	
	const TSharedPtr<FJoltLayeredMoveInstance> ActiveMoveToQueue = MakeShared<FJoltLayeredMoveInstance>(QueuedInstancedData.ToSharedRef(), ActiveMoveLogic);
	ModeFSM->QueueActiveLayeredMove(ActiveMoveToQueue);
	
	return true;
}


void UJoltMoverComponent::SetSimulationOutput(const FJoltMoverTimeStep& TimeStep, const UE::JoltMover::FJoltSimulationOutputData& OutputData)
{
	CachedLastSimTickTimeStep = TimeStep;

	CachedLastUsedInputCmd = OutputData.LastUsedInputCmd;

	FJoltMoverSyncState& BufferedSyncState = MoverSyncStateDoubleBuffer.GetWritable();
	BufferedSyncState = OutputData.SyncState;
	LastMoverDefaultSyncState = BufferedSyncState.Collection.FindDataByType<FJoltUpdatedMotionState>();
	MoverSyncStateDoubleBuffer.Flip();

	for (const TSharedPtr<FJoltMoverSimulationEventData>& EventData : OutputData.Events)
	{
		if (const FJoltMoverSimulationEventData* Data = EventData.Get())
		{
			DispatchSimulationEvent(*Data);
		}
	}

	// This is for things like the ground info that we want to cache and interpolate but isn't part of the networked sync state.
	// AdditionalOutputData is generic because ground info might not be useful for platforms, say, but platforms may want to store something else.
	SetAdditionalSimulationOutput(OutputData.AdditionalOutputData);
}

void UJoltMoverComponent::DispatchSimulationEvent(const FJoltMoverSimulationEventData& EventData)
{
	// This gives the event a callback when it is processed on the game thread
	FJoltMoverSimEventGameThreadContext GTContext({ this });
	EventData.OnEventProcessed(GTContext);

	// Process the simulation event at the mover component (or derived) level
	ProcessSimulationEvent(EventData);

	// Broadcast the event outside mover component
	if (OnPostSimEventReceived.IsBound())
	{
		OnPostSimEventReceived.Broadcast(EventData);
	}
}

void UJoltMoverComponent::ProcessSimulationEvent(const FJoltMoverSimulationEventData& EventData)
{
	// On a mode change call deactivate on the previous mode and activate on the new mode,
	// then broadcast the mode changed event
	if (const FJoltMovementModeChangedEventData* ModeChangedData = EventData.CastTo<FJoltMovementModeChangedEventData>())
	{
		if (ModeChangedData->PreviousModeName != NAME_None)
		{
			if (TObjectPtr<UJoltBaseMovementMode>* PrevModePtr = MovementModes.Find(ModeChangedData->PreviousModeName))
			{
				UJoltBaseMovementMode* PrevMode = PrevModePtr->Get();
				if (PrevMode && PrevMode->bSupportsAsync)
				{
					PrevMode->Deactivate_External();
				}
			}
		}

		if (ModeChangedData->NewModeName != NAME_None)
		{
			if (TObjectPtr<UJoltBaseMovementMode>* NewModePtr = MovementModes.Find(ModeChangedData->NewModeName))
			{
				UJoltBaseMovementMode* NewMode = NewModePtr->Get();
				if (NewMode && NewMode->bSupportsAsync)
				{
					NewMode->Activate_External();
				}
			}
		}

		OnMovementModeChanged.Broadcast(ModeChangedData->PreviousModeName, ModeChangedData->NewModeName);
	}
	else if (const FJoltTeleportSucceededEventData* TeleportSucceededEventData = EventData.CastTo<FJoltTeleportSucceededEventData>())
	{
		OnTeleportSucceeded.Broadcast(TeleportSucceededEventData->FromLocation, TeleportSucceededEventData->FromRotation, TeleportSucceededEventData->ToLocation, TeleportSucceededEventData->ToRotation);
	}
	else if (const FJoltTeleportFailedEventData* TeleportFailedEventData = EventData.CastTo<FJoltTeleportFailedEventData>())
	{
		OnTeleportFailed.Broadcast(TeleportFailedEventData->FromLocation, TeleportFailedEventData->FromRotation, TeleportFailedEventData->ToLocation, TeleportFailedEventData->ToRotation, TeleportFailedEventData->TeleportFailureReason);
	}
}

void UJoltMoverComponent::SetAdditionalSimulationOutput(const FJoltMoverDataCollection& Data)
{

}


void UJoltMoverComponent::CheckForExternalMovement(const FJoltMoverTickStartData& SimStartingData)
{
	if (!bWarnOnExternalMovement && !bAcceptExternalMovement)
	{
		return;
	}

	if (const FJoltUpdatedMotionState* StartingSyncState = SimStartingData.SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>())
	{		
		if (StartingSyncState->GetMovementBase())
		{
			return;	// TODO: need alternative handling of movement checks when based on another object
		}

		const FTransform& ComponentTransform = UpdatedComponent->GetComponentTransform();

		if (!ComponentTransform.GetLocation().Equals(StartingSyncState->GetLocation_WorldSpace()))
		{
			if (bWarnOnExternalMovement)
			{
				UE_LOG(LogJoltMover, Warning, TEXT("%s %s: Simulation start location (%s) disagrees with actual mover component location (%s). This indicates movement of the component out-of-band with the simulation, and may cause poor quality motion."),
					*GetNameSafe(GetOwner()),
					*StaticEnum<ENetRole>()->GetValueAsString(GetOwnerRole()),
					*StartingSyncState->GetLocation_WorldSpace().ToCompactString(),
					*UpdatedComponent->GetComponentLocation().ToCompactString());
			}

			if (bAcceptExternalMovement)
			{
				FJoltUpdatedMotionState* MutableSyncState = const_cast<FJoltUpdatedMotionState*>(StartingSyncState);

				MutableSyncState->SetTransforms_WorldSpace(ComponentTransform.GetLocation(), 
				                                           ComponentTransform.GetRotation().Rotator(),
				                                           MutableSyncState->GetVelocity_WorldSpace(),
				                                           MutableSyncState->GetAngularVelocityDegrees_WorldSpace());
			}
		}
	}
}



#pragma region JOLT PHYSICS

TArray<UPrimitiveComponent*> UJoltMoverComponent::GetSecondaryCollisionShapes_Implementation() const
{
	return TArray<UPrimitiveComponent*>();
}

void UJoltMoverComponent::InitializeWithJolt()
{
	CreateShapesForRootComponent();
	
	if (bShouldCreateSecondaryShapes)
	{
		CreateSecondaryShapes();
	}
}

void UJoltMoverComponent::CreateShapesForRootComponent()
{
	//TODO:@GreggoryAddison::CodeCompletion || Try and create the root shape for the character
}

void UJoltMoverComponent::CreateSecondaryShapes()
{
	const TArray<UPrimitiveComponent*> Comps(GetSecondaryCollisionShapes());
	for (const UPrimitiveComponent* C : Comps)
	{
		//TODO:@GreggoryAddison::CodeCompletion || Try and create the root shape for the character	
	}
	
}

#pragma endregion 


#undef LOCTEXT_NAMESPACE
