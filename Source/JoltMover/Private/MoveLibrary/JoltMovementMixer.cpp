// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoveLibrary/JoltMovementMixer.h"
#include "JoltLayeredMove.h"
#include "JoltLayeredMoveBase.h"
#include "JoltMoverLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMovementMixer)

UJoltMovementMixer::UJoltMovementMixer()
	: CurrentHighestPriority(0)
	, CurrentLayeredMoveStartTime(TNumericLimits<float>::Max())
{
}

void UJoltMovementMixer::MixLayeredMove(const FJoltLayeredMoveBase& ActiveMove, const FJoltProposedMove& MoveStep, FJoltProposedMove& OutCumulativeMove)
{
	if (OutCumulativeMove.PreferredMode != MoveStep.PreferredMode && !OutCumulativeMove.PreferredMode.IsNone() && !MoveStep.PreferredMode.IsNone())
	{
		UE_LOG(LogJoltMover, Log, TEXT("Multiple LayeredMoves are conflicting with preferred moves. %s will override %s"),
			*MoveStep.PreferredMode.ToString(), *OutCumulativeMove.PreferredMode.ToString());
	}

	if (MoveStep.bHasDirIntent && OutCumulativeMove.MixMode != EJoltMoveMixMode::OverrideAll && ActiveMove.Priority >= CurrentHighestPriority)
	{
		if (OutCumulativeMove.bHasDirIntent)
		{
			UE_LOG(LogJoltMover, Log, TEXT("Multiple LayeredMoves are setting direction intent and the layered move with highest priority will be used."));
		}
				
		OutCumulativeMove.bHasDirIntent = MoveStep.bHasDirIntent;
		OutCumulativeMove.DirectionIntent = MoveStep.DirectionIntent;
	}

	if (MoveStep.MixMode == EJoltMoveMixMode::OverrideVelocity)
	{
		if (CheckPriority(&ActiveMove, CurrentHighestPriority, CurrentLayeredMoveStartTime))
		{
			if (OutCumulativeMove.MixMode == EJoltMoveMixMode::OverrideVelocity || OutCumulativeMove.MixMode == EJoltMoveMixMode::OverrideAll)
			{
				UE_LOG(LogJoltMover, Log, TEXT("Multiple LayeredMoves with Override mix mode are active simultaneously. Layered move with the highest priority will take effect."));
			}

			if (!MoveStep.PreferredMode.IsNone() && OutCumulativeMove.MixMode != EJoltMoveMixMode::OverrideAll)
			{
				OutCumulativeMove.PreferredMode = MoveStep.PreferredMode;
			}
				
			OutCumulativeMove.MixMode = EJoltMoveMixMode::OverrideVelocity;
			OutCumulativeMove.LinearVelocity  = MoveStep.LinearVelocity;
			OutCumulativeMove.AngularVelocityDegrees = MoveStep.AngularVelocityDegrees;
		}
	}
	else if (MoveStep.MixMode == EJoltMoveMixMode::AdditiveVelocity)
	{
		if (OutCumulativeMove.MixMode != EJoltMoveMixMode::OverrideVelocity && OutCumulativeMove.MixMode != EJoltMoveMixMode::OverrideAll)
		{
			if (!MoveStep.PreferredMode.IsNone())
			{
				OutCumulativeMove.PreferredMode = MoveStep.PreferredMode;
			}

			OutCumulativeMove.LinearVelocity += MoveStep.LinearVelocity;
			OutCumulativeMove.AngularVelocityDegrees += MoveStep.AngularVelocityDegrees;
		}
	}
	else if (MoveStep.MixMode == EJoltMoveMixMode::OverrideAll)
	{
		if (CheckPriority(&ActiveMove, CurrentHighestPriority, CurrentLayeredMoveStartTime))
		{
			if (OutCumulativeMove.MixMode == EJoltMoveMixMode::OverrideVelocity || OutCumulativeMove.MixMode == EJoltMoveMixMode::OverrideAll)
			{
				UE_LOG(LogJoltMover, Log, TEXT("Multiple LayeredMoves with Override mix mode are active simultaneously. Layered move with the highest priority will take effect."));
			}
				
			OutCumulativeMove = MoveStep;
			OutCumulativeMove.MixMode = EJoltMoveMixMode::OverrideAll;
		}
	}
	else if (MoveStep.MixMode == EJoltMoveMixMode::OverrideAllExceptVerticalVelocity)
	{
		if (CheckPriority(&ActiveMove, CurrentHighestPriority, CurrentLayeredMoveStartTime))
		{
			if (OutCumulativeMove.MixMode == EJoltMoveMixMode::OverrideVelocity || OutCumulativeMove.MixMode == EJoltMoveMixMode::OverrideAll || OutCumulativeMove.MixMode == EJoltMoveMixMode::OverrideAllExceptVerticalVelocity)
			{
				UE_LOG(LogJoltMover, Log, TEXT("Multiple LayeredMoves with Override mix mode are active simultaneously. Layered move with the highest priority will take effect."));
			}

			OutCumulativeMove = MoveStep;
			OutCumulativeMove.MixMode = EJoltMoveMixMode::OverrideAllExceptVerticalVelocity;
		}
	}
	else
	{
		ensureMsgf(false, TEXT("Unhandled move mix mode was found."));
	}
}

