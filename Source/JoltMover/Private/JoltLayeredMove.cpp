// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltLayeredMove.h"
#include "JoltMoverLog.h"
#include "JoltMoverComponent.h"
#include "JoltMoverSimulationTypes.h"
#include "JoltMoverModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltLayeredMove)

const double LayeredMove_InvalidTime = -UE_BIG_NUMBER;

void FJoltLayeredMoveFinishVelocitySettings::NetSerialize(FArchive& Ar)
{
	uint8 bHasFinishVelocitySettings = Ar.IsSaving() ? 0 : (FinishVelocityMode == EJoltLayeredMoveFinishVelocityMode::MaintainLastRootMotionVelocity);
	Ar.SerializeBits(&bHasFinishVelocitySettings, 1);

	if (bHasFinishVelocitySettings)
	{
		uint8 FinishVelocityModeAsU8 = (uint8)(FinishVelocityMode);
		Ar << FinishVelocityModeAsU8;
		FinishVelocityMode = (EJoltLayeredMoveFinishVelocityMode)FinishVelocityModeAsU8;

		if (FinishVelocityMode == EJoltLayeredMoveFinishVelocityMode::SetVelocity)
		{
			Ar << SetVelocity;
		}
		else if (FinishVelocityMode == EJoltLayeredMoveFinishVelocityMode::ClampVelocity)
		{
			Ar << ClampVelocity;
		}
	}
}

FJoltLayeredMoveBase::FJoltLayeredMoveBase() :
	MixMode(EJoltMoveMixMode::AdditiveVelocity),
	Priority(0),
	DurationMs(-1.f),
	StartSimTimeMs(LayeredMove_InvalidTime)
{
}


void FJoltLayeredMoveBase::StartMove(const UJoltMoverComponent* MoverComp, UJoltMoverBlackboard* SimBlackboard, double CurrentSimTimeMs)
{
	StartSimTimeMs = CurrentSimTimeMs;
	OnStart(MoverComp, SimBlackboard);
}

void FJoltLayeredMoveBase::StartMove_Async(UJoltMoverBlackboard* SimBlackboard, double CurrentSimTimeMs)
{
	StartSimTimeMs = CurrentSimTimeMs;
	OnStart_Async(SimBlackboard);
}

bool FJoltLayeredMoveBase::GenerateMove_Async(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard, FJoltProposedMove& OutProposedMove)
{
	ensureMsgf(false, TEXT("GenerateMove_Async is not implemented"));
	return false;
}

bool FJoltLayeredMoveBase::IsFinished(double CurrentSimTimeMs) const
{
	const bool bHasStarted = (StartSimTimeMs >= 0.0);
	const bool bTimeExpired = bHasStarted && (DurationMs > 0.f) && (StartSimTimeMs + DurationMs <= CurrentSimTimeMs);
	const bool bDidTickOnceAndExpire = bHasStarted && (DurationMs == 0.f);

	return bTimeExpired || bDidTickOnceAndExpire;
}

void FJoltLayeredMoveBase::EndMove(const UJoltMoverComponent* MoverComp, UJoltMoverBlackboard* SimBlackboard, double CurrentSimTimeMs)
{
	OnEnd(MoverComp, SimBlackboard, CurrentSimTimeMs);
}

void FJoltLayeredMoveBase::EndMove_Async(UJoltMoverBlackboard* SimBlackboard, double CurrentSimTimeMs)
{
	OnEnd_Async(SimBlackboard, CurrentSimTimeMs);
}

FJoltLayeredMoveBase* FJoltLayeredMoveBase::Clone() const
{
	// If child classes don't override this, saved moves will not work
	checkf(false, TEXT("FJoltLayeredMoveBase::Clone() being called erroneously from %s. A FJoltLayeredMoveBase should never be queued directly and Clone should always be overridden in child structs!"), *GetNameSafe(GetScriptStruct()));
	return nullptr;
}


void FJoltLayeredMoveBase::NetSerialize(FArchive& Ar)
{
	uint8 MixModeAsU8 = (uint8)MixMode;
	Ar << MixModeAsU8;
	MixMode = (EJoltMoveMixMode)MixModeAsU8;

	uint8 bHasDefaultPriority = Priority == 0;
	Ar.SerializeBits(&bHasDefaultPriority, 1);
	if (!bHasDefaultPriority)
	{
		Ar << Priority;
	}
	
	Ar << DurationMs;
	Ar << StartSimTimeMs;

	FinishVelocitySettings.NetSerialize(Ar);
}


