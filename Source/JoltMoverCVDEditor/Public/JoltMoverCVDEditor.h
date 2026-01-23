// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FChaosVDExtension;

/** This module contains the Editor side of the JoltMover support in Chaos Visual Debugger (CVD)
* JoltMover is a module responsible for the movement of actors and its replication, for instance the input controlled movement of characters
* This module allows the display and recording specific data in CVD that makes it easier to understand and debug JoltMover
* JoltMoverCVDTab is the tab in CVD where that information is displayed
* JoltMoverCVDSimDataProcessor is the receiving and processing end of the JoltMover data trace ("Sim Data") that the game sends to CVD
* JoltMoverSimDataComponent is a component holding JoltMover data for the current visualized frame
* JoltMoverCVDExtension is where we register JoltMoverCVDTab as a displayable tab, register JoltMoverCVDSimDataProcessor and give access to the JoltMoverSimDataComponent
*/
class FJoltMoverCVDEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:

	TArray<TWeakPtr<FChaosVDExtension>> AvailableExtensions;
};
