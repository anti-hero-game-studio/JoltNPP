// Copyright Epic Games, Inc. All Rights Reserved.

#include "Backends/JoltMoverStandaloneLiaison.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "JoltMoverComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMoverStandaloneLiaison)

namespace MoverStandaloneBackendCVars
{
	static void OnRunProduceInputOnAnyThreadChanged(IConsoleVariable* Var)
	{
		// Force a refresh of all liaisons when the global cvar changes to reflect new settings
		ForEachObjectOfClass(UJoltMoverStandaloneLiaisonComponent::StaticClass(), [&](UObject* AsObj)
		{
			if (UJoltMoverStandaloneLiaisonComponent* StandaloneLiaison = CastChecked<UJoltMoverStandaloneLiaisonComponent>(AsObj, ECastCheckedType::NullAllowed))
			{
				StandaloneLiaison->SetUseAsyncProduceInput(StandaloneLiaison->GetUseAsyncProduceInput());
			}
		});
	}
	
	static void OnRunMovementSimOnAnyThreadChanged(IConsoleVariable* Var)
	{
		// Force a refresh of all liaisons when the global cvar changes to reflect new settings
		ForEachObjectOfClass(UJoltMoverStandaloneLiaisonComponent::StaticClass(), [&](UObject* AsObj)
		{
			if (UJoltMoverStandaloneLiaisonComponent* StandaloneLiaison = CastChecked<UJoltMoverStandaloneLiaisonComponent>(AsObj, ECastCheckedType::NullAllowed))
			{
				StandaloneLiaison->SetUseAsyncMovementSimulationTick(StandaloneLiaison->GetUseAsyncMovementSimulationTick());
			}
		});
	}

	// Whether to allow produce input calls on any thread
	static int32 RunProduceInputOnAnyThread = 0;
	FAutoConsoleVariableRef CVarMoverStandaloneProduceInputOnAnyThread(
		TEXT("jolt.mover.standalone.RunProduceInputOnAnyThread"),
		RunProduceInputOnAnyThread,
		TEXT("Whether to allow produce input to run on any thread.\nIndividuals must also have their UseAsyncProduceInput option enabled.\n0: Game thread only, 1: Any thread"),
		FConsoleVariableDelegate::CreateStatic(&OnRunProduceInputOnAnyThreadChanged),
		ECVF_Default);
	
	// Whether to allow movement simulation ticks on any thread
	static int32 RunMovementSimOnAnyThread = 0;
	FAutoConsoleVariableRef CVarMoverStandaloneRunSimOnAnyThread(
		TEXT("jolt.mover.standalone.RunMovementSimOnAnyThread"),
		RunMovementSimOnAnyThread,
		TEXT("Whether to allow Mover simulation ticks to run on any thread. Requires use of threadsafe movement modes that do not modify scene components.\nIndividuals must also have their UseAsyncMovementSimulationTick option enabled.\n0: Game thread only, 1: Any thread"),
		FConsoleVariableDelegate::CreateStatic(&OnRunMovementSimOnAnyThreadChanged),
		ECVF_Default);

}	// end MoverStandaloneBackendCVars

UJoltMoverStandaloneLiaisonComponent::UJoltMoverStandaloneLiaisonComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bCanEverTick = false;

	ProduceInputTickFunction.bCanEverTick = true;
	ProduceInputTickFunction.bStartWithTickEnabled = true;
	ProduceInputTickFunction.SetTickFunctionEnable(true);
	ProduceInputTickFunction.TickGroup = TG_PrePhysics;
	ProduceInputTickFunction.bAllowTickBatching = true;
	ProduceInputTickFunction.bRunOnAnyThread = bUseAsyncProduceInput && MoverStandaloneBackendCVars::RunProduceInputOnAnyThread;

	SimulateMovementTickFunction.bCanEverTick = true;
	SimulateMovementTickFunction.bStartWithTickEnabled = true;
	SimulateMovementTickFunction.SetTickFunctionEnable(true);
	SimulateMovementTickFunction.TickGroup = TG_PrePhysics;
	SimulateMovementTickFunction.bRunOnAnyThread = bUseAsyncMovementSimulationTick && MoverStandaloneBackendCVars::RunMovementSimOnAnyThread;
	SimulateMovementTickFunction.bHighPriority = true;

	ApplyStateTickFunction.bCanEverTick = true;
	ApplyStateTickFunction.bStartWithTickEnabled = true;
	ApplyStateTickFunction.SetTickFunctionEnable(true);
	ApplyStateTickFunction.TickGroup = TG_PrePhysics;
	ApplyStateTickFunction.bHighPriority = true;	

	bWantsInitializeComponent = true;
	bAutoActivate = true;
	SetIsReplicatedByDefault(false);

	CurrentSimTimeMs = 0.0;
	CurrentSimFrame = 0;

}