void UJoltMovementMixer::MixLayeredMove(const FJoltLayeredMoveInstance& ActiveMove, const FJoltProposedMove& MoveStep, FJoltProposedMove& OutCumulativeMove)
{
	if (OutCumulativeMove.PreferredMode != MoveStep.PreferredMode && !OutCumulativeMove.PreferredMode.IsNone() && !MoveStep.PreferredMode.IsNone())
	{
		UE_LOG(LogJoltMover, Log, TEXT("Multiple LayeredMoves are conflicting with preferred moves. %s will override %s"),
			*MoveStep.PreferredMode.ToString(), *OutCumulativeMove.PreferredMode.ToString());
	}

	if (MoveStep.bHasDirIntent && OutCumulativeMove.MixMode != EJoltMoveMixMode::OverrideAll && ActiveMove.GetPriority() >= CurrentHighestPriority)
	{
		if (OutCumulativeMove.bHasDirIntent)
		{
			UE_LOG(LogJoltMover, Log, TEXT("Multiple LayeredMoves are setting direction intent and the layered move with highest priority will be used."));
		}
				
		OutCumulativeMove.bHasDirIntent = MoveStep.bHasDirIntent;
		OutCumulativeMove.DirectionIntent = MoveStep.DirectionIntent;
	}

	if (MoveStep.MixMode == EJoltMoveMixMode::OverrideVelocity)
	{
		if (CheckPriority(ActiveMove.GetPriority(), ActiveMove.GetStartingTimeMs(), CurrentHighestPriority, CurrentLayeredMoveStartTime))
		{
			if (OutCumulativeMove.MixMode == EJoltMoveMixMode::OverrideVelocity || OutCumulativeMove.MixMode == EJoltMoveMixMode::OverrideAll)
			{
				UE_LOG(LogJoltMover, Log, TEXT("Multiple LayeredMoves with Override mix mode are active simultaneously. Layered move with the highest priority will take effect."));
			}

			if (!MoveStep.PreferredMode.IsNone() && OutCumulativeMove.MixMode != EJoltMoveMixMode::OverrideAll)
			{
				OutCumulativeMove.PreferredMode = MoveStep.PreferredMode;
			}
				
			OutCumulativeMove.MixMode = EJoltMoveMixMode::OverrideVelocity;
			OutCumulativeMove.LinearVelocity  = MoveStep.LinearVelocity;
			OutCumulativeMove.AngularVelocityDegrees = MoveStep.AngularVelocityDegrees;
		}
	}
	else if (MoveStep.MixMode == EJoltMoveMixMode::AdditiveVelocity)
	{
		if (OutCumulativeMove.MixMode != EJoltMoveMixMode::OverrideVelocity && OutCumulativeMove.MixMode != EJoltMoveMixMode::OverrideAll)
		{
			if (!MoveStep.PreferredMode.IsNone())
			{
				OutCumulativeMove.PreferredMode = MoveStep.PreferredMode;
			}

			OutCumulativeMove.LinearVelocity += MoveStep.LinearVelocity;
			OutCumulativeMove.AngularVelocityDegrees += MoveStep.AngularVelocityDegrees;
		}
	}
	else if (MoveStep.MixMode == EJoltMoveMixMode::OverrideAll)
	{
		if (CheckPriority(ActiveMove.GetPriority(), ActiveMove.GetStartingTimeMs(), CurrentHighestPriority, CurrentLayeredMoveStartTime))
		{
			if (OutCumulativeMove.MixMode == EJoltMoveMixMode::OverrideVelocity || OutCumulativeMove.MixMode == EJoltMoveMixMode::OverrideAll)
			{
				UE_LOG(LogJoltMover, Log, TEXT("Multiple LayeredMoves with Override mix mode are active simultaneously. Layered move with the highest priority will take effect."));
			}
				
			OutCumulativeMove = MoveStep;
			OutCumulativeMove.MixMode = EJoltMoveMixMode::OverrideAll;
		}
	}
	else if (MoveStep.MixMode == EJoltMoveMixMode::OverrideAllExceptVerticalVelocity)
	{
		if (CheckPriority(ActiveMove.GetPriority(), ActiveMove.GetStartingTimeMs(), CurrentHighestPriority, CurrentLayeredMoveStartTime))
		{
			if (OutCumulativeMove.MixMode == EJoltMoveMixMode::OverrideVelocity || OutCumulativeMove.MixMode == EJoltMoveMixMode::OverrideAll || OutCumulativeMove.MixMode == EJoltMoveMixMode::OverrideAllExceptVerticalVelocity)
			{
				UE_LOG(LogJoltMover, Log, TEXT("Multiple LayeredMoves with Override mix mode are active simultaneously. Layered move with the highest priority will take effect."));
			}

			OutCumulativeMove = MoveStep;
			OutCumulativeMove.MixMode = EJoltMoveMixMode::OverrideAllExceptVerticalVelocity;
		}
	}
	else
	{
		ensureMsgf(false, TEXT("Unhandled move mix mode was found."));
	}
}

