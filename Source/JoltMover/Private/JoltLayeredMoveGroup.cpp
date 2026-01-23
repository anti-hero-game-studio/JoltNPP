// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltLayeredMoveGroup.h"
#include "JoltLayeredMoveBase.h"
#include "JoltMoverLog.h"
#include "MoveLibrary/JoltMovementMixer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltLayeredMoveGroup)

FJoltLayeredMoveInstanceGroup::FJoltLayeredMoveInstanceGroup()
	: ResidualClamping(-1.f)
	, bApplyResidualVelocity(false)
	, ResidualVelocity(FVector::Zero())
{
}

FJoltLayeredMoveInstanceGroup& FJoltLayeredMoveInstanceGroup::operator=(const FJoltLayeredMoveInstanceGroup& Other)
{
	if (this != &Other)
	{
		const auto DeepCopyMovesFunc = [](const TArray<TSharedPtr<FJoltLayeredMoveInstance>>& From, TArray<TSharedPtr<FJoltLayeredMoveInstance>>& To)
		{
			To.Empty(From.Num());
			for (const TSharedPtr<FJoltLayeredMoveInstance>& Move : From)
			{
				To.Add(Move);
			}
		};
		DeepCopyMovesFunc(Other.ActiveMoves, ActiveMoves);
		DeepCopyMovesFunc(Other.QueuedMoves, QueuedMoves);

		TagCancellationRequests = Other.TagCancellationRequests;
	}

	return *this;
}

bool FJoltLayeredMoveInstanceGroup::operator==(const FJoltLayeredMoveInstanceGroup& Other) const
{
	// Todo: Deep move-by-move comparison
	if (ActiveMoves.Num() != Other.ActiveMoves.Num())
	{
		return false;
	}
	if (QueuedMoves.Num() != Other.QueuedMoves.Num())
	{
		return false;
	}
	
	return true;
}

void FJoltLayeredMoveInstanceGroup::QueueLayeredMove(const TSharedPtr<FJoltLayeredMoveInstance>& Move)
{
	if (ensure(Move.IsValid() && Move->HasLogic()))
	{
		QueuedMoves.Add(Move);
		// TODO NS: Add simple string support and re add this log
		//UE_LOG(LogJoltMover, VeryVerbose, TEXT("JoltLayeredMove queued move (%s)"), *Move->ToSimpleString());
	}
}
bool FJoltLayeredMoveInstanceGroup::HasSameContents(const FJoltLayeredMoveInstanceGroup& Other) const
{
	// Only compare the types of moves contained, not the state
	if (ActiveMoves.Num() != Other.ActiveMoves.Num() ||
		QueuedMoves.Num() != Other.QueuedMoves.Num())
	{
		return false;
	}
	for (int32 i = 0; i < ActiveMoves.Num(); ++i)
	{
		if (ActiveMoves[i]->GetDataStructType() != Other.ActiveMoves[i]->GetDataStructType())
		{
			return false;
		}
	}
	for (int32 i = 0; i < QueuedMoves.Num(); ++i)
	{
		if (QueuedMoves[i]->GetDataStructType() != Other.QueuedMoves[i]->GetDataStructType())
		{
			return false;
		}
	}
	return true;
}
void FJoltLayeredMoveInstanceGroup::ApplyResidualVelocity(FJoltProposedMove& ProposedMove)
{
	if (bApplyResidualVelocity)
	{
		ProposedMove.LinearVelocity = ResidualVelocity;
	}
	if (ResidualClamping >= 0.0f)
	{
		ProposedMove.LinearVelocity = ProposedMove.LinearVelocity.GetClampedToMaxSize(ResidualClamping);
	}
	ResetResidualVelocity();
}
bool FJoltLayeredMoveInstanceGroup::GenerateMixedMove(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, UJoltMovementMixer& MovementMixer, UJoltMoverBlackboard* SimBlackboard, FJoltProposedMove& OutMixedMove)
{
	// Tick and accumulate all active moves
	// Gather all proposed moves and distill this into a cumulative movement report. May include separate additive vs override moves.
	// TODO: may want to sort by priority or other factors
	bool bHasLayeredMoveContributions = false;
	for (const TSharedPtr<FJoltLayeredMoveInstance>& Move : ActiveMoves)
	{
		FJoltProposedMove MoveStep;
		if (Move->GenerateMove(StartState, TimeStep, SimBlackboard, MoveStep))
		{	
			bHasLayeredMoveContributions = true;
			
			MovementMixer.MixLayeredMove(*Move, MoveStep, OutMixedMove);
		}
	}
	
	return bHasLayeredMoveContributions;
}

