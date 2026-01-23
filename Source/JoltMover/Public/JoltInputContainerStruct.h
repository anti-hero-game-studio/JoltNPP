// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JoltMoverTypes.h"
#include "JoltInputContainerStruct.generated.h"



#define UE_API JOLTMOVER_API

/** 
 * Wrapper class that's used to include input structs in the sync state without them causing reconciliation.
 * This is intended only for internal use.
 */

 USTRUCT()
 struct FJoltMoverInputContainerDataStruct : public FJoltMoverDataStructBase
 {
	GENERATED_BODY()

public:
	// All input data in this struct
	FJoltMoverDataCollection Collection;

	// Implementation of FJoltMoverDataStructBase

	// This struct never triggers reconciliation
	virtual bool ShouldReconcile(const FJoltMoverDataStructBase& AuthorityState) const override { return false; }
	virtual void Interpolate(const FJoltMoverDataStructBase& From, const FJoltMoverDataStructBase& To, float LerpFactor) override;
	virtual FJoltMoverDataStructBase* Clone() const override;
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;

	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }

};


#undef UE_API
