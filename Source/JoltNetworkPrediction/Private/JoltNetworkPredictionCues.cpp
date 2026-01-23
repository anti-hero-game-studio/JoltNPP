// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltNetworkPredictionCues.h"

DEFINE_LOG_CATEGORY(LogJoltNetworkPredictionCues);

FGlobalCueTypeTable FGlobalCueTypeTable::Singleton;

FGlobalCueTypeTable::FRegisteredCueTypeInfo& FGlobalCueTypeTable::GetRegistedTypeInfo()
{
	static FGlobalCueTypeTable::FRegisteredCueTypeInfo PendingCueTypes;
	return PendingCueTypes;
}