UScriptStruct* FJoltLayeredMoveBase::GetScriptStruct() const
{
	return FJoltLayeredMoveBase::StaticStruct();
}


FString FJoltLayeredMoveBase::ToSimpleString() const
{
	return GetScriptStruct()->GetName();
}


FJoltLayeredMoveGroup::FJoltLayeredMoveGroup()
	: ResidualVelocity(FVector::Zero())
	, ResidualClamping(-1.f)
	, bApplyResidualVelocity(false)
{
}


void FJoltLayeredMoveGroup::QueueLayeredMove(TSharedPtr<FJoltLayeredMoveBase> Move)
{
	if (ensure(Move.IsValid()))
	{
		QueuedLayeredMoves.Add(Move);
		UE_LOG(LogJoltMover, VeryVerbose, TEXT("JoltLayeredMove queued move (%s)"), *Move->ToSimpleString());
	}
}

void FJoltLayeredMoveGroup::CancelMovesByTag(FGameplayTag Tag, bool bRequireExactMatch)
{

	// Schedule a tag cancellation request, to be handled during simulation
	TagCancellationRequests.Add(TPair<FGameplayTag, bool>(Tag, bRequireExactMatch));

}

TArray<TSharedPtr<FJoltLayeredMoveBase>> FJoltLayeredMoveGroup::GenerateActiveMoves(const FJoltMoverTimeStep& TimeStep, const UJoltMoverComponent* MoverComp, UJoltMoverBlackboard* SimBlackboard)
{
	const double SimStartTimeMs		= TimeStep.BaseSimTimeMs;
	const double SimTimeAfterTickMs	= SimStartTimeMs + TimeStep.StepMs;

	FlushMoveArrays(MoverComp, SimBlackboard, SimStartTimeMs, /*bIsAsync =*/ false);

	return ActiveLayeredMoves;
}

TArray<TSharedPtr<FJoltLayeredMoveBase>> FJoltLayeredMoveGroup::GenerateActiveMoves_Async(const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard)
{
	const double SimStartTimeMs		= TimeStep.BaseSimTimeMs;
	const double SimTimeAfterTickMs	= SimStartTimeMs + TimeStep.StepMs;

	FlushMoveArrays(nullptr, SimBlackboard, SimStartTimeMs, /*bIsAsync =*/ true);

	return ActiveLayeredMoves;
}

void FJoltLayeredMoveGroup::NetSerialize(FArchive& Ar, uint8 MaxNumMovesToSerialize/* = MAX_uint8*/)
{
	// TODO: Warn if some sources will be dropped
	const uint8 NumActiveMovesToSerialize = FMath::Min<int32>(ActiveLayeredMoves.Num(), MaxNumMovesToSerialize);
	const uint8 NumQueuedMovesToSerialize = NumActiveMovesToSerialize < MaxNumMovesToSerialize ? MaxNumMovesToSerialize - NumActiveMovesToSerialize : 0;
	NetSerializeLayeredMovesArray(Ar, ActiveLayeredMoves, NumActiveMovesToSerialize);
	NetSerializeLayeredMovesArray(Ar, QueuedLayeredMoves, NumQueuedMovesToSerialize);
}


