// Fill out your copyright notice in the Description page of Project Settings.


#include "JoltNetworkPredictionLagCompensationData.h"


void FNpLagCompensationData::Lerp(const TSharedPtr<FNpLagCompensationData>& From, const TSharedPtr<FNpLagCompensationData>& To,const float Alpha)
{
	static constexpr float TeleportThreshold = 500.f * 500.f;
	if (FVector::DistSquared(From->Location, To->Location) > TeleportThreshold)
	{
		Location = To->Location;
	}
	else
	{
		Location = FMath::Lerp(From->Location,To->Location,Alpha);
	}
	CollisionExtent = FMath::Lerp(From->CollisionExtent,To->CollisionExtent,Alpha);
	Rotation = FMath::Lerp(From->Rotation,To->Rotation,Alpha);
	CanRewindFurther = Alpha > 0.5 ? To->CanRewindFurther : From->CanRewindFurther;
}

TSharedPtr<FNpLagCompensationData> FNpLagCompensationHistory::GetStateAtTime(const float& SimTimeMS) const
{
	if (Num() <= 0)
	{
		return nullptr;
	}
	const int32 NumSamples = Num();
	const int32 LastIndex =  NumSamples - 1;
	TSharedPtr<FNpLagCompensationData> LagCompensationData = TSharedPtr<FNpLagCompensationData>(GetAt(LastIndex)->Clone());
	LagCompensationData->SimTimeMs = SimTimeMS;
	
	if (NumSamples <= 0)
	{
		return LagCompensationData;
	}

	// The number of elements in History*
	const float MinTime = GetAt(0)->SimTimeMs;
	const float MaxTime = GetAt(LastIndex)->SimTimeMs;

	// ✅ Cutoff check
	if (LastPossibleRewindIndex != INDEX_NONE)
	{
		const auto& Cutoff = GetAt(LastPossibleRewindIndex);
		if (SimTimeMS < Cutoff->SimTimeMs)
		{
			return TSharedPtr<FNpLagCompensationData>(Cutoff->Clone());
		}
	}
	
	if (SimTimeMS >= MaxTime - 1) // this 1 is for precision
	{
		return TSharedPtr<FNpLagCompensationData>(GetAt(LastIndex)->Clone());
	}
	if (SimTimeMS <= MinTime + 1)
	{
		return TSharedPtr<FNpLagCompensationData>(GetAt(0)->Clone());
	}
	// Clamp Value To Eliminate Edge Cases.
	const float ClampedServerTime = FMath::Clamp(SimTimeMS,MinTime,MaxTime);

	if (NumSamples < 2 || MinTime == MaxTime)
	{
		return TSharedPtr<FNpLagCompensationData>(GetAt(LastIndex)->Clone());
	}

	// Binary search*
	int32 NextIndex = 1;
	int32 Count = LastIndex - NextIndex;
	while (Count > 0)
	{
		const int32 Step = Count / 2;
		const int32 Middle = NextIndex + Step;

		if (ClampedServerTime > GetAt(Middle)->SimTimeMs)
		{
			NextIndex = Middle + 1;
			Count -= Step + 1;
		}
		else
		{
			Count = Step;
		}
	}

	const int32 PrevIndex = NextIndex - 1;

	const float PrevCurveTime = GetAt(PrevIndex)->SimTimeMs;
	const float NextCurveTime = GetAt(NextIndex)->SimTimeMs;

	// Find time by two nearest known points in History*
	const float Diff = NextCurveTime - PrevCurveTime;
	const float Alpha = !FMath::IsNearlyZero(Diff) ? (ClampedServerTime - PrevCurveTime) / Diff : 0.0f;
	LagCompensationData->Lerp(GetAt(PrevIndex), GetAt(NextIndex), Alpha);
	return LagCompensationData;
}