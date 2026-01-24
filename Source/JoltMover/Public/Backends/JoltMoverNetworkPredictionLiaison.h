// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Backends/JoltMoverBackendLiaison.h"
#include "JoltNetworkPredictionComponent.h"
#include "JoltNetworkPredictionSimulation.h"
#include "JoltNetworkPredictionTickState.h"
#include "JoltMovementMode.h"
#include "JoltMoverTypes.h"

#include "JoltMoverNetworkPredictionLiaison.generated.h"

#define UE_API JOLTMOVER_API

class UJoltMoverComponent;


using KinematicMoverStateTypes = TJoltNetworkPredictionStateTypes<FJoltMoverInputCmdContext, FJoltMoverSyncState, FJoltMoverAuxStateContext>;

/**
 * MoverNetworkPredictionLiaisonComponent: this component acts as a middleman between an actor's Mover component and the Network Prediction plugin.
 * This class is set on a Mover component as the "back end".
 */
UCLASS(MinimalAPI)
class 
UJoltMoverNetworkPredictionLiaisonComponent : public UJoltNetworkPredictionComponent, public IJoltMoverBackendLiaisonInterface
{
	GENERATED_BODY()

public:
	// Begin NP Driver interface
	// Get latest local input prior to simulation step. Called by Network Prediction system on owner's instance (autonomous or authority).
	UE_API void ProduceInput(const int32 DeltaTimeMS, FJoltMoverInputCmdContext* Cmd);

	// Restore a previous frame prior to resimulating. Called by Network Prediction system.
	UE_API void RestoreFrame(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState);
	
	// Restore a previous frame prior to resimulating. Called by Network Prediction system.
	UE_API void RestorePhysicsFrame(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState);

	// Take output for simulation. Called by Network Prediction system.
	UE_API void FinalizeFrame(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState);

	// Take output for smoothing. Called by Network Prediction system.
	UE_API void FinalizeSmoothingFrame(const FJoltMoverSyncState* SyncState, const FJoltMoverAuxStateContext* AuxState);

	// Seed initial values based on component's state. Called by Network Prediction system.
	UE_API void InitializeSimulationState(FJoltMoverSyncState* OutSync, FJoltMoverAuxStateContext* OutAux);

	// Primary movement simulation update. Given an starting state and timestep, produce a new state. Called by Network Prediction system.
	UE_API void SimulationTick(const FJoltNetSimTimeStep& TimeStep, const TJoltNetSimInput<KinematicMoverStateTypes>& SimInput, const TJoltNetSimOutput<KinematicMoverStateTypes>& SimOutput);
	UE_API void PostPhysicsTick(const FJoltNetSimTimeStep& TimeStep, const TJoltNetSimInput<KinematicMoverStateTypes>& SimInput, const TJoltNetSimOutput<KinematicMoverStateTypes>& SimOutput);
	// End NP Driver interface

	// IJoltMoverBackendLiaisonInterface
	UE_API virtual double GetCurrentSimTimeMs() override;
	UE_API virtual int32 GetCurrentSimFrame() override;
	UE_API virtual bool ReadPendingSyncState(OUT FJoltMoverSyncState& OutSyncState) override;
	UE_API virtual bool WritePendingSyncState(const FJoltMoverSyncState& SyncStateToWrite) override;
	UE_API virtual bool ReadPresentationSyncState(OUT FJoltMoverSyncState& OutSyncState) override;
	UE_API virtual bool WritePresentationSyncState(const FJoltMoverSyncState& SyncStateToWrite) override;
	UE_API virtual bool ReadPrevPresentationSyncState(FJoltMoverSyncState& OutSyncState) override;
	UE_API virtual bool WritePrevPresentationSyncState(const FJoltMoverSyncState& SyncStateToWrite) override;
#if WITH_EDITOR
	UE_API virtual EDataValidationResult ValidateData(FDataValidationContext& Context, const UJoltMoverComponent& ValidationMoverComp) const override;
#endif
	// End IJoltMoverBackendLiaisonInterface

	UE_API virtual void BeginPlay() override;

	// UObject interface
	UE_API void InitializeComponent() override;
	UE_API void UninitializeComponent() override;
	UE_API void OnRegister() override;
	UE_API void RegisterComponentTickFunctions(bool bRegister) override;
	// End UObject interface

	// UJoltNetworkPredictionComponent interface
	UE_API virtual void InitializeNetworkPredictionProxy() override;
	// End UJoltNetworkPredictionComponent interface


public:
	UE_API UJoltMoverNetworkPredictionLiaisonComponent();

protected:
	TObjectPtr<UJoltMoverComponent> MoverComp;	// the component that we're in charge of driving
	FJoltMoverSyncState* StartingOutSync;
	FJoltMoverAuxStateContext* StartingOutAux;
};

#undef UE_API