static void CopyLayeredMoveArray(TArray<TSharedPtr<FJoltLayeredMoveBase>>& Dest, const TArray<TSharedPtr<FJoltLayeredMoveBase>>& Src)
{
	bool bCanCopyInPlace = (UE::JoltMover::DisableDataCopyInPlace == 0 && Dest.Num() == Src.Num());
	if (bCanCopyInPlace)
	{
		// If copy in place is enabled and the arrays are the same size, copy by index
		for (int32 i = 0; i < Dest.Num(); ++i)
		{
			if (FJoltLayeredMoveBase* SrcData = Src[i].Get())
			{
				FJoltLayeredMoveBase* DestData = Dest[i].Get();
				UScriptStruct* SourceStruct = SrcData->GetScriptStruct();

				if (DestData && SourceStruct == DestData->GetScriptStruct())
				{
					// Same type so copy in place
					SourceStruct->CopyScriptStruct(DestData, SrcData, 1);
				}
				else
				{
					// Different type so replace the shared ptr with a clone
					Dest[i] = TSharedPtr<FJoltLayeredMoveBase>(SrcData->Clone());
				}
			}
			else
			{
				// Found invalid source, fall back to full copy
				bCanCopyInPlace = false;
				break;
			}
		}
	}
	
	if (!bCanCopyInPlace)
	{
		// Deep copy active moves
		Dest.Empty(Src.Num());
		for (int i = 0; i < Src.Num(); ++i)
		{
			if (Src[i].IsValid())
			{
				FJoltLayeredMoveBase* CopyOfSourcePtr = Src[i]->Clone();
				Dest.Add(TSharedPtr<FJoltLayeredMoveBase>(CopyOfSourcePtr));
			}
			else
			{
				UE_LOG(LogJoltMover, Warning, TEXT("CopyLayeredMoveArray trying to copy invalid Other Layered Move"));
			}
		}
	}
}


FJoltLayeredMoveGroup& FJoltLayeredMoveGroup::operator=(const FJoltLayeredMoveGroup& Other)
{
	// Perform deep copy of this Group
	if (this != &Other)
	{
		CopyLayeredMoveArray(ActiveLayeredMoves, Other.ActiveLayeredMoves);
		CopyLayeredMoveArray(QueuedLayeredMoves, Other.QueuedLayeredMoves);

		TagCancellationRequests = Other.TagCancellationRequests;
	}

	return *this;
}

bool FJoltLayeredMoveGroup::operator==(const FJoltLayeredMoveGroup& Other) const
{
	// Deep move-by-move comparison
	if (ActiveLayeredMoves.Num() != Other.ActiveLayeredMoves.Num())
	{
		return false;
	}
	if (QueuedLayeredMoves.Num() != Other.QueuedLayeredMoves.Num())
	{
		return false;
	}


	for (int32 i = 0; i < ActiveLayeredMoves.Num(); ++i)
	{
		if (ActiveLayeredMoves[i].IsValid() == Other.ActiveLayeredMoves[i].IsValid())
		{
			if (ActiveLayeredMoves[i].IsValid())
			{
				// TODO: Implement deep equality checks
				// 				if (!ActiveLayeredMoves[i]->MatchesAndHasSameState(Other.ActiveLayeredMoves[i].Get()))
				// 				{
				// 					return false; // They're valid and don't match/have same state
				// 				}
			}
		}
		else
		{
			return false; // Mismatch in validity
		}
	}
	for (int32 i = 0; i < QueuedLayeredMoves.Num(); ++i)
	{
		if (QueuedLayeredMoves[i].IsValid() == Other.QueuedLayeredMoves[i].IsValid())
		{
			if (QueuedLayeredMoves[i].IsValid())
			{
				// TODO: Implement deep equality checks
				// 				if (!QueuedLayeredMoves[i]->MatchesAndHasSameState(Other.QueuedLayeredMoves[i].Get()))
				// 				{
				// 					return false; // They're valid and don't match/have same state
				// 				}
			}
		}
		else
		{
			return false; // Mismatch in validity
		}
	}
	return true;
}

bool FJoltLayeredMoveGroup::operator!=(const FJoltLayeredMoveGroup& Other) const
{
	return !(FJoltLayeredMoveGroup::operator==(Other));
}

bool FJoltLayeredMoveGroup::HasSameContents(const FJoltLayeredMoveGroup& Other) const
{
	// Only compare the types of moves contained, not the state
	if (ActiveLayeredMoves.Num() != Other.ActiveLayeredMoves.Num() ||
		QueuedLayeredMoves.Num() != Other.QueuedLayeredMoves.Num())
	{
		return false;
	}

	for (int32 i = 0; i < ActiveLayeredMoves.Num(); ++i)
	{
		if (ActiveLayeredMoves[i]->GetScriptStruct() != Other.ActiveLayeredMoves[i]->GetScriptStruct())
		{
			return false;
		}
	}

	for (int32 i = 0; i < QueuedLayeredMoves.Num(); ++i)
	{
		if (QueuedLayeredMoves[i]->GetScriptStruct() != Other.QueuedLayeredMoves[i]->GetScriptStruct())
		{
			return false;
		}
	}

	return true;
}


