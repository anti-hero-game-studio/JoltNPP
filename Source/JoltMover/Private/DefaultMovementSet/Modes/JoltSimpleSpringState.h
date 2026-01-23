// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JoltMovementMode.h"
#include "JoltMoverTypes.h"
#include "JoltSimpleSpringState.generated.h"

/**
* Internal state data for the SimpleSpringWalkingMode
*/
USTRUCT()
struct FJoltSimpleSpringState : public FJoltMoverDataStructBase
{
	GENERATED_BODY()
	
	virtual UScriptStruct* GetScriptStruct() const override;
	virtual FJoltMoverDataStructBase* Clone() const override;
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;
	virtual void ToString(FAnsiStringBuilderBase& Out) const override;
	virtual bool ShouldReconcile(const FJoltMoverDataStructBase& AuthorityState) const override;
	virtual void Interpolate(const FJoltMoverDataStructBase& From, const FJoltMoverDataStructBase& To, float Pct) override;

	// Acceleration of internal spring model
	UPROPERTY(BlueprintReadOnly, Category = "Jolt Mover|Experimental")
	FVector CurrentAccel = FVector::ZeroVector;
};
