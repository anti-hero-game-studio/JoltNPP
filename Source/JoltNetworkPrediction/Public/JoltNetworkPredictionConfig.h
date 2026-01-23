// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JoltNetworkPredictionConfig.generated.h"

// Must be kept in sync with EJNP_TickingPolicy
UENUM()
enum class EJoltNetworkPredictionTickingPolicy : uint8
{
	// Client ticks at local frame rate. Server ticks clients independently at client input cmd rate.
	Independent	= 1 << 0,
	// Everyone ticks at same fixed rate. Supports group rollback
	Fixed		= 1 << 1,

	All = Independent | Fixed UMETA(Hidden),
};
ENUM_CLASS_FLAGS(EJoltNetworkPredictionTickingPolicy);

enum class EJoltNetworkPredictionLocalInputPolicy : uint8
{
	// Up to the user to write input via FJoltNetSimProxy::WriteInputCmd.
	Passive,
	// ProduceInput is called on the driver before every simulation frame. This may be necessary for things like aim assist and fixed step simulations that run multiple sim frames per engine frame
	PollPerSimFrame,
};

// Must be kept in sync with EJNP_NetworkLOD. Note: SimExtrapolate Not currently implemented so it is hidden
UENUM()
enum class EJoltNetworkLOD : uint8
{
	Interpolated	= 1 << 0,
	SimExtrapolate	= 1 << 1 UMETA(Hidden),
	ForwardPredict	= 1 << 2,

	All = Interpolated | SimExtrapolate | ForwardPredict UMETA(Hidden),
};
ENUM_CLASS_FLAGS(EJoltNetworkLOD);

static constexpr EJoltNetworkLOD GetHighestNetworkLOD(EJoltNetworkLOD Mask)
{
	if ((uint8)Mask >= (uint8)EJoltNetworkLOD::ForwardPredict)
	{
		return EJoltNetworkLOD::ForwardPredict;
	}

	if ((uint8)Mask >= (uint8)EJoltNetworkLOD::SimExtrapolate)
	{
		return EJoltNetworkLOD::SimExtrapolate;
	}

	return EJoltNetworkLOD::Interpolated;
}

// -------------------------------------------------------------------------------------------------------------

// What a ModelDef of capable of
struct FJoltNetworkPredictionModelDefCapabilities
{
	struct FSupportedNetworkLODs
	{
		EJoltNetworkLOD	AP;
		EJoltNetworkLOD	SP;
	};

	FSupportedNetworkLODs FixedNetworkLODs = FSupportedNetworkLODs{ EJoltNetworkLOD::All, EJoltNetworkLOD::All };
	FSupportedNetworkLODs IndependentNetworkLODs = FSupportedNetworkLODs{ EJoltNetworkLOD::All, EJoltNetworkLOD::Interpolated | EJoltNetworkLOD::SimExtrapolate };

	EJoltNetworkPredictionTickingPolicy SupportedTickingPolicies = EJoltNetworkPredictionTickingPolicy::All;
};

// How a registered instance should behave globally. That is, independent of any instance state (local role, connection, significance, local budgets). E.g, everyone agrees on this.
// This can be changed explicitly by the user or simulation. For example, a sim that transitions between fixed and independent ticking modes.
struct FJoltNetworkPredictionInstanceArchetype
{
	EJoltNetworkPredictionTickingPolicy	TickingMode;
	void NetSerialize(FArchive& Ar)
	{
		Ar << TickingMode;
	}
};

// The config should tell us what services we should be subscribed to. See UJoltNetworkPredictionWorldManager::ConfigureInstance
// This probably needs to be split into two parts:
//	1. What settings/config that the server is authority over and must be agreed on (TickingPolicy)
//	2. What are the local settings that can be lodded around?
struct FJoltNetworkPredictionInstanceConfig
{
	EJoltNetworkPredictionLocalInputPolicy InputPolicy = EJoltNetworkPredictionLocalInputPolicy::Passive;
	EJoltNetworkLOD NetworkLOD = EJoltNetworkLOD::ForwardPredict;
};
