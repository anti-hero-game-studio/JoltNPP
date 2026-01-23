// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultMovementSet/LayeredMoves/JoltMultiJumpLayeredMove.h"
#include "JoltMoverComponent.h"
#include "JoltMoverSimulationTypes.h"
#include "DefaultMovementSet/Settings/JoltCommonLegacyMovementSettings.h"
#include "MoveLibrary/JoltFloorQueryUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMultiJumpLayeredMove)

FJoltLayeredMove_MultiJump::FJoltLayeredMove_MultiJump()
	: MaximumInAirJumps(1)
	, UpwardsSpeed(800)
	, JumpsInAirRemaining(-1)
	, TimeOfLastJumpMS(0)
{
	DurationMs = -1.f;
	MixMode = EJoltMoveMixMode::OverrideVelocity;
}

bool FJoltLayeredMove_MultiJump::WantsToJump(const FJoltMoverInputCmdContext& InputCmd)
{
	if (const FJoltCharacterDefaultInputs* CharacterInputs = InputCmd.Collection.FindDataByType<FJoltCharacterDefaultInputs>())
	{
		return CharacterInputs->bIsJumpJustPressed;
	}
	
	return false;
}

bool FJoltLayeredMove_MultiJump::GenerateMove(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, const UJoltMoverComponent* MoverComp, UJoltMoverBlackboard* SimBlackboard, FJoltProposedMove& OutProposedMove)
{
	const FJoltCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.Collection.FindDataByType<FJoltCharacterDefaultInputs>();
	const FJoltUpdatedMotionState* SyncState = StartState.SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>();
	check(SyncState);

	OutProposedMove.MixMode = MixMode;

	FJoltFloorCheckResult FloorHitResult;
	bool bValidBlackboard = SimBlackboard->TryGet(CommonBlackboard::LastFloorResult, OUT FloorHitResult);

	if (FMath::IsNearlyEqual(StartSimTimeMs, TimeStep.BaseSimTimeMs))
	{
		JumpsInAirRemaining = MaximumInAirJumps;
	}
	
	bool bPerformedJump = false;
	if (CharacterInputs && CharacterInputs->bIsJumpJustPressed)
	{
		if (StartSimTimeMs == TimeStep.BaseSimTimeMs)
		{
			// if this was the first jump and its a valid floor we do the initial jump from walking and back out so we don't get extra jump
			if (bValidBlackboard && FloorHitResult.IsWalkableFloor())
			{
				bPerformedJump = PerformJump(SyncState, TimeStep, MoverComp, OutProposedMove);
				return bPerformedJump;
			}
		}

		// perform in air jump
		if (TimeStep.BaseSimTimeMs > TimeOfLastJumpMS && JumpsInAirRemaining > 0)
		{
			JumpsInAirRemaining--;
			bPerformedJump = PerformJump(SyncState, TimeStep, MoverComp, OutProposedMove);
		}
		else
		{
			// setting mix mode to additive when we're not adding any jump input so air movement acts as expected
			OutProposedMove.MixMode = EJoltMoveMixMode::AdditiveVelocity;
		}
	}
	else
	{
		// setting mix mode to additive when we're not adding any jump input so air movement acts as expected
		OutProposedMove.MixMode = EJoltMoveMixMode::AdditiveVelocity;
	}

	// if we hit a valid floor and it's not the start of the move (since we could start this move on the ground) end this move
	if ((bValidBlackboard && FloorHitResult.IsWalkableFloor() && StartSimTimeMs < TimeStep.BaseSimTimeMs) || JumpsInAirRemaining <= 0)
	{
		DurationMs = 0;
	}

	return bPerformedJump;
}

FJoltLayeredMoveBase* FJoltLayeredMove_MultiJump::Clone() const
{
	FJoltLayeredMove_MultiJump* CopyPtr = new FJoltLayeredMove_MultiJump(*this);
	return CopyPtr;
}

void FJoltLayeredMove_MultiJump::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	Ar << MaximumInAirJumps;
	Ar << UpwardsSpeed;
	Ar << JumpsInAirRemaining;
	Ar << TimeOfLastJumpMS;
}

UScriptStruct* FJoltLayeredMove_MultiJump::GetScriptStruct() const
{
	return FJoltLayeredMove_MultiJump::StaticStruct();
}

FString FJoltLayeredMove_MultiJump::ToSimpleString() const
{
	return FString::Printf(TEXT("Multi-jump"));
}

void FJoltLayeredMove_MultiJump::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}

bool FJoltLayeredMove_MultiJump::PerformJump(const FJoltUpdatedMotionState* SyncState, const FJoltMoverTimeStep& TimeStep, const UJoltMoverComponent* MoverComp, FJoltProposedMove& OutProposedMove)
{
	TimeOfLastJumpMS = TimeStep.BaseSimTimeMs;
	if (const TObjectPtr<const UJoltCommonLegacyMovementSettings> CommonLegacySettings = MoverComp->FindSharedSettings<UJoltCommonLegacyMovementSettings>())
	{
		OutProposedMove.PreferredMode = CommonLegacySettings->AirMovementModeName;
	}

	const FVector UpDir = MoverComp->GetUpDirection();

	const FVector ImpulseVelocity = UpDir * UpwardsSpeed;

	switch (MixMode)
	{
	case EJoltMoveMixMode::AdditiveVelocity:
		{
			OutProposedMove.LinearVelocity = ImpulseVelocity;
			break;
		}
		
	case EJoltMoveMixMode::OverrideAll:
	case EJoltMoveMixMode::OverrideVelocity:
		{
			// Jump impulse overrides vertical velocity while maintaining the rest
			const FVector PriorVelocityWS = SyncState->GetVelocity_WorldSpace();
			const FVector StartingNonUpwardsVelocity = PriorVelocityWS - PriorVelocityWS.ProjectOnToNormal(UpDir);

			OutProposedMove.LinearVelocity = StartingNonUpwardsVelocity + ImpulseVelocity;
			break;
		}
		
	default:
		ensureMsgf(false, TEXT("Multi-Jump layered move has an invalid MixMode and will do nothing."));
		return false;
	}

	return true;
}