void FJoltLayeredMoveGroup::AddStructReferencedObjects(FReferenceCollector& Collector) const
{
	for (const TSharedPtr<FJoltLayeredMoveBase>& LayeredMove : ActiveLayeredMoves)
	{
		if (LayeredMove.IsValid())
		{
			LayeredMove->AddReferencedObjects(Collector);
		}
	}

	for (const TSharedPtr<FJoltLayeredMoveBase>& LayeredMove : QueuedLayeredMoves)
	{
		if (LayeredMove.IsValid())
		{
			LayeredMove->AddReferencedObjects(Collector);
		}
	}
}

FString FJoltLayeredMoveGroup::ToSimpleString() const
{
	return FString::Printf(TEXT("FJoltLayeredMoveGroup. Active: %i Queued: %i"), ActiveLayeredMoves.Num(), QueuedLayeredMoves.Num());
}

const FJoltLayeredMoveBase* FJoltLayeredMoveGroup::FindActiveMove(const UScriptStruct* LayeredMoveStructType) const
{
	for (const TSharedPtr<FJoltLayeredMoveBase>& ActiveMove : ActiveLayeredMoves)
	{
		if (ActiveMove && ActiveMove->GetScriptStruct()->IsChildOf(LayeredMoveStructType))
		{
			return ActiveMove.Get();
		}
	}
	return nullptr;
}

const FJoltLayeredMoveBase* FJoltLayeredMoveGroup::FindQueuedMove(const UScriptStruct* LayeredMoveStructType) const
{
	for (const TSharedPtr<FJoltLayeredMoveBase>& QueuedMove : QueuedLayeredMoves)
	{
		if (QueuedMove && QueuedMove->GetScriptStruct()->IsChildOf(LayeredMoveStructType))
		{
			return QueuedMove.Get();
		}
	}
	return nullptr;
}

void FJoltLayeredMoveGroup::FlushMoveArrays(const UJoltMoverComponent* MoverComp, UJoltMoverBlackboard* SimBlackboard, double CurrentSimTimeMs, bool bIsAsync)
{
	if (bIsAsync)
	{
		ensureMsgf(MoverComp == nullptr, TEXT("In async mode, no mover Component should be passed in as argument in FJoltLayeredMoveGroup::FlushMoveArrays"));
		MoverComp = nullptr;
	}

	bool bResidualVelocityOverridden = false;
	bool bClampVelocityOverridden = false;
	
	// Process any cancellations
	{
		for (TPair<FGameplayTag, bool> CancelRequest : TagCancellationRequests)
		{
			const FGameplayTag TagToMatch = CancelRequest.Key;
			const bool bRequireExactMatch = CancelRequest.Value;

			QueuedLayeredMoves.RemoveAll([TagToMatch, bRequireExactMatch](const TSharedPtr<FJoltLayeredMoveBase>& Move)
				{
					return (Move.IsValid() && Move->HasGameplayTag(TagToMatch, bRequireExactMatch));
				});

			ActiveLayeredMoves.RemoveAll([MoverComp, SimBlackboard, CurrentSimTimeMs, &bResidualVelocityOverridden, &bClampVelocityOverridden, bIsAsync, TagToMatch, bRequireExactMatch, this] (const TSharedPtr<FJoltLayeredMoveBase>& Move)
				{
					if (Move.IsValid() && Move->HasGameplayTag(TagToMatch, bRequireExactMatch))
					{
						GatherResidualVelocitySettings(Move, bResidualVelocityOverridden, bClampVelocityOverridden);
						if (bIsAsync)
						{
							Move->EndMove_Async(SimBlackboard, CurrentSimTimeMs);
						}
						else
						{
							Move->EndMove(MoverComp, SimBlackboard, CurrentSimTimeMs);
						}
						return true;
					}

					return false;
				});
		}

		TagCancellationRequests.Empty();
	}

	
	// Remove any finished moves
	ActiveLayeredMoves.RemoveAll([MoverComp, SimBlackboard, CurrentSimTimeMs, &bResidualVelocityOverridden, &bClampVelocityOverridden, bIsAsync, this]
		(const TSharedPtr<FJoltLayeredMoveBase>& Move)
		{
			if (Move.IsValid())
			{
				if (Move->IsFinished(CurrentSimTimeMs))
				{
					GatherResidualVelocitySettings(Move, bResidualVelocityOverridden, bClampVelocityOverridden);
					if (bIsAsync)
					{
						Move->EndMove_Async(SimBlackboard, CurrentSimTimeMs);
					}
					else
					{
						Move->EndMove(MoverComp, SimBlackboard, CurrentSimTimeMs);
					}
					return true;
				}
			}
			else
			{
				return true;	
			}

			return false;
		});

	// Make any queued moves active
	for (TSharedPtr<FJoltLayeredMoveBase>& QueuedMove : QueuedLayeredMoves)
	{
		ActiveLayeredMoves.Add(QueuedMove);
		if (bIsAsync)
		{
			QueuedMove->StartMove_Async(SimBlackboard, CurrentSimTimeMs);
		}
		else
		{
			QueuedMove->StartMove(MoverComp, SimBlackboard, CurrentSimTimeMs);
		}
	}

	QueuedLayeredMoves.Empty();
}

