// Fill out your copyright notice in the Description page of Project Settings.


#include "Toolbar/JoltToolbarCommands.h"
#define LOCTEXT_NAMESPACE "FNewPluginModule"

void FJoltToolbarCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "Jolt", "Convert Actors", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE