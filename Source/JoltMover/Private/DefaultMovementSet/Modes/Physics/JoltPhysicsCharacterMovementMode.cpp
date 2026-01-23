// Fill out your copyright notice in the Description page of Project Settings.


#include "DefaultMovementSet/Modes/Physics/JoltPhysicsCharacterMovementMode.h"
#include "JoltMoverComponent.h"
#include "DefaultMovementSet/Settings/JoltCommonLegacyMovementSettings.h"


float UJoltPhysicsCharacterMovementMode::GetMaxWalkSlopeCosine() const
{
	if (const UJoltCommonLegacyMovementSettings* SharedSettingsPtr = GetMoverComponent<UJoltMoverComponent>()->FindSharedSettings<UJoltCommonLegacyMovementSettings>())
	{
		return SharedSettingsPtr->MaxStepHeight;
	}

	return 0.707f;
}


void UJoltPhysicsCharacterMovementMode::SetTargetHeightOverride(const float InTargetHeight)
{
	TargetHeightOverride = InTargetHeight;
	TargetHeight = InTargetHeight;
}

void UJoltPhysicsCharacterMovementMode::ClearTargetHeightOverride()
{
	TargetHeightOverride.Reset();

	TargetHeight = GetDefault<UJoltPhysicsCharacterMovementMode>(GetClass())->TargetHeight;
}

void UJoltPhysicsCharacterMovementMode::SetQueryRadiusOverride(const float InQueryRadius)
{
	QueryRadiusOverride = InQueryRadius;
	QueryRadius = InQueryRadius;
}

void UJoltPhysicsCharacterMovementMode::ClearQueryRadiusOverride()
{
	QueryRadiusOverride.Reset();

	QueryRadius = GetDefault<UJoltPhysicsCharacterMovementMode>(GetClass())->QueryRadius;
}
