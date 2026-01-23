// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltMoverCVDDataWrappers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMoverCVDDataWrappers)

FStringView FJoltMoverCVDSimDataWrapper::WrapperTypeName = TEXT("FJoltMoverCVDSimDataWrapper");

bool FJoltMoverCVDSimDataWrapper::Serialize(FArchive& Ar)
{
	Ar << bHasValidData;

	if (!bHasValidData)
	{
		return !Ar.IsError();
	}

	Ar << SolverID;
	Ar << ParticleID;
	Ar << SyncStateBytes;
	Ar << SyncStateDataCollectionBytes;
	Ar << InputCmdBytes;
	Ar << InputJoltMoverDataCollectionBytes;
	Ar << LocalSimDataBytes;

	return !Ar.IsError();
}
