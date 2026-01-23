// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Math/Box.h"
#include "Misc/NetworkGuid.h"

class FNetworkGUID;

// non ModelDef specific debug helpers
namespace JoltNetworkPredictionDebug
{
	JOLTNETWORKPREDICTION_API void DrawDebugOutline(FTransform Transform, FBox BoundingBox, FColor Color, float Lifetime);
	JOLTNETWORKPREDICTION_API void DrawDebugText3D(const TCHAR* Str, FTransform Transform, FColor, float Lifetime);
	JOLTNETWORKPREDICTION_API UObject* FindReplicatedObjectOnPIEServer(UObject* ClientObject);
	JOLTNETWORKPREDICTION_API FNetworkGUID FindObjectNetGUID(UObject* Obj);
};
