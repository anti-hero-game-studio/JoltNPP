// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltMoverModule.h"

#include "Debug/JoltMoverDebugComponent.h"
#include "HAL/ConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebugger.h"
#include "Debug/GameplayDebuggerCategory_JoltMover.h"
#define JOLT_MOVER_CATEGORY_NAME "JoltMover"
#endif // WITH_GAMEPLAY_DEBUGGER

#define LOCTEXT_NAMESPACE "FJoltMoverModule"


namespace UE::JoltMover
{
	int32 DisableDataCopyInPlace = 0;
	static FAutoConsoleVariableRef CVarDisableDataCopyInPlace(
		TEXT("jolt.mover.debug.DisableDataCopyInPlace"),
		DisableDataCopyInPlace,
		TEXT("Whether to allow Mover data collections with identical contained struct types to be copied in place, avoiding reallocating memory"),
		ECVF_Default);
}


void FJoltMoverModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
	TEXT("JoltMover.LocalPlayer.ShowTrail"),
	TEXT("Toggles showing the players trail according to the mover component. Trail will show previous path and some information on rollbacks. NOTE: this is applied the first local player controller."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateRaw(this, &FJoltMoverModule::ShowTrail),
	ECVF_Cheat
	));
	
	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
	TEXT("JoltMover.LocalPlayer.ShowTrajectory"),
	TEXT("Toggles showing the players trajectory according to the mover component. NOTE: this is applied the first local player controller"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateRaw(this, &FJoltMoverModule::ShowTrajectory),
	ECVF_Cheat
	));
	
	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
	TEXT("JoltMover.LocalPlayer.ShowCorrections"),
	TEXT("Toggles showing corrections that were applied to the actor. Green is the updated position after correction, Red was the position before correction. NOTE: this is applied the first local player controller."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateRaw(this, &FJoltMoverModule::ShowCorrections),
	ECVF_Cheat
	));
	
#if WITH_GAMEPLAY_DEBUGGER
	IGameplayDebugger& GameplayDebuggerModule = IGameplayDebugger::Get();
	GameplayDebuggerModule.RegisterCategory(JOLT_MOVER_CATEGORY_NAME, IGameplayDebugger::FOnGetCategory::CreateStatic(&FGameplayDebuggerCategory_JoltMover::MakeInstance));
	GameplayDebuggerModule.NotifyCategoriesChanged();
#endif // WITH_GAMEPLAY_DEBUGGER
}

void FJoltMoverModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

#if WITH_GAMEPLAY_DEBUGGER
	if (IGameplayDebugger::IsAvailable())
	{
		IGameplayDebugger& GameplayDebuggerModule = IGameplayDebugger::Get();
		GameplayDebuggerModule.UnregisterCategory(JOLT_MOVER_CATEGORY_NAME);
		GameplayDebuggerModule.NotifyCategoriesChanged();
	}
#endif // WITH_GAMEPLAY_DEBUGGER
}

void FJoltMoverModule::ShowTrajectory(const TArray<FString>& Args, UWorld* World)
{
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		APawn* MyPawn = PC->GetPawn();
		if (UJoltMoverDebugComponent* MoverDebugComponent = MyPawn ? Cast<UJoltMoverDebugComponent>(MyPawn->GetComponentByClass(UJoltMoverDebugComponent::StaticClass())) : nullptr)
		{
			MoverDebugComponent->bShowTrajectory = !MoverDebugComponent->bShowTrajectory;
		}
		else
		{
			UJoltMoverDebugComponent* NewMoverDebugComponent = Cast<UJoltMoverDebugComponent>(MyPawn->AddComponentByClass(UJoltMoverDebugComponent::StaticClass(), false, FTransform::Identity, false));
			NewMoverDebugComponent->bShowTrajectory = true;
			NewMoverDebugComponent->bShowTrail = false;
			NewMoverDebugComponent->bShowCorrections = false;
			NewMoverDebugComponent->SetHistoryTracking(1.0f, 20.0f);
		}
	}
}
void FJoltMoverModule::ShowTrail(const TArray<FString>& Args, UWorld* World)
{
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		APawn* MyPawn = PC->GetPawn();
		if (UJoltMoverDebugComponent* MoverDebugComponent = MyPawn ? Cast<UJoltMoverDebugComponent>(MyPawn->GetComponentByClass(UJoltMoverDebugComponent::StaticClass())) : nullptr)
		{
			MoverDebugComponent->bShowTrail = !MoverDebugComponent->bShowTrail;
		}
		else
		{
			UJoltMoverDebugComponent* NewMoverDebugComponent = Cast<UJoltMoverDebugComponent>(MyPawn->AddComponentByClass(UJoltMoverDebugComponent::StaticClass(), false, FTransform::Identity, false));
			NewMoverDebugComponent->bShowTrail = true;
			NewMoverDebugComponent->bShowTrajectory = false;
			NewMoverDebugComponent->bShowCorrections = false;
			NewMoverDebugComponent->SetHistoryTracking(1.0f, 20.0f);
		}
	}
}

void FJoltMoverModule::ShowCorrections(const TArray<FString>& Args, UWorld* World)
{
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		APawn* MyPawn = PC->GetPawn();
		if (UJoltMoverDebugComponent* MoverDebugComponent = MyPawn ? Cast<UJoltMoverDebugComponent>(MyPawn->GetComponentByClass(UJoltMoverDebugComponent::StaticClass())) : nullptr)
		{
			MoverDebugComponent->bShowCorrections = !MoverDebugComponent->bShowCorrections;
		}
		else
		{
			UJoltMoverDebugComponent* NewMoverDebugComponent = Cast<UJoltMoverDebugComponent>(MyPawn->AddComponentByClass(UJoltMoverDebugComponent::StaticClass(), false, FTransform::Identity, false));
			NewMoverDebugComponent->bShowTrail = false;
			NewMoverDebugComponent->bShowTrajectory = false;
			NewMoverDebugComponent->bShowCorrections = true;
			NewMoverDebugComponent->SetHistoryTracking(1.0f, 20.0f);
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef JOLT_MOVER_CATEGORY_NAME
	
IMPLEMENT_MODULE(FJoltMoverModule, JoltMover)