void FJoltLayeredMoveInstanceGroup::NetSerialize(FArchive& Ar, uint8 MaxNumMovesToSerialize/* = MAX_uint8*/)
{
	const auto NetSerializeMovesArrayFunc = [&Ar, this](TArray<TSharedPtr<FJoltLayeredMoveInstance>>& MovesArray, uint8 MaxArraySize)
	{
		uint8 NumMovesToSerialize;
		if (Ar.IsSaving())
		{
			UE_CLOG(MovesArray.Num() > MaxArraySize, LogJoltMover, Warning, TEXT("Too many Layered Moves (%d!) to net serialize. Clamping to %d"), MovesArray.Num(), MaxArraySize);
			NumMovesToSerialize = FMath::Min<int32>(MovesArray.Num(), MaxArraySize);
		}

		Ar << NumMovesToSerialize;

		if (Ar.IsLoading())
		{
			// Note that any instances of FJoltLayeredMoveInstance added this way won't have their constructor called
			// They are not safe to use until after the NetSerialize() that immediately follows below (so don't add anything in between!) 
			MovesArray.SetNumZeroed(NumMovesToSerialize);
			
			for (int32 MoveIdx = 0; MoveIdx < NumMovesToSerialize && !Ar.IsError(); ++MoveIdx)
			{
				MovesArray[MoveIdx] = MakeShared<FJoltLayeredMoveInstance>(MakeShared<FJoltLayeredMoveInstancedData>(), nullptr);
				FJoltLayeredMoveInstance* Move = MovesArray[MoveIdx].Get();
				Move->NetSerialize(Ar);
			}
		}
		else
		{
			for (int32 MoveIdx = 0; MoveIdx < NumMovesToSerialize && !Ar.IsError(); ++MoveIdx)
			{
				FJoltLayeredMoveInstance* Move = MovesArray[MoveIdx].Get();
				Move->NetSerialize(Ar);
			}
		}
	};

	NetSerializeMovesArrayFunc(ActiveMoves, MaxNumMovesToSerialize);
	
	const uint8 MaxNumQueuedMovesToSerialize = FMath::Max(MaxNumMovesToSerialize - ActiveMoves.Num(), 0);
	NetSerializeMovesArrayFunc(QueuedMoves, MaxNumQueuedMovesToSerialize);
}


void FJoltLayeredMoveInstanceGroup::AddStructReferencedObjects(FReferenceCollector& Collector) const
{
	for (const TSharedPtr<FJoltLayeredMoveInstance>& Move : ActiveMoves)
	{
		Move->AddReferencedObjects(Collector);
	}
	for (const TSharedPtr<FJoltLayeredMoveInstance>& Move : QueuedMoves)
	{
		Move->AddReferencedObjects(Collector);
	}
}

void FJoltLayeredMoveInstanceGroup::ResetResidualVelocity()
{
	bApplyResidualVelocity = false;
	ResidualVelocity = FVector::ZeroVector;
	ResidualClamping = -1.f;
}

void FJoltLayeredMoveInstanceGroup::Reset()
{
	ResetResidualVelocity();
	QueuedMoves.Empty();
	ActiveMoves.Empty();
	TagCancellationRequests.Empty();
}

