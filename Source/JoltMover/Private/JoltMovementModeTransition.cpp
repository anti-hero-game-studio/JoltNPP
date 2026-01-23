// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltMovementModeTransition.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "JoltMoverComponent.h"
#include "JoltMoverSimulationTypes.h"
#include "JoltMoverTypes.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMovementModeTransition)

const FJoltTransitionEvalResult FJoltTransitionEvalResult::NoTransition = FJoltTransitionEvalResult();

UWorld* UJoltBaseMovementModeTransition::GetWorld() const
{
	if (UJoltMoverComponent* MoverComponent = GetMoverComponent())
	{
		return MoverComponent->GetWorld();
	}
	return nullptr;
}

void UJoltBaseMovementModeTransition::OnRegistered()
{
	K2_OnRegistered();
}

void UJoltBaseMovementModeTransition::OnUnregistered()
{
	K2_OnUnregistered();
}

UJoltMoverComponent* UJoltBaseMovementModeTransition::K2_GetMoverComponent() const
{
	// Transitions can belong to either a mode or the component itself - either way they're always ultimately outer'd to a mover comp
	return GetTypedOuter<UJoltMoverComponent>();
}

FJoltTransitionEvalResult UJoltBaseMovementModeTransition::Evaluate_Implementation(const FJoltSimulationTickParams& Params) const
{
	return FJoltTransitionEvalResult::NoTransition;
}

void UJoltBaseMovementModeTransition::Trigger_Implementation(const FJoltSimulationTickParams& Params)
{
}

#if WITH_EDITOR
EDataValidationResult UJoltBaseMovementModeTransition::IsDataValid(FDataValidationContext& Context) const
{
	return EDataValidationResult::Valid;
}
#endif // WITH_EDITOR

UJoltImmediateMovementModeTransition::UJoltImmediateMovementModeTransition(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Clear();
}

FJoltTransitionEvalResult UJoltImmediateMovementModeTransition::Evaluate_Implementation(const FJoltSimulationTickParams& Params) const
{
	if (NextMode != NAME_None)
	{
		if (bAllowModeReentry)
		{
			return FJoltTransitionEvalResult(NextMode);
		}
		else if (NextMode != Params.StartState.SyncState.MovementMode)
		{
			return FJoltTransitionEvalResult(NextMode);
		}
	}

	return FJoltTransitionEvalResult::NoTransition;
}

void UJoltImmediateMovementModeTransition::Trigger_Implementation(const FJoltSimulationTickParams& Params)
{
	Clear();
}

void UJoltImmediateMovementModeTransition::SetNextMode(FName DesiredModeName, bool bShouldReenter)
{
	NextMode = DesiredModeName;
	bAllowModeReentry = bShouldReenter;
}

void UJoltImmediateMovementModeTransition::Clear()
{
	NextMode = NAME_None;
	bAllowModeReentry = false;
}
