// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "JoltNetworkPredictionLagCompensationData.generated.h"

/**
 * 
 */

USTRUCT(BlueprintType)
struct JOLTNETWORKPREDICTION_API FNpLagCompensationData
{
	GENERATED_BODY()
	
	FNpLagCompensationData() {}

	FNpLagCompensationData(float InSimTimeMs)
	{
		SimTimeMs = InSimTimeMs;
	}
	virtual ~FNpLagCompensationData() {}
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LagCompensation)
	float SimTimeMs = 0.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LagCompensation)
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LagCompensation)
	FQuat Rotation = FQuat::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LagCompensation)
	FVector CollisionExtent = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LagCompensation)
	bool CanRewindFurther = true;
	// Can Add more here And Fill Them In UpdateLagCompensationHistory() inside UExtendedMoverComp class.
	

	virtual void Lerp(const TSharedPtr<FNpLagCompensationData>& From, const TSharedPtr<FNpLagCompensationData>& To , const float Alpha);

	virtual UScriptStruct* GetScriptStruct() const
	{
		return FNpLagCompensationData::StaticStruct();
	}

	virtual FNpLagCompensationData* Clone()
	{
		return new FNpLagCompensationData(*this);
	}

	virtual void ResetToDefault()
	{
		SimTimeMs = 0.f;
		Location = FVector::ZeroVector;
		Rotation = FQuat::Identity;
		CollisionExtent = FVector::ZeroVector;
		CanRewindFurther = true;
	}

	virtual bool operator==(const FNpLagCompensationData& Other) const
	{
		return SimTimeMs == Other.SimTimeMs; 
	}

	
	
};

struct FNpLagCompensationDataDeleter
{
	FORCEINLINE void operator()(FNpLagCompensationData* Object) const
	{
		check(Object);
		UScriptStruct* ScriptStruct = Object->GetScriptStruct();
		check(ScriptStruct);
		ScriptStruct->DestroyStruct(Object);
		FMemory::Free(Object);
	}
};

USTRUCT(BlueprintType)
struct JOLTNETWORKPREDICTION_API FNpLagCompensationHistory
{
	GENERATED_BODY()
	
	FNpLagCompensationHistory(UScriptStruct* DataType = FNpLagCompensationData::StaticStruct(), const int32 MaxSize = 1)
		: History(TCircularBuffer<TSharedPtr<FNpLagCompensationData>>(FMath::Max(MaxSize,2)))
	{
		HistoryDataType = FNpLagCompensationData::StaticStruct();
		if (DataType->IsChildOf(FNpLagCompensationData::StaticStruct()))
		{
			HistoryDataType = DataType;
		}
	}
	
	TCircularBuffer<TSharedPtr<FNpLagCompensationData>> History;
	uint32 HeadIndex = 0; 
	uint32 NumEntries = 0;
	UScriptStruct* HistoryDataType;
	TSharedPtr<FNpLagCompensationData> PreRewindData = nullptr;
	int32 LastPossibleRewindIndex = INDEX_NONE;
	bool bIsInRewind = false;

	void Add(const TSharedPtr<FNpLagCompensationData>& NewState)
	{
		// must ensure new entries have bigger sim time
		if (NumEntries > 0)
		{
			const int32 LastIndex = NumEntries - 1;
			TSharedPtr<FNpLagCompensationData>& LatestState = GetAtMutable(LastIndex);
			if (LatestState->SimTimeMs > NewState->SimTimeMs || FMath::IsNearlyEqual(LatestState->SimTimeMs,NewState->SimTimeMs))
			{
				return;
			}
		}
		
		bool bIsFull = (NumEntries == History.Capacity());

		// Overwriting oldest element?
		if (bIsFull)
		{
			// If cutoff exists, slide it forward
			if (LastPossibleRewindIndex != INDEX_NONE)
			{
				LastPossibleRewindIndex--;
				if (LastPossibleRewindIndex < 0)
				{
					LastPossibleRewindIndex = INDEX_NONE;
				}
			}
		}

		// Write new state
		History[HeadIndex] = TSharedPtr<FNpLagCompensationData>(NewState->Clone());

		const int32 NewStateIndex = HeadIndex;
		// If stop frame, cutoff is the newest valid frame before it
		if (!NewState->CanRewindFurther)
		{
			LastPossibleRewindIndex = NumEntries > 0 ? (NumEntries - 1) : INDEX_NONE;
		}

		// Advance
		HeadIndex = History.GetNextIndex(HeadIndex);
		if (!bIsFull)
		{
			++NumEntries;
		}
	}
	