void FJoltLayeredMoveInstanceGroup::PopulateMissingActiveMoveLogic(const TArray<TObjectPtr<UJoltLayeredMoveLogic>>& RegisteredMoves)
{
	for (TSharedPtr<FJoltLayeredMoveInstance> ActiveMove : ActiveMoves)
	{
		if (ActiveMove && !ActiveMove->HasLogic())
		{
			bool bPopulatedActiveMoveLogic = ActiveMove->PopulateMissingActiveMoveLogic(RegisteredMoves);
		}
	}

	for (TSharedPtr<FJoltLayeredMoveInstance> QueuedMove : QueuedMoves)
	{
		if (QueuedMove && !QueuedMove->HasLogic())
		{
			bool bPopulatedActiveMoveLogic = QueuedMove->PopulateMissingActiveMoveLogic(RegisteredMoves);
		}
	}
}

FString FJoltLayeredMoveInstanceGroup::ToSimpleString() const
{
	return FString::Printf(TEXT("FJoltLayeredMoveInstanceGroup. Active: %i Queued: %i"), ActiveMoves.Num(), QueuedMoves.Num());
}

const FJoltLayeredMoveInstance* FJoltLayeredMoveInstanceGroup::PrivateFindActiveMove(const TSubclassOf<UJoltLayeredMoveLogic>& MoveLogicClass) const
{
	for (const TSharedPtr<FJoltLayeredMoveInstance>& Move : ActiveMoves)
	{
		if (Move->GetLogicClass() == MoveLogicClass)
		{
			return Move.Get();
		}
	}

	return nullptr;
}

const FJoltLayeredMoveInstance* FJoltLayeredMoveInstanceGroup::PrivateFindActiveMove(const UScriptStruct* MoveDataType) const
{
	for (const TSharedPtr<FJoltLayeredMoveInstance>& Move : ActiveMoves)
	{
		if (Move->GetDataStructType() == MoveDataType)
		{
			return Move.Get();
		}
	}

	return nullptr;
}

const FJoltLayeredMoveInstance* FJoltLayeredMoveInstanceGroup::PrivateFindQueuedMove(const TSubclassOf<UJoltLayeredMoveLogic>& MoveLogicClass) const
{
	for (const TSharedPtr<FJoltLayeredMoveInstance>& Move : QueuedMoves)
	{
		if (Move->GetLogicClass() == MoveLogicClass)
		{
			return Move.Get();
		}
	}

	return nullptr;
}

const FJoltLayeredMoveInstance* FJoltLayeredMoveInstanceGroup::PrivateFindQueuedMove(const UScriptStruct* MoveDataType) const
{
	for (const TSharedPtr<FJoltLayeredMoveInstance>& Move : QueuedMoves)
	{
		if (Move->GetDataStructType() == MoveDataType)
		{
			return Move.Get();
		}
	}

	return nullptr;
}

void FJoltLayeredMoveInstanceGroup::CancelMovesByTag(FGameplayTag Tag, bool bRequireExactMatch)
{

	// Schedule a tag cancellation request, to be handled during simulation
	TagCancellationRequests.Add(TPair<FGameplayTag, bool>(Tag, bRequireExactMatch));

}


