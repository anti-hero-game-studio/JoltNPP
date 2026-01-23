// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltMoverCVDSimDataProcessor.h"
#include "JoltMoverCVDDataWrappers.h"

FJoltMoverCVDSimDataProcessor::FJoltMoverCVDSimDataProcessor() : FChaosVDDataProcessorBase(FJoltMoverCVDSimDataWrapper::WrapperTypeName)
{
}

bool FJoltMoverCVDSimDataProcessor::ProcessRawData(const TArray<uint8>& InData)
{
	FChaosVDDataProcessorBase::ProcessRawData(InData);

	const TSharedPtr<FChaosVDTraceProvider> ProviderSharedPtr = TraceProvider.Pin();
	if (!ensure(ProviderSharedPtr.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FJoltMoverCVDSimDataWrapper> SimData = MakeShared<FJoltMoverCVDSimDataWrapper>();
	const bool bSuccess = Chaos::VisualDebugger::ReadDataFromBuffer(InData, *SimData, ProviderSharedPtr.ToSharedRef());
	
	if (bSuccess)
	{
		if (FChaosVDSolverFrameData* CurrentSolverFrameData = ProviderSharedPtr->GetCurrentSolverFrame(SimData->SolverID))
		{
			if (TSharedPtr<FJoltMoverCVDSimDataContainer> SimDataContainer = CurrentSolverFrameData->GetCustomData().GetOrAddDefaultData<FJoltMoverCVDSimDataContainer>())
			{
				SimDataContainer->SimDataBySolverID.FindOrAdd(SimData->SolverID).Add(SimData);
			}
		}
	}

	return bSuccess;
}
