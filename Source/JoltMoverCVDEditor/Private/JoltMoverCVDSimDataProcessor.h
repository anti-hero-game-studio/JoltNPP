// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "../../../../../../../../EpicGames/UE_5.7/Engine/Plugins/ChaosVD/Source/ChaosVD/Public/Trace/DataProcessors/ChaosVDDataProcessorBase.h"

/**
 * Data processor implementation that is able to deserialize traced JoltMover data
 */
class FJoltMoverCVDSimDataProcessor final : public FChaosVDDataProcessorBase
{
public:
	explicit FJoltMoverCVDSimDataProcessor();
	
	virtual bool ProcessRawData(const TArray<uint8>& InData) override;
};


