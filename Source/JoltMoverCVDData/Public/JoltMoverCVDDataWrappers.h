// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataWrappers/ChaosVDDataSerializationMacros.h"
#include "DataWrappers/ChaosVDParticleDataWrapper.h"
#include "UObject/ObjectMacros.h"

#include "JoltMoverCVDDataWrappers.generated.h"

USTRUCT(DisplayName="JoltMover Sim Data")
struct FJoltMoverCVDSimDataWrapper : public FChaosVDWrapperDataBase
{
	GENERATED_BODY()
	
	JOLTMOVERCVDDATA_API static FStringView WrapperTypeName;

	UPROPERTY(VisibleAnywhere, Category="JoltMover Info")
	int32 SolverID = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category="JoltMover Info")
	int32 ParticleID = INDEX_NONE;

	TArray<uint8> SyncStateBytes;
	TArray<uint8> SyncStateDataCollectionBytes;
	TArray<uint8> InputCmdBytes;
	TArray<uint8> InputJoltMoverDataCollectionBytes;
	TArray<uint8> LocalSimDataBytes;

	JOLTMOVERCVDDATA_API bool Serialize(FArchive& Ar);
};

CVD_IMPLEMENT_SERIALIZER(FJoltMoverCVDSimDataWrapper)

USTRUCT()
struct FJoltMoverCVDSimDataContainer
{
	GENERATED_BODY()

	TMap<int32, TArray<TSharedPtr<FJoltMoverCVDSimDataWrapper>>> SimDataBySolverID;
};