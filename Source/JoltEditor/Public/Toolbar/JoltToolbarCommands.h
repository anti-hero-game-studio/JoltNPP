// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "JoltToolbarStyle.h"

class FJoltToolbarCommands : public TCommands<FJoltToolbarCommands>
{
public:

	FJoltToolbarCommands()
		: TCommands<FJoltToolbarCommands>(TEXT("Jolt"), NSLOCTEXT("Contexts", "Jolt", "Convert Actors"), NAME_None, FJoltToolbarStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction;
};