void UJoltMovementMixer::MixProposedMoves(const FJoltProposedMove& MoveToMix, FVector UpDirection, FJoltProposedMove& OutCumulativeMove)
{
	if (MoveToMix.bHasDirIntent && OutCumulativeMove.MixMode != EJoltMoveMixMode::OverrideAll)
	{
		OutCumulativeMove.bHasDirIntent = MoveToMix.bHasDirIntent;
		OutCumulativeMove.DirectionIntent = MoveToMix.DirectionIntent;
	}

	// Combine movement parameters from layered moves into what the mode wants to do
	if (MoveToMix.MixMode == EJoltMoveMixMode::OverrideAll)
	{
		OutCumulativeMove = MoveToMix;
	}
	else if (MoveToMix.MixMode == EJoltMoveMixMode::AdditiveVelocity)
	{
		OutCumulativeMove.LinearVelocity += MoveToMix.LinearVelocity;
		OutCumulativeMove.AngularVelocityDegrees += MoveToMix.AngularVelocityDegrees;
	}
	else if (MoveToMix.MixMode == EJoltMoveMixMode::OverrideVelocity)
	{
		OutCumulativeMove.LinearVelocity = MoveToMix.LinearVelocity;
		OutCumulativeMove.AngularVelocityDegrees = MoveToMix.AngularVelocityDegrees;
	}
	else if (MoveToMix.MixMode == EJoltMoveMixMode::OverrideAllExceptVerticalVelocity)
	{
		const FVector IncomingVerticalVelocity = MoveToMix.LinearVelocity.ProjectOnToNormal(UpDirection);
		const FVector IncomingNonVerticalVelocity = MoveToMix.LinearVelocity - IncomingVerticalVelocity;
		const FVector ExistingVerticalVelocity = OutCumulativeMove.LinearVelocity.ProjectOnToNormal(UpDirection);

		OutCumulativeMove = MoveToMix;
		OutCumulativeMove.LinearVelocity = IncomingNonVerticalVelocity + ExistingVerticalVelocity;
	}
	else
	{
		ensureMsgf(false, TEXT("Unhandled move mix mode was found."));
	}
}

void UJoltMovementMixer::ResetMixerState()
{
	CurrentHighestPriority = 0;
	CurrentLayeredMoveStartTime = TNumericLimits<double>::Max();
}

bool UJoltMovementMixer::CheckPriority(const FJoltLayeredMoveBase* LayeredMove, uint8& InOutHighestPriority, double& InOutCurrentLayeredMoveStartTimeMs)
{
	if (LayeredMove->Priority > InOutHighestPriority)
	{
		InOutHighestPriority = LayeredMove->Priority;
		InOutCurrentLayeredMoveStartTimeMs = LayeredMove->StartSimTimeMs;
		return true;
	}
	if (LayeredMove->Priority == InOutHighestPriority && LayeredMove->StartSimTimeMs < InOutCurrentLayeredMoveStartTimeMs)
	{
		InOutCurrentLayeredMoveStartTimeMs = LayeredMove->StartSimTimeMs;
		return true;
	}

	return false;
}

bool UJoltMovementMixer::CheckPriority(const uint8 LayeredMovePriority, const double LayeredMoveStartTimeMs, uint8& InOutHighestPriority, double& InOutCurrentLayeredMoveStartTimeMs)
{
	if (LayeredMovePriority > InOutHighestPriority)
	{
		InOutHighestPriority = LayeredMovePriority;
		InOutCurrentLayeredMoveStartTimeMs = LayeredMoveStartTimeMs;
		return true;
	}
	if (LayeredMovePriority == InOutHighestPriority && LayeredMoveStartTimeMs < InOutCurrentLayeredMoveStartTimeMs)
	{
		InOutCurrentLayeredMoveStartTimeMs = LayeredMoveStartTimeMs;
		return true;
	}

	return false;
}
