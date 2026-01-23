// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JoltMoverSimulationTypes.h"
#include "MoveLibrary/JoltFloorQueryUtils.h"

#include "CharacterJoltMoverSimulationTypes.generated.h"

USTRUCT()
struct FJoltLandedEventData : public FJoltMoverSimulationEventData
{
	GENERATED_BODY()

	FJoltLandedEventData(double InEventTimeMs, const FHitResult& InHitResult, const FName InNewModeName)
		: FJoltMoverSimulationEventData(InEventTimeMs)
		, HitResult(InHitResult)
		, NewModeName(InNewModeName)
	{
	}
	FJoltLandedEventData() {}

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FJoltLandedEventData::StaticStruct();
	}

	FHitResult HitResult;
	FName NewModeName = NAME_None;
};

USTRUCT()
struct FJoltJumpedEventData : public FJoltMoverSimulationEventData
{
	GENERATED_BODY()

	FJoltJumpedEventData(double InEventTimeMs, float InJumpStartHeight)
		: FJoltMoverSimulationEventData(InEventTimeMs)
		, JumpStartHeight(InJumpStartHeight)
	{
	}
	FJoltJumpedEventData() {}

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FJoltJumpedEventData::StaticStruct();
	}

	float JumpStartHeight = 0.0f;
};

USTRUCT(BlueprintType)
struct FJoltFloorResultData : public FJoltMoverDataStructBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = Mover)
	FJoltFloorCheckResult FloorResult;

	FJoltFloorResultData() = default;
	virtual ~FJoltFloorResultData() = default;

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return StaticStruct();
	}

	virtual FJoltMoverDataStructBase* Clone() const override
	{
		return new FJoltFloorResultData(*this);
	}

	JOLTMOVER_API virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;
	JOLTMOVER_API virtual void ToString(FAnsiStringBuilderBase& Out) const override;
	JOLTMOVER_API virtual bool ShouldReconcile(const FJoltMoverDataStructBase& AuthorityState) const override;
	JOLTMOVER_API virtual void Interpolate(const FJoltMoverDataStructBase& From, const FJoltMoverDataStructBase& To, float Pct) override;
	JOLTMOVER_API virtual void Merge(const FJoltMoverDataStructBase& From) override;
	JOLTMOVER_API virtual void Decay(float DecayAmount) override;
};

template<>
struct TStructOpsTypeTraits< FJoltFloorResultData > : public TStructOpsTypeTraitsBase2< FJoltFloorResultData >
{
	enum
	{
		WithCopy = true
	};
};