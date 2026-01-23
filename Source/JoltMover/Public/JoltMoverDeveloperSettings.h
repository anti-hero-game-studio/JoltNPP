// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettingsBackedByCVars.h"

#include "JoltMoverDeveloperSettings.generated.h"

#define UE_API JOLTMOVER_API

/** Developer settings for the Mover plugin */
UCLASS(MinimalAPI, config = Engine, defaultconfig, meta = (DisplayName = "JoltMover Settings"))
class UJoltMoverDeveloperSettings : public UDeveloperSettingsBackedByCVars
{
	GENERATED_BODY()
public:
	UE_API UJoltMoverDeveloperSettings();
	
	/**
     * This specifies the number of times a movement mode can refund all of the time in a substep before we back out to avoid freezing the game/editor
     */
    UPROPERTY(config, EditAnywhere, Category = "JoltMover")
    int32 MaxTimesToRefundSubstep;
	
};

#undef UE_API