double UJoltMoverStandaloneLiaisonComponent::GetCurrentSimTimeMs()
{
	return CurrentSimTimeMs;
}

int32 UJoltMoverStandaloneLiaisonComponent::GetCurrentSimFrame()
{
	return CurrentSimFrame;
}


bool UJoltMoverStandaloneLiaisonComponent::ReadPendingSyncState(OUT FJoltMoverSyncState& OutSyncState)
{
	{
		FRWScopeLock Lock(StateDataLock, SLT_ReadOnly);
		OutSyncState = CachedLastSyncState;
	}

	return true;
}

bool UJoltMoverStandaloneLiaisonComponent::WritePendingSyncState(const FJoltMoverSyncState& SyncStateToWrite)
{
	if (bIsInApplySimulationState)
	{
		UE_LOG(LogJoltMover, Log, TEXT("Attempted to WritePendingSyncState during ApplySimulationState. Skipping. (%s)"), *GetNameSafe(GetOwner()));
		return false;	// can't write to the sync state while it's being applied. Any changes from this will be overwritten anyway.
	}

	{
		FRWScopeLock Lock(StateDataLock, SLT_Write);
		CachedLastSyncState = SyncStateToWrite;
		bIsCachedStateDirty = true;
	}

	return true;
}

void UJoltMoverStandaloneLiaisonComponent::BeginPlay()
{
	Super::BeginPlay();

	UpdateSimulationTime();

	if (const AActor* OwnerActor = GetOwner())
	{
		ensureMsgf(OwnerActor->GetNetMode() == NM_Standalone, TEXT("UJoltMoverStandaloneLiaisonComponent is only valid for use in Standalone projects. Movement will not work properly in networked play."));

		if (UJoltMoverComponent* FoundMoverComp = OwnerActor->FindComponentByClass<UJoltMoverComponent>())
		{
			MoverComp = FoundMoverComp;

			FRWScopeLock Lock(StateDataLock, SLT_Write);
			MoverComp->InitializeSimulationState(OUT &CachedLastSyncState, OUT &CachedLastAuxState);
			bIsCachedStateDirty = true;
		}
		else
		{
			ensureMsgf(MoverComp, TEXT("Owning actor %s does not have a MoverComponent."), *GetNameSafe(GetOwner()));

			// Disable tick if we don't have mover for some reason..
			SetComponentTickEnabled(false);
			ProduceInputTickFunction.SetTickFunctionEnable(false);
			SimulateMovementTickFunction.SetTickFunctionEnable(false);
			ApplyStateTickFunction.SetTickFunctionEnable(false);
		}
	}
}

FTickFunction* UJoltMoverStandaloneLiaisonComponent::FindTickFunction(EJoltMoverTickPhase MoverTickPhase)
{
	FTickFunction* FoundFunction = nullptr;

	switch (MoverTickPhase)
	{
		case EJoltMoverTickPhase::ProduceInput:
			FoundFunction = &ProduceInputTickFunction;
			break;

		case EJoltMoverTickPhase::SimulateMovement:
			FoundFunction = &SimulateMovementTickFunction;
			break;

		case EJoltMoverTickPhase::ApplyState:
			FoundFunction = &ApplyStateTickFunction;
			break;
			
		default:
			break;
	}

	return FoundFunction;
}

