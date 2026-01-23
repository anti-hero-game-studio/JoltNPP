// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"
#include "JoltMoverSimulationTypes.h"
#include "JoltMoverBackendLiaison.generated.h"

class FDataValidationContext;
class UJoltMoverComponent;

/**
 * MoverBackendLiaisonInterface: any object or system wanting to be the driver of Mover actors must implement this. The intent is to act as a
 * middleman between the Mover actor and the system that drives it, such as the Network Prediction plugin.
 * In practice, objects implementing this interface should be some kind of UActorComponent. The Mover actor instantiates its backend liaison
 * when initialized, then relies on the liaison to call various functions as the simulation progresses. See @MoverComponent.
 */
UINTERFACE(MinimalAPI)
class UJoltMoverBackendLiaisonInterface : public UInterface
{
	GENERATED_BODY()
};

class IJoltMoverBackendLiaisonInterface
{
	GENERATED_BODY()

public:
	virtual double GetCurrentSimTimeMs() = 0;
	virtual int32 GetCurrentSimFrame() = 0;

	// Whether this backend will simulate movement asynchronously
	virtual bool IsAsync() const { return false; }

	// How much delay to apply to scheduled events. This is important for networked events, and should be greater than the RTT to ensure the event will be executed on all end points at the same frame.
	virtual float GetEventSchedulingMinDelaySeconds() const {return 0.3f;}

	// Pending State: the simulation state currently being authored
	virtual bool ReadPendingSyncState(OUT FJoltMoverSyncState& OutSyncState) { return false; }
	virtual bool WritePendingSyncState(const FJoltMoverSyncState& SyncStateToWrite) { return false; }
	
	// Presentation State: the most recent presentation state, possibly the result of interpolation or smoothing. Writing to it does not affect the official simulation record.
	virtual bool ReadPresentationSyncState(OUT FJoltMoverSyncState& OutSyncState) { return false; }
	virtual bool WritePresentationSyncState(const FJoltMoverSyncState& SyncStateToWrite) { return false; }

	// Previous Presentation State: the state that our optional smoothing process is moving away from, towards a more recent state. Writing to it does not affect the official simulation record.
	virtual bool ReadPrevPresentationSyncState(OUT FJoltMoverSyncState& OutSyncState) { return false; }
	virtual bool WritePrevPresentationSyncState(const FJoltMoverSyncState& SyncStateToWrite) { return false; }

#if WITH_EDITOR
	virtual EDataValidationResult ValidateData(FDataValidationContext& Context, const UJoltMoverComponent& ValidationMoverComp) const { return EDataValidationResult::Valid; }
#endif // WITH_EDITOR

};