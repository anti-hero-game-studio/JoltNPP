// Fill out your copyright notice in the Description page of Project Settings.


#include "DefaultMovementSet/Modes/Physics/JoltPhysicsMovementMode.h"
#include "JoltMoverComponent.h"
#include "DefaultMovementSet/Settings/JoltCommonLegacyMovementSettings.h"



float UJoltPhysicsMovementMode::GetMaxSpeed() const
{
	if (MaxSpeedOverride.IsSet())
	{
		return MaxSpeedOverride.GetValue();
	}
	
	if (const UJoltCommonLegacyMovementSettings* SharedSettingsPtr = GetMoverComponent<UJoltMoverComponent>()->FindSharedSettings<UJoltCommonLegacyMovementSettings>())
	{
		return SharedSettingsPtr->MaxSpeed;
	}

	UE_LOG(LogJoltMover, Warning, TEXT("Invalid max speed on CharacterJoltMoverComponent"));
	return 0.0f;
}

void UJoltPhysicsMovementMode::OverrideMaxSpeed(float Value)
{
	MaxSpeedOverride = Value;
}

void UJoltPhysicsMovementMode::ClearMaxSpeedOverride()
{
	MaxSpeedOverride.Reset();
}

float UJoltPhysicsMovementMode::GetAcceleration() const
{
	if (AccelerationOverride.IsSet())
	{
		return AccelerationOverride.GetValue();
	}
	
	if (const UJoltCommonLegacyMovementSettings* SharedSettingsPtr = GetMoverComponent<UJoltMoverComponent>()->FindSharedSettings<UJoltCommonLegacyMovementSettings>())
	{
		return SharedSettingsPtr->Acceleration;
	}
	
	UE_LOG(LogJoltMover, Warning, TEXT("Invalid acceleration on CharacterJoltMoverComponent"));
	return 0.0f;
}


void UJoltPhysicsMovementMode::OverrideAcceleration(const float Value)
{
	AccelerationOverride = Value;
}

void UJoltPhysicsMovementMode::ClearAccelerationOverride()
{
	AccelerationOverride.Reset();
}