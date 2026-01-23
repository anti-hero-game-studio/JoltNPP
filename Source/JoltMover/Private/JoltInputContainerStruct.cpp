// Copyright Epic Games, Inc. All Rights Reserved.


#include "JoltInputContainerStruct.h"
#include "JoltMoverLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltInputContainerStruct)

#define LOCTEXT_NAMESPACE "JoltMoverInputContainerStruct"



void FJoltMoverInputContainerDataStruct::Interpolate(const FJoltMoverDataStructBase& From, const FJoltMoverDataStructBase& To, float LerpFactor)
{
	const FJoltMoverInputContainerDataStruct* FromContainer = static_cast<const FJoltMoverInputContainerDataStruct*>(&From);
	const FJoltMoverInputContainerDataStruct* ToContainer = static_cast<const FJoltMoverInputContainerDataStruct*>(&To);

	Collection.Interpolate(FromContainer->Collection, ToContainer->Collection, LerpFactor);
}


FJoltMoverDataStructBase* FJoltMoverInputContainerDataStruct::Clone() const
{
	FJoltMoverInputContainerDataStruct* CopyPtr = new FJoltMoverInputContainerDataStruct(*this);
	return CopyPtr;
}

bool FJoltMoverInputContainerDataStruct::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	if (!Super::NetSerialize(Ar, Map, bOutSuccess))
	{
		bOutSuccess = false;
		return false;
	}

	if (!Collection.NetSerialize(Ar, Map, bOutSuccess))
	{
		bOutSuccess = false;
		return false;
	}

	bOutSuccess = true;
	return true;
}


#undef LOCTEXT_NAMESPACE