void FJoltLayeredMoveInstanceGroup::FlushMoveArrays(const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard)
{
	bool bResidualVelocityOverridden = false;
	bool bClampVelocityOverridden = false;

	// Process any cancellations
	{
		for (TPair<FGameplayTag, bool> CancelRequest : TagCancellationRequests)
		{
			const FGameplayTag TagToMatch = CancelRequest.Key;
			const bool bRequireExactMatch = CancelRequest.Value;

			QueuedMoves.RemoveAll([TagToMatch, bRequireExactMatch](const TSharedPtr<FJoltLayeredMoveInstance>& Move)
				{
					return (!Move.IsValid() || Move->HasGameplayTag(TagToMatch, bRequireExactMatch));
				});

			// Process completion of any active moves that being canceled
			ActiveMoves.RemoveAll([&TimeStep, &SimBlackboard, &bResidualVelocityOverridden, &bClampVelocityOverridden, TagToMatch, bRequireExactMatch, this] (const TSharedPtr<FJoltLayeredMoveInstance>& Move)
				{
					if (Move.IsValid() && Move->HasGameplayTag(TagToMatch, bRequireExactMatch))
					{
						ProcessFinishedMove(*Move, bResidualVelocityOverridden, bClampVelocityOverridden);
						Move->EndMove(TimeStep, SimBlackboard);
						return true;
					}
					return false;
				});
		}

		TagCancellationRequests.Empty();
	}

	{
	
		// Process completion of any active moves that are finished
		ActiveMoves.RemoveAll([&TimeStep, &SimBlackboard, &bResidualVelocityOverridden, &bClampVelocityOverridden, this]
			(const TSharedPtr<FJoltLayeredMoveInstance>& Move)
			{
				if (Move.IsValid() && Move->IsFinished(TimeStep, SimBlackboard))
				{
					ProcessFinishedMove(*Move, bResidualVelocityOverridden, bClampVelocityOverridden);
					Move->EndMove(TimeStep, SimBlackboard);
					return true;
				}
				return false;
			});	
	}

	// Begin any queued moves
	for (TSharedPtr<FJoltLayeredMoveInstance>& QueuedMove : QueuedMoves)
	{
		if (QueuedMove.IsValid())
		{
			if (QueuedMove->HasLogic())
			{
				ActiveMoves.Add(QueuedMove);
				QueuedMove->StartMove(TimeStep, SimBlackboard);
			}
			else
			{
				// We should've populated missing logic before this so let's just clear this and warn about it
				UE_LOG(LogJoltMover, Warning, TEXT("Queued Active Move (%s) logic was not present. Move will not be activated."), *QueuedMove->GetDataStructType()->GetName());
			}
		}
	}
	
	QueuedMoves.Empty();
}

void FJoltLayeredMoveInstanceGroup::ProcessFinishedMove(const FJoltLayeredMoveInstance& Move, bool& bResidualVelocityOverridden, bool& bClampVelocityOverridden)
{
	const FJoltLayeredMoveFinishVelocitySettings& FinishVelocitySettings = Move.GetFinishVelocitySettings();
	const EJoltMoveMixMode MixMode = Move.GetMixMode();
	
	if (FinishVelocitySettings.FinishVelocityMode == EJoltLayeredMoveFinishVelocityMode::SetVelocity)
	{
		bApplyResidualVelocity = true;

		switch (MixMode)
		{
		case EJoltMoveMixMode::AdditiveVelocity:
			if (!bResidualVelocityOverridden)
			{
				ResidualVelocity += FinishVelocitySettings.SetVelocity;
			}
			break;
		case EJoltMoveMixMode::OverrideVelocity:
		case EJoltMoveMixMode::OverrideAll:
			{
				UE_CLOG(bClampVelocityOverridden, LogJoltMover, Log, TEXT("Multiple LayeredMove residual settings have a MixMode that overrides. Only one will take effect."));
				bResidualVelocityOverridden = true;
				ResidualVelocity = FinishVelocitySettings.SetVelocity;
			}
			break;
		}
	}
	else if (FinishVelocitySettings.FinishVelocityMode == EJoltLayeredMoveFinishVelocityMode::ClampVelocity)
	{
		switch (MixMode)
		{
		case EJoltMoveMixMode::AdditiveVelocity:
			if (!bClampVelocityOverridden)
			{
				if (ResidualClamping < 0)
				{
					ResidualClamping = FinishVelocitySettings.ClampVelocity;
				}
				else if (ResidualClamping > FinishVelocitySettings.ClampVelocity)
				{
					// No way to really add clamping so we instead apply it if it was smaller
					ResidualClamping = FinishVelocitySettings.ClampVelocity;
				}
			}
			break;
		case EJoltMoveMixMode::OverrideVelocity:
		case EJoltMoveMixMode::OverrideAll:
			{
				UE_CLOG(bClampVelocityOverridden, LogJoltMover, Log, TEXT("Multiple LayeredMove residual settings have a MixMode that overrides. Only one will take effect."));
				bClampVelocityOverridden = true;
				ResidualClamping = FinishVelocitySettings.ClampVelocity;
			}
			break;
		}
	}
}
