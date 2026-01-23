// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JoltMoverTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "JoltBlueprintDataExt.generated.h"


#define UE_API JOLTMOVER_API

/** Data block containing mappings between names and commonly-used types, so that Blueprint-only devs can include custom data
 * in their project's sync state or input cmds without needing native code changes.
 * EXPERIMENTAL: this will be removed in favor of generic user-defined struct support. If this is for Blueprint usage,
 * consider using FJoltMoverUserDefinedDataStruct instead. If native, consider deriving your own FJoltMoverDataStructBase type.
 */ 
USTRUCT(BlueprintType, Experimental)
struct FJoltMoverDictionaryData : public FJoltMoverDataStructBase
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Mover)
	TMap<FName, bool> BoolValues;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Mover)
	TMap<FName, int32> IntValues;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Mover)
	TMap<FName, double> FloatValues;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Mover)
	TMap<FName, FVector> VectorValues;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Mover)
	TMap<FName, FRotator> RotatorValues;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Mover)
	TMap<FName, FName> NameValues;


	virtual FJoltMoverDataStructBase* Clone() const override
	{
		return new FJoltMoverDictionaryData(*this);
	}

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return StaticStruct();
	}

	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;
	virtual void ToString(FAnsiStringBuilderBase& Out) const override;
	virtual void Interpolate(const FJoltMoverDataStructBase& From, const FJoltMoverDataStructBase& To, float Alpha) override;
	virtual void Merge(const FJoltMoverDataStructBase& From) override;
	virtual bool ShouldReconcile(const FJoltMoverDataStructBase& AuthorityState) const override;

};


#undef UE_API