void UJoltMoverStandaloneLiaisonComponent::AddTickDependency(UActorComponent* OtherComponent, EJoltMoverTickDependencyOrder TickOrder, EJoltMoverTickPhase TickPhase)
{
	ensure(OtherComponent);
	FTickFunction* TickFunction = FindTickFunction(TickPhase);
	if (OtherComponent && TickFunction)
	{
		switch (TickOrder)
		{
			case EJoltMoverTickDependencyOrder::After:
				TickFunction->RemovePrerequisite(OtherComponent, OtherComponent->PrimaryComponentTick);
				OtherComponent->PrimaryComponentTick.AddPrerequisite(this, *TickFunction);
				break;
			case EJoltMoverTickDependencyOrder::Before:
				OtherComponent->PrimaryComponentTick.RemovePrerequisite(this, *TickFunction);
				TickFunction->AddPrerequisite(OtherComponent, OtherComponent->PrimaryComponentTick);
				break;
			
			default:
				break;
		}
	}
}

void UJoltMoverStandaloneLiaisonComponent::SetUseAsyncProduceInput(bool bUseAsyncInputProduction)
{
	bUseAsyncProduceInput = bUseAsyncInputProduction;
	ProduceInputTickFunction.bRunOnAnyThread = bUseAsyncProduceInput && MoverStandaloneBackendCVars::RunProduceInputOnAnyThread;
}

bool UJoltMoverStandaloneLiaisonComponent::GetUseAsyncProduceInput() const
{
	return bUseAsyncProduceInput;
}

void UJoltMoverStandaloneLiaisonComponent::SetEnableProduceInput(bool bEnableInputProduction)
{
	ProduceInputTickFunction.SetTickFunctionEnable(bEnableInputProduction);
	ProduceInputTickFunction.bStartWithTickEnabled = bEnableInputProduction;

	if (HasBegunPlay())
	{
		if (!bEnableInputProduction && ProduceInputTickFunction.IsTickFunctionRegistered())
		{
			ProduceInputTickFunction.UnRegisterTickFunction();
		}
		else if (bEnableInputProduction && !ProduceInputTickFunction.IsTickFunctionRegistered())
		{
			const AActor* Owner = GetOwner();
			ULevel* ComponentLevel = (Owner ? Owner->GetLevel() : ToRawPtr(GetWorld()->PersistentLevel));
			ProduceInputTickFunction.RegisterTickFunction(ComponentLevel);
		}
	}
}

bool UJoltMoverStandaloneLiaisonComponent::GetEnableProduceInput() const
{
	return ProduceInputTickFunction.IsTickFunctionEnabled();
}

void UJoltMoverStandaloneLiaisonComponent::SetUseAsyncMovementSimulationTick(bool bUseAsyncMovementSim)
{
	bUseAsyncMovementSimulationTick = bUseAsyncMovementSim;
	SimulateMovementTickFunction.bRunOnAnyThread = bUseAsyncMovementSimulationTick && MoverStandaloneBackendCVars::RunMovementSimOnAnyThread;
}

bool UJoltMoverStandaloneLiaisonComponent::GetUseAsyncMovementSimulationTick() const
{
	return bUseAsyncMovementSimulationTick;
}


void UJoltMoverStandaloneLiaisonComponent::RegisterComponentTickFunctions(bool bRegister)
{
	Super::RegisterComponentTickFunctions(bRegister);

	if (bRegister)
	{
		if (SetupActorComponentTickFunction(&ProduceInputTickFunction))
		{
			// Set up tick dependencies so this order always holds true:
			//   1) Controller update, which triggers input events
			//   2) Mover input production for the next movement simulation step
			//   3) Perform simulation step to generate new state
			//   4) Apply new state to actor/components

			ProduceInputTickFunction.Target = this;

			// Input production should always wait for the controller update, and we will watch for controller changes
			if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
			{
				if (AController* OwnerController = OwnerPawn->GetController())
				{
					ProduceInputTickFunction.AddPrerequisite(OwnerController, OwnerController->PrimaryActorTick);
				}

				OwnerPawn->ReceiveControllerChangedDelegate.AddDynamic(this, &UJoltMoverStandaloneLiaisonComponent::OnControllerChanged);
			}

			if (SetupActorComponentTickFunction(&SimulateMovementTickFunction))
			{
				SimulateMovementTickFunction.Target = this;
				SimulateMovementTickFunction.AddPrerequisite(this, this->ProduceInputTickFunction);

				if (SetupActorComponentTickFunction(&ApplyStateTickFunction))
				{
					ApplyStateTickFunction.Target = this;
					ApplyStateTickFunction.AddPrerequisite(this, this->SimulateMovementTickFunction);
				}
			}
		}
	}
	else
	{
		if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
		{
			if (AController* OwnerController = OwnerPawn->GetController())
			{
				ProduceInputTickFunction.RemovePrerequisite(OwnerController, OwnerController->PrimaryActorTick);
			}

			OwnerPawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &UJoltMoverStandaloneLiaisonComponent::OnControllerChanged);
		}

		if (ProduceInputTickFunction.IsTickFunctionRegistered())
		{
			ProduceInputTickFunction.UnRegisterTickFunction();
		}

		if (SimulateMovementTickFunction.IsTickFunctionRegistered())
		{
			SimulateMovementTickFunction.UnRegisterTickFunction();
		}

		if (ApplyStateTickFunction.IsTickFunctionRegistered())
		{
			ApplyStateTickFunction.UnRegisterTickFunction();
		}
	}
}


