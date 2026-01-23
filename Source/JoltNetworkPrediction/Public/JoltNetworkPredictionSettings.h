// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "JoltNetworkPredictionConfig.h"
#include "Templates/SubclassOf.h"
#include "CoreMinimal.h"
#include "JoltNetworkPredictionReplicatedManager.h"
#include "JoltNetworkPredictionSettings.generated.h"

class AJoltNetworkPredictionReplicatedManager;
struct FPropertyChangedEvent;

USTRUCT(meta=(ShowOnlyInnerProperties))
struct FJoltNetworkPredictionSettings
{
	GENERATED_BODY()

	// Which ticking policy to use in cases where both are supported by the underlying simulation.
	UPROPERTY(config, EditAnywhere, Category = Global)
	EJoltNetworkPredictionTickingPolicy PreferredTickingPolicy = EJoltNetworkPredictionTickingPolicy::Fixed;

	// Replicated Manager class
	UPROPERTY(config, EditAnywhere, Category = Global)
	TSubclassOf<AJoltNetworkPredictionReplicatedManager> ReplicatedManagerClassOverride;

	// ------------------------------------------------------------------------------------------

	// Frame rate to use when running Fixed Tick simulations. Note: Engine::FixedFrameRate will take precedence if Engine::bUseFixedFrameRate is enabled.
	UPROPERTY(config, EditAnywhere, Category = FixedTick)
	float FixedTickFrameRate = 62.5;

	// Default NetworkLOD for simulated proxy simulations.
	UPROPERTY(config, EditAnywhere, Category = FixedTick)
	EJoltNetworkLOD SimulatedProxyNetworkLOD = EJoltNetworkLOD::Interpolated;

	// If enabled, the fixed tick smoothing service will be active, allowing drivers to receive smoothly interpolated updates for any locally-simulated objects (including forward-predicted sim proxies).
	UPROPERTY(config, EditAnywhere, Category = FixedTick)
	bool bEnableFixedTickSmoothing = true;

	// Smoothing Speed From 0.1-1. 0 makes correction never applied and is not allowed.
	// 1 will make correction get applied instantly, so Teleport. This happens only on the smoothing mesh , not to capsule.
	// capsule always teleported to where it should be, smoothing is only visual.
	UPROPERTY(config, EditAnywhere, Category = FixedTick,meta=(UIMin = 0.1f,UIMax = 1.f,ClampMin = 0.1f,ClampMax = 1.f))
	float SmoothingSpeed = 0.1f;
	// ------------------------------------------------------------------------------------------

	// How much buffered time to keep for fixed ticking interpolated sims (client only).
	UPROPERTY(config, EditAnywhere, Category = Interpolation)
	int32 FixedTickInterpolationBufferedMS = 100;

	// How much buffered time to keep for fixed independent interpolated sims (client only).
	UPROPERTY(config, EditAnywhere, Category = Interpolation)
	int32 IndependentTickInterpolationBufferedMS = 100;

	// Max buffered time to keep for fixed independent interpolated sims (client only).
	UPROPERTY(config, EditAnywhere, Category = Interpolation)
	int32 IndependentTickInterpolationMaxBufferedMS = 250;

	// ------------------------------------------------------------------------------------------

	// Clients should send this number of most recent input commands together per update, for a Fixed Tick simulation
	UPROPERTY(config, EditAnywhere, Category = Input)
	int32 FixedTickInputSendCount = 6;

	UPROPERTY(config, EditAnywhere, Category = Input)
	int32 FixedTickDesiredBufferedInputCount = 4;

	// Clients should send this number of most recent input commands together per update, for an Independent Tick simulation
	UPROPERTY(config, EditAnywhere, Category = Input)
	int32 IndependentTickInputSendCount = 6;

	// Cap the number of remote input commands required to be buffered before resuming input consumption after a fault
	UPROPERTY(config, EditAnywhere, Category = Input)
	int32 MaximumRemoteInputFaultLimit = 6;


	// this represents how much ping the lag compensation supports.
	// this does not mean a specific player will be rewound max for this duration,
	// FixedTickInterpolationBufferedMS + (FixedTickDesiredBufferedInputCount * FixedTickMS) is added to this.
	// to ensure that player shooting do not feel like there's time they have to lead their shots by not based on their ping.
	// when time gets clamped and server doesn't rewind to desired time there will be an error in the log.
	UPROPERTY(config, EditAnywhere, Category = LagCompensation)
	int32 MaxRewindTimeMS = 200;

	// the max history duration kept in the buffer in Milliseconds
	UPROPERTY(config, EditAnywhere, Category = LagCompensation)
	int32 MaxBufferedRewindHistoryTimeMS = 1000;
	
};

USTRUCT(meta=(ShowOnlyInnerProperties))
struct FJoltNetworkPredictionDevHUDItem
{
	GENERATED_BODY();

	UPROPERTY(config, EditAnywhere, Category = DevHUD)
	FString DisplayName;

	UPROPERTY(config, EditAnywhere, Category = DevHUD)
	FString ExecCommand;

	// Return to to level HUD menu after selecting this
	UPROPERTY(config, EditAnywhere, Category = DevHUD)
	bool bAutoBack = true;

	// only works in PIE
	UPROPERTY(config, EditAnywhere, Category = DevHUD)
	bool bRequirePIE = false;

	// only works in non PIE
	UPROPERTY(config, EditAnywhere, Category = DevHUD)
	bool bRequireNotPIE = false;
};

USTRUCT(meta=(ShowOnlyInnerProperties))
struct FJoltNetworkPredictionDevHUD
{
	GENERATED_BODY()

	UPROPERTY(config, EditAnywhere, Category = DevHUD)
	FString HUDName;

	UPROPERTY(config, EditAnywhere, Category = DevHUD, meta=(ShowOnlyInnerProperties))
	TArray<FJoltNetworkPredictionDevHUDItem> Items;

	// only works in PIE
	UPROPERTY(config, EditAnywhere, Category = DevHUD)
	bool bRequirePIE = false;

	// only works in non PIE
	UPROPERTY(config, EditAnywhere, Category = DevHUD)
	bool bRequireNotPIE = false;
};


UCLASS(config=NetworkPrediction, defaultconfig, meta=(DisplayName="Jolt Network Prediction"))
class UJoltNetworkPredictionSettingsObject : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(config, EditAnywhere, Category = "Network Prediction", meta=(ShowOnlyInnerProperties))
	FJoltNetworkPredictionSettings Settings;

	UPROPERTY(config, EditAnywhere, Category = DevHUD, meta=(ShowOnlyInnerProperties))
	TArray<FJoltNetworkPredictionDevHUD> DevHUDs;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