void FJoltLayeredMoveGroup::GatherResidualVelocitySettings(const TSharedPtr<FJoltLayeredMoveBase>& Move, bool& bResidualVelocityOverridden, bool& bClampVelocityOverridden)
{
	if (Move->FinishVelocitySettings.FinishVelocityMode == EJoltLayeredMoveFinishVelocityMode::SetVelocity)
	{
		if (Move->MixMode == EJoltMoveMixMode::OverrideVelocity)
		{
			if (bResidualVelocityOverridden)
			{
				UE_LOG(LogJoltMover, Log, TEXT("Multiple LayeredMove residual settings have a MixMode that overrides. Only one will take effect."));
			}

			bResidualVelocityOverridden = true;
			ResidualVelocity = Move->FinishVelocitySettings.SetVelocity;
		}
		else if (Move->MixMode == EJoltMoveMixMode::AdditiveVelocity && !bResidualVelocityOverridden)
		{
			ResidualVelocity += Move->FinishVelocitySettings.SetVelocity;
		}
		else if (Move->MixMode == EJoltMoveMixMode::OverrideAll)
		{
			if (bResidualVelocityOverridden)
			{
				UE_LOG(LogJoltMover, Log, TEXT("Multiple LayeredMove residual settings have a MixMode that overrides. Only one will take effect."));
			}

			bResidualVelocityOverridden = true;
			ResidualVelocity = Move->FinishVelocitySettings.SetVelocity;
		}
		else
		{
			check(0);	// unhandled case
		}
		bApplyResidualVelocity = true;
	}
	else if (Move->FinishVelocitySettings.FinishVelocityMode == EJoltLayeredMoveFinishVelocityMode::ClampVelocity)
	{
		if (Move->MixMode == EJoltMoveMixMode::OverrideVelocity)
		{
			if (bClampVelocityOverridden)
			{
				UE_LOG(LogJoltMover, Log, TEXT("Multiple LayeredMove residual settings have a MixMode that overrides. Only one will take effect."));
			}

			bClampVelocityOverridden = true;
			ResidualClamping = Move->FinishVelocitySettings.ClampVelocity;
		}
		else if (Move->MixMode == EJoltMoveMixMode::AdditiveVelocity && !bClampVelocityOverridden)
		{
			if (ResidualClamping < 0)
			{
				ResidualClamping = Move->FinishVelocitySettings.ClampVelocity;
			}
			// No way to really add clamping so we instead apply it if it was smaller
			else if (ResidualClamping > Move->FinishVelocitySettings.ClampVelocity)
			{
				ResidualClamping = Move->FinishVelocitySettings.ClampVelocity;
			}
		}
		else if (Move->MixMode == EJoltMoveMixMode::OverrideAll)
		{
			if (bClampVelocityOverridden)
			{
				UE_LOG(LogJoltMover, Log, TEXT("Multiple LayeredMove residual settings have a MixMode that overrides. Only one will take effect."));
			}

			bClampVelocityOverridden = true;
			ResidualClamping = Move->FinishVelocitySettings.ClampVelocity;
		}
		else
		{
			check(0);	// unhandled case
		}
	}
}

struct FJoltLayeredMoveDeleter
{
	FORCEINLINE void operator()(FJoltLayeredMoveBase* Object) const
	{
		check(Object);
		UScriptStruct* ScriptStruct = Object->GetScriptStruct();
		check(ScriptStruct);
		ScriptStruct->DestroyStruct(Object);
		FMemory::Free(Object);
	}
};