	TSharedPtr<FNpLagCompensationData> GetLatestOrAddCopy(const float& SimTimeMs)
	{
		if (NumEntries > 0)
		{
			const int32 LastIndex = NumEntries - 1;
			TSharedPtr<FNpLagCompensationData>& LatestState = GetAtMutable(LastIndex);
			if (LatestState->SimTimeMs > SimTimeMs || FMath::IsNearlyEqual(LatestState->SimTimeMs,SimTimeMs))
			{
				return TSharedPtr<FNpLagCompensationData>(LatestState->Clone());
			}
		}
		
		bool bIsFull = (NumEntries == History.Capacity());

		// Overwriting oldest element?
		if (bIsFull)
		{
			// If cutoff exists, slide it forward
			if (LastPossibleRewindIndex != INDEX_NONE)
			{
				LastPossibleRewindIndex--;
				if (LastPossibleRewindIndex < 0)
				{
					LastPossibleRewindIndex = INDEX_NONE;
				}
			}
		}

		// Write new state, no need to allocate again if valid, just use existing and reset it.
		if (History[HeadIndex].IsValid())
		{
			History[HeadIndex]->ResetToDefault();
		}
		else
		{
			History[HeadIndex] = CreateDataByType(HistoryDataType);
		}
		
		History[HeadIndex]->SimTimeMs = SimTimeMs;
		const int32 NewStateIndex = HeadIndex;
		// If stop frame, cutoff is the newest valid frame before it
		// Advance
		HeadIndex = History.GetNextIndex(HeadIndex);
		if (!bIsFull)
		{
			++NumEntries;
		}
		return TSharedPtr<FNpLagCompensationData>(History[NewStateIndex]->Clone());
	}
	
	void WriteToLatestState(const TSharedPtr<FNpLagCompensationData>& State)
	{
		if (NumEntries > 0)
		{
			const int32 LastIndex = NumEntries - 1;
			TSharedPtr<FNpLagCompensationData>& LatestState = GetAtMutable(LastIndex);
			if (FMath::IsNearlyEqual(LatestState->SimTimeMs,State->SimTimeMs))
			{
				LatestState = TSharedPtr<FNpLagCompensationData>(State->Clone());
			}
			if (!LatestState->CanRewindFurther)
			{
				LastPossibleRewindIndex = FMath::Max<int32>(NumEntries - 2,0);
			}
		}
	}
	
	uint32 Num() const { return NumEntries; }
	
	const TSharedPtr<FNpLagCompensationData>& Last() const
	{
		const int32 LastIndex = NumEntries - 1;
		return GetAt(LastIndex);
	}

	TSharedPtr<FNpLagCompensationData> LastCopy() const
	{
		const int32 LastIndex = NumEntries - 1;
		return TSharedPtr<FNpLagCompensationData>(GetAt(LastIndex)->Clone());
	}
	
	const TSharedPtr<FNpLagCompensationData>& GetAt(uint32 Index) const
	{
		// Logical index 0 = oldest, Num()-1 = newest
		uint32 OldestIndex = (HeadIndex + (History.Capacity() - NumEntries)) & (History.Capacity() - 1);
		return History[(OldestIndex + Index) & (History.Capacity() - 1)];
	}
	
	TSharedPtr<FNpLagCompensationData>& GetAtMutable(uint32 Index)
	{
		// Logical index 0 = oldest, Num()-1 = newest
		uint32 OldestIndex = (HeadIndex + (History.Capacity() - NumEntries)) & (History.Capacity() - 1);
		return History[(OldestIndex + Index) & (History.Capacity() - 1)];
	}
	
	static TSharedPtr<FNpLagCompensationData> CreateDataByType(const UScriptStruct* DataStructType)
	{
		FNpLagCompensationData* NewDataBlock = static_cast<FNpLagCompensationData*>(FMemory::Malloc(DataStructType->GetCppStructOps()->GetSize()));
		DataStructType->InitializeStruct(NewDataBlock);

		return TSharedPtr<FNpLagCompensationData>(NewDataBlock, FNpLagCompensationDataDeleter());
	}

	TSharedPtr<FNpLagCompensationData> GetStateAtTime(const float& SimTimeMS) const;
};

