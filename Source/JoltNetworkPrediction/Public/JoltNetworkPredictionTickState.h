// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "JoltNetworkPredictionBuffer.h"
#include "JoltNetworkPredictionDeltaSerializationData.h"


// TimeStep info that is passed into SimulationTick
struct FJoltNetSimTimeStep
{
	// The delta time step for this tick
	int32 StepMS;

	// How much simulation time has ran up until this point. This is "Server time", e.g, everyone agrees on this time and it can
	// be used for timers/cooldowns etc in the simulation code. It can be stored in sync/aux state and reconciles pred vs authority.
	// This will be 0 the first time ::SimulationTick runs (globally for fix tick, local tick and per-sim for remote independent sims)
	int32 TotalSimulationTime;

	// The Simulation Frame number we are computing in this tick, E.g, the output frame.
	// This is the global, everyone agrees-upon frame number. E.g, the "server frame" number.	
	// This will be 1 the first time ::SimulationTick runs. (0 is the starting input and is not generated in a Tick)
	// (globally for fix tick, local tick and per-sim for remote independent sims)
	int32 Frame;

	// Lag compensation Time, This is the simulation time the client simulated proxies were rendered
	// sent by the client along with its simulations input commands and used by both client and server.
	// NOTE : for client this Lag Compensation Time and time when enemies are rendered are equal, when not resimulating.
	// Server Clamps the total duration it rewinds enemies based on Lag Compensation project setting.
	// however client should not clamp it (max is buffer duration time) when resimulating.
	// to ensure we get same targeting results server did during re-simulations.
	bool IsResimulating = false;
	
	float InterpolationTimeMS = 0.f;

	FJoltNetSimTimeStep(int32 InStepMS, int32 InTotalSimulationTime, int32 InFrame)
		: StepMS(InStepMS)
		, TotalSimulationTime(InTotalSimulationTime)
		, Frame(InFrame)
	{
	}
};

// Data that is needed to tick the internal ticking services but is not passed to the user code
struct FJoltServiceTimeStep
{
	// The local frame number, this is what should be used when mapping to local frame buffers for storage
	int32 LocalInputFrame;
	int32 LocalOutputFrame;

	// Ending total sim time, needed for Cue dispatching
	int32 EndTotalSimulationTime;
};


//Delta Serialization Acked Frames



// ---------------------------------------------------------------------------
// (Global) Tick state for fixed tick services
//
// Notes about Fix Ticking in NetworkPrediction:
//	1. FixedTick mode will accumulate real time and run 0-N Sim frames per engine frame.
//		a. Since NP uses int32 ms and the engine will use float DeltaTimeSeconds, NP will slowly lose time compared to the rest of the engine
//
//
// ---------------------------------------------------------------------------
struct FJoltFixedTickState
{
	// FixedStepMS that simulations should use
	int32 FixedStepMS = 33;

	// Realtime steps. That is, one of these = one FixedStepMS in simulation time.
	// This means sim time ticks slightly slower than real time.
	// This is seemingly the best choice
	float FixedStepRealTimeMS = 1/30.f;

	// Next frame to be ticked (used as input to generated PendingFrame+1)
	int32 PendingFrame=0;

	// Latest confirmed local frame number. Anything at or before this frame is "set in stone"
	int32 ConfirmedFrame=INDEX_NONE;
	
	// Maps ForwardPredicted authority frames to local frame.
	// E.g, server says "I processed your frame 1 on my frame 101" client calcs Offset as 100.
	// "LocalFrame" = "ServerFrame" - Offset. 
	int32 Offset=0;

	int32 LastOffset = 0;

	// Accumulates raw delta time into our fixed steps
	float UnspentTimeMS = 0.f;

	struct FInterpolationState
	{
		float AccumulatedTimeMS = 0.f; // accumulated real time
		int32 LatestRecvFrameAP = INDEX_NONE;	// Latest server frame we received, set by the AP
		int32 LatestRecvFrameSP = INDEX_NONE;	// Latest server frame we received, set by the SP
		int32 ToFrame = INDEX_NONE;	// Where we are interpolating to (ToFrame-1 -> ToFrame. Both should be valid at all times for anyone interpolating)
		float PCT = 0.f;

		int32 InterpolatedTimeMS = 0;
	};

	FInterpolationState Interpolation;

	struct FTimeDilationState
	{
		float TimeDilation = 1.f; 
		float FixedStepDilatedTimeMS = 1/30.f;
	};

	FTimeDilationState TimeDilationState;

	FJoltNetSimTimeStep GetNextTimeStep() const
	{
		return FJoltNetSimTimeStep{FixedStepMS, GetTotalSimTimeMS(),  PendingFrame+1 + Offset};
	}

	FJoltServiceTimeStep GetNextServiceTimeStep() const
	{
		return FJoltServiceTimeStep{PendingFrame, PendingFrame+1,(PendingFrame+Offset+1) * FixedStepMS};
	}

	int32 GetTotalSimTimeMS() const
	{
		return (PendingFrame+Offset) * FixedStepMS; 
	}

	//Delta Serialization
	// ToDo : Get these out of tick state and make them variables in the manager.
	// and bind to them appropriate NetSend And NetRcv functions
	
	//Server Only Data
	FJoltServerAckedFrames ServerAckedFrames;
	// Client Only Data
	FJoltAckedFrames LocalAckedFrames;
};

// Variable tick state tracking for independent tick services
struct FJoltVariableTickState
{
	struct FFrame
	{
		int32 DeltaMS=0;
		int32 TotalMS=0;
	};

	TJoltNetworkPredictionBuffer<FFrame> Frames;

	FJoltVariableTickState() : Frames(64) { }

	int32 PendingFrame = 0;
	int32 ConfirmedFrame = INDEX_NONE;

	float UnspentTimeMS = 0.f;

	struct FInterpolationState
	{
		float fTimeMS = 0.f;
		int32 LatestRecvTimeMS = 0;
	};

	FInterpolationState Interpolation;

	FJoltNetSimTimeStep GetNextTimeStep() const
	{
		return GetNextTimeStep(Frames[PendingFrame]);
	}

	FJoltNetSimTimeStep GetNextTimeStep(const FFrame& PendingFrameData) const
	{
		return FJoltNetSimTimeStep{PendingFrameData.DeltaMS, PendingFrameData.TotalMS, PendingFrame+1};
	}

	FJoltServiceTimeStep GetNextServiceTimeStep(const FFrame& PendingFrameData) const
	{
		return FJoltServiceTimeStep{PendingFrame, PendingFrame+1, PendingFrameData.TotalMS + PendingFrameData.DeltaMS};
	}
	
};