/* static */ void FJoltLayeredMoveGroup::NetSerializeLayeredMovesArray(FArchive& Ar, TArray< TSharedPtr<FJoltLayeredMoveBase> >& LayeredMovesArray, uint8 MaxNumLayeredMovesToSerialize /*=MAX_uint8*/)
{
	uint8 NumMovesToSerialize;
	if (Ar.IsSaving())
	{
		UE_CLOG(LayeredMovesArray.Num() > MaxNumLayeredMovesToSerialize, LogJoltMover, Warning, TEXT("Too many Layered Moves (%d!) to net serialize. Clamping to %d"),
			LayeredMovesArray.Num(), MaxNumLayeredMovesToSerialize);

		NumMovesToSerialize = FMath::Min<int32>(LayeredMovesArray.Num(), MaxNumLayeredMovesToSerialize);
	}

	Ar << NumMovesToSerialize;

	if (Ar.IsLoading())
	{
		LayeredMovesArray.SetNumZeroed(NumMovesToSerialize);
	}

	for (int32 i = 0; i < NumMovesToSerialize && !Ar.IsError(); ++i)
	{
		TCheckedObjPtr<UScriptStruct> ScriptStruct = LayeredMovesArray[i].IsValid() ? LayeredMovesArray[i]->GetScriptStruct() : nullptr;
		UScriptStruct* ScriptStructLocal = ScriptStruct.Get();
		Ar << ScriptStruct;

		if (ScriptStruct.IsValid())
		{
			// Restrict replication to derived classes of FJoltLayeredMoveBase for security reasons:
			// If FJoltLayeredMoveGroup is replicated through a Server RPC, we need to prevent clients from sending us
			// arbitrary ScriptStructs due to the allocation/reliance on GetCppStructOps below which could trigger a server crash
			// for invalid structs. All provided sources are direct children of FJoltLayeredMoveBase and we never expect to have deep hierarchies
			// so this should not be too costly
			bool bIsDerivedFromBase = false;
			UStruct* CurrentSuperStruct = ScriptStruct->GetSuperStruct();
			while (CurrentSuperStruct)
			{
				if (CurrentSuperStruct == FJoltLayeredMoveBase::StaticStruct())
				{
					bIsDerivedFromBase = true;
					break;
				}
				CurrentSuperStruct = CurrentSuperStruct->GetSuperStruct();
			}

			if (bIsDerivedFromBase)
			{
				if (Ar.IsLoading())
				{
					if (LayeredMovesArray[i].IsValid() && ScriptStructLocal == ScriptStruct.Get())
					{
						// What we have locally is the same type as we're being serialized into, so we don't need to
						// reallocate - just use existing structure
					}
					else
					{
						// For now, just reset/reallocate the data when loading.
						// Longer term if we want to generalize this and use it for property replication, we should support
						// only reallocating when necessary
						FJoltLayeredMoveBase* NewMove = (FJoltLayeredMoveBase*)FMemory::Malloc(ScriptStruct->GetCppStructOps()->GetSize());
						ScriptStruct->InitializeStruct(NewMove);

						LayeredMovesArray[i] = TSharedPtr<FJoltLayeredMoveBase>(NewMove, FJoltLayeredMoveDeleter());
					}
				}

				LayeredMovesArray[i]->NetSerialize(Ar);
			}
			else
			{
				UE_LOG(LogJoltMover, Error, TEXT("FJoltLayeredMoveGroup::NetSerialize: ScriptStruct not derived from FJoltLayeredMoveBase attempted to serialize."));
				Ar.SetError();
				break;
			}
		}
		else if (ScriptStruct.IsError())
		{
			UE_LOG(LogJoltMover, Error, TEXT("FJoltLayeredMoveGroup::NetSerialize: Invalid ScriptStruct serialized."));
			Ar.SetError();
			break;
		}
	}
}

void FJoltLayeredMoveGroup::ResetResidualVelocity()
{
	bApplyResidualVelocity = false;
	ResidualVelocity = FVector::ZeroVector;
	ResidualClamping = -1.f;
}

void FJoltLayeredMoveGroup::Reset()
{
	ResetResidualVelocity();
	QueuedLayeredMoves.Empty();
	ActiveLayeredMoves.Empty();
	TagCancellationRequests.Empty();
}

