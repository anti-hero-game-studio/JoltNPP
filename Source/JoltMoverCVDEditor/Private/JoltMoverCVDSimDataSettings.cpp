// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltMoverCVDSimDataSettings.h"

#include "ChaosVDSettingsManager.h"
#include "Utils/ChaosVDUserInterfaceUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMoverCVDSimDataSettings)

void UJoltMoverCVDSimDataSettings::SetDataVisualizationFlags(EJoltMoverCVDSimDataVisualizationFlags NewFlags)
{
	if (UJoltMoverCVDSimDataSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UJoltMoverCVDSimDataSettings>())
	{
		Settings->DebugDrawFlags = static_cast<uint32>(NewFlags);
		Settings->BroadcastSettingsChanged();
	}
}

EJoltMoverCVDSimDataVisualizationFlags UJoltMoverCVDSimDataSettings::GetDataVisualizationFlags()
{
	if (UJoltMoverCVDSimDataSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UJoltMoverCVDSimDataSettings>())
	{
		return static_cast<EJoltMoverCVDSimDataVisualizationFlags>(Settings->DebugDrawFlags);
	}

	return EJoltMoverCVDSimDataVisualizationFlags::None;
}

bool UJoltMoverCVDSimDataSettings::CanVisualizationFlagBeChangedByUI(uint32 Flag)
{
	return Chaos::VisualDebugger::Utils::ShouldVisFlagBeEnabledInUI(Flag, DebugDrawFlags, EJoltMoverCVDSimDataVisualizationFlags::EnableDraw);
}
