// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JoltNetworkPredictionCheck.h"
#include "UObject/NameTypes.h"

namespace UE_NP
{
#ifndef JNP_MAX_ASYNC_MODEL_DEFS
#define JNP_MAX_ASYNC_MODEL_DEFS 16
#endif

	const int32 MaxAsyncModelDefs = JNP_MAX_ASYNC_MODEL_DEFS;

#ifndef JNP_NUM_FRAME_STORAGE
#define JNP_NUM_FRAME_STORAGE 64
#endif

	const int32 NumFramesStorage = JNP_NUM_FRAME_STORAGE;

#ifndef JNP_FRAME_STORAGE_GROWTH
#define JNP_FRAME_STORAGE_GROWTH 8
#endif

	const int32 FrameStorageGrowth = JNP_FRAME_STORAGE_GROWTH;

#ifndef JNP_FRAME_INPUTCMD_BUFFER_SIZE
#define JNP_FRAME_INPUTCMD_BUFFER_SIZE 16
#endif

	const int32 InputCmdBufferSize = JNP_FRAME_INPUTCMD_BUFFER_SIZE;

#ifndef JNP_INLINE_SIMOBJ_INPUTS
#define JNP_INLINE_SIMOBJ_INPUTS 3
#endif

	const int32 InlineSimObjInputs = JNP_INLINE_SIMOBJ_INPUTS;

};

// Sets index to value, resizing bit array if necessary and setting new bits to false
template<typename BitArrayType>
void JnpResizeAndSetBit(BitArrayType& BitArray, int32 Index, bool Value=true)
{
	if (!BitArray.IsValidIndex(Index))
	{
		const int32 PreNum = BitArray.Num();
		BitArray.SetNumUninitialized(Index+1);
		BitArray.SetRange(PreNum, BitArray.Num() - PreNum, false);
		jnpCheckSlow(BitArray.IsValidIndex(Index));
	}

	BitArray[Index] = Value;
}

// Resize BitArray to NewNum, setting default value of new bits to false
template<typename BitArrayType>
void JnpResizeBitArray(BitArrayType& BitArray, int32 NewNum)
{
	if (BitArray.Num() < NewNum)
	{
		const int32 PreNum = BitArray.Num();
		BitArray.SetNumUninitialized(NewNum);
		BitArray.SetRange(PreNum, BitArray.Num() - PreNum, false);
		jnpCheckSlow(BitArray.Num() == NewNum);
	}
}

// Set bit array contents to false
template<typename BitArrayType>
void JnpClearBitArray(BitArrayType& BitArray)
{
	BitArray.SetRange(0, BitArray.Num(), false);
}

template<typename ArrayType>
void JnpResizeForIndex(ArrayType& Array, int32 Index)
{
	jnpEnsure(Index >= 0);
	if (Array.IsValidIndex(Index) == false)
	{
		Array.SetNum(Index + UE_NP::FrameStorageGrowth);
	}
}

class AActor;