void UJoltMoverStandaloneLiaisonComponent::UpdateSimulationTime()
{
	CurrentSimTimeMs = GetWorld()->GetTimeSeconds() * 1000.0;
	CurrentSimFrame = GFrameCounter;
}

void UJoltMoverStandaloneLiaisonComponent::TickInputProduction(float DeltaSeconds)
{
	UpdateSimulationTime();

	const int32 DeltaTimeMs = DeltaSeconds * 1000.f;

	FJoltMoverInputCmdContext InputCmd;
	{
		SCOPED_NAMED_EVENT(StandaloneMoverBackendLiaison_Tick_ProduceInput, FColor::Yellow);
		MoverComp->ProduceInput(DeltaTimeMs, OUT & LastProducedInputCmd);
	}
}

void UJoltMoverStandaloneLiaisonComponent::TickMovementSimulation(float DeltaSeconds)
{
	SCOPED_NAMED_EVENT(StandaloneMoverBackendLiaison_Tick_MovementSimulation, FColor::Blue);

	if (!GetEnableProduceInput())
	{
		// If input production is turned off, we need to update current time ourselves
		UpdateSimulationTime();
	}

	FJoltMoverTimeStep TimeStep;
	TimeStep.ServerFrame = CurrentSimFrame;
	TimeStep.BaseSimTimeMs = CurrentSimTimeMs;
	TimeStep.StepMs = DeltaSeconds * 1000.f;

	WorkingStartData.InputCmd = LastProducedInputCmd;

	WorkingEndData.InitForNewFrame();

	{
		FRWScopeLock Lock(StateDataLock, SLT_ReadOnly);
		WorkingStartData.SyncState = CachedLastSyncState;
		WorkingStartData.AuxState = CachedLastAuxState;
	}

	{
		SCOPED_NAMED_EVENT(StandaloneMoverBackendLiaison_Tick_SimulationOnly, FColor::Blue);
		MoverComp->SimulationTick(TimeStep, WorkingStartData, OUT WorkingEndData);
	}

	{
		if (!CachedLastSyncState.HasSameContents(WorkingEndData.SyncState) ||
			!WorkingEndData.MovementEndState.bEndedWithNoChanges)
		{
			FRWScopeLock Lock(StateDataLock, SLT_Write);
			CachedLastSyncState = WorkingEndData.SyncState;
			CachedLastAuxState = WorkingEndData.AuxState;
			bIsCachedStateDirty = true;
		}
	}

}



void UJoltMoverStandaloneLiaisonComponent::TickApplySimulationState(float DeltaSeconds)
{
	TGuardValue<bool> InApplySimulationState(bIsInApplySimulationState, true);

	{
		SCOPED_NAMED_EVENT(StandaloneMoverBackendLiaison_Tick_FinalizeFrame, FColor::Green);

		bool bNeedsFinalizeUnchanged = true;

		{
			FRWScopeLock Lock(StateDataLock, SLT_ReadOnly);
			if (bIsCachedStateDirty)
			{
				MoverComp->FinalizeFrame(&CachedLastSyncState, &CachedLastAuxState);

				bIsCachedStateDirty = false;
				bNeedsFinalizeUnchanged = false;
			}
		}

		if (bNeedsFinalizeUnchanged)
		{
			MoverComp->FinalizeUnchangedFrame();
		}
	}
}

void UJoltMoverStandaloneLiaisonComponent::OnControllerChanged(APawn* Pawn, AController* OldController, AController* NewController)
{
	if (OldController)
	{
		ProduceInputTickFunction.RemovePrerequisite(OldController, OldController->PrimaryActorTick);
	}

	if (NewController)
	{
		ProduceInputTickFunction.AddPrerequisite(NewController, NewController->PrimaryActorTick);
	}
}


// FJoltMoverStandaloneProduceInputTickFunction ---------------------

void FJoltMoverStandaloneProduceInputTickFunction::ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	// ExecuteTickHelper does the null check
	UJoltMoverStandaloneLiaisonComponent* TargetComp = Target.Get();
	FActorComponentTickFunction::ExecuteTickHelper(TargetComp, /*bTickInEditor=*/ false, DeltaTime, TickType, [TargetComp](float DilatedTime)
		{
			TargetComp->TickInputProduction(DilatedTime);
		});
}

FString FJoltMoverStandaloneProduceInputTickFunction::DiagnosticMessage()
{
	return Target->GetFullName() + TEXT("[UJoltMoverStandaloneLiaisonComponent::ProduceInputTick]");
}

FName FJoltMoverStandaloneProduceInputTickFunction::DiagnosticContext(bool bDetailed)
{
	if (bDetailed)
	{
		return FName(*FString::Printf(TEXT("JoltMoverStandaloneProduceInputTickFunction/%s"), *GetFullNameSafe(Target.Get())));
	}

	static const FName MoverStandaloneProduceInputTickFunctionFName(TEXT("JoltMoverStandaloneProduceInputTickFunction"));
	return MoverStandaloneProduceInputTickFunctionFName;
}

// FJoltMoverStandaloneSimulateMovementTickFunction ---------------------

void FJoltMoverStandaloneSimulateMovementTickFunction::ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	UJoltMoverStandaloneLiaisonComponent* TargetComp = Target.Get();
	FActorComponentTickFunction::ExecuteTickHelper(TargetComp, /*bTickInEditor=*/ false, DeltaTime, TickType, [TargetComp](float DilatedTime)
		{
			TargetComp->TickMovementSimulation(DilatedTime);
		});
}

FString FJoltMoverStandaloneSimulateMovementTickFunction::DiagnosticMessage()
{
	return Target->GetFullName() + TEXT("[UJoltMoverStandaloneLiaisonComponent::SimulateMovement]");
}

FName FJoltMoverStandaloneSimulateMovementTickFunction::DiagnosticContext(bool bDetailed)
{
	if (bDetailed)
	{
		return FName(*FString::Printf(TEXT("JoltMoverStandaloneSimulateMovementTickFunction/%s"), *GetFullNameSafe(Target.Get())));
	}

	static const FName MoverStandaloneSimulateMovementTickFunctionFName(TEXT("JoltMoverStandaloneSimulateMovementTickFunction"));
	return MoverStandaloneSimulateMovementTickFunctionFName;
}


// FJoltMoverStandaloneApplyStateTickFunction ---------------------

void FJoltMoverStandaloneApplyStateTickFunction::ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	UJoltMoverStandaloneLiaisonComponent* TargetComp = Target.Get();
	FActorComponentTickFunction::ExecuteTickHelper(TargetComp, /*bTickInEditor=*/ false, DeltaTime, TickType, [TargetComp](float DilatedTime)
		{
			TargetComp->TickApplySimulationState(DilatedTime);
		});
}

FString FJoltMoverStandaloneApplyStateTickFunction::DiagnosticMessage()
{
	return Target->GetFullName() + TEXT("[UJoltMoverStandaloneLiaisonComponent::ApplyState]");
}

FName FJoltMoverStandaloneApplyStateTickFunction::DiagnosticContext(bool bDetailed)
{
	if (bDetailed)
	{
		return FName(*FString::Printf(TEXT("JoltMoverStandaloneApplyStateTickFunction/%s"), *GetFullNameSafe(Target.Get())));
	}

	static const FName MoverStandaloneApplyStateTickFunctionFName(TEXT("JoltMoverStandaloneApplyStateTickFunction"));
	return MoverStandaloneApplyStateTickFunctionFName;
}
