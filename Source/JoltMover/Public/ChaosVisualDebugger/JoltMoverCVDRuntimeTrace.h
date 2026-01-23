// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "HAL/Platform.h"
#include "Templates/SharedPointer.h"

#include "ChaosVisualDebugger/ChaosVDOptionalDataChannel.h"

struct FJoltMoverCVDSimDataWrapper;
struct FJoltMoverInputCmdContext;
struct FJoltMoverSyncState;
struct FJoltMoverDataCollection;
class UJoltMoverComponent;


namespace UE::JoltMoverUtils
{

typedef TArray<TPair<FName, const FJoltMoverDataCollection*>> NamedDataCollections;

#if WITH_CHAOS_VISUAL_DEBUGGER

/** Utility functions used to trace JoltMover data into the Chaos Visual Debugger */
class FJoltMoverCVDRuntimeTrace
{
public:
	JOLTMOVER_API static void TraceJoltMoverData(UJoltMoverComponent* JoltMoverComponent, const FJoltMoverInputCmdContext* InputCmd, const FJoltMoverSyncState* SyncState, const NamedDataCollections* LocalSimDataCollections = nullptr);
	JOLTMOVER_API static void TraceJoltMoverData(uint32 SolverID, uint32 ParticleID, const FJoltMoverInputCmdContext* InputCmd, const FJoltMoverSyncState* SyncState, const NamedDataCollections* LocalSimDataCollections = nullptr);

	JOLTMOVER_API static void UnwrapSimData(const FJoltMoverCVDSimDataWrapper& InSimDataWrapper, TSharedPtr<FJoltMoverInputCmdContext>& OutInputCmd, TSharedPtr<FJoltMoverSyncState>& OutSyncState, TSharedPtr<FJoltMoverDataCollection>& OutLocalSimState);
	JOLTMOVER_API static void WrapSimData(uint32 SolverID, uint32 ParticleID, const FJoltMoverInputCmdContext& InInputCmd, const FJoltMoverSyncState& InSyncState, const FJoltMoverDataCollection* LocalSimState, FJoltMoverCVDSimDataWrapper& OutSimDataWrapper);

private:
	static void TraceJoltMoverDataPrivate(uint32 SolverID, uint32 ParticleID, const FJoltMoverInputCmdContext* InputCmd, const FJoltMoverSyncState* SyncState, const FJoltMoverDataCollection* LocalSimState = nullptr);
};

// This is all joltMover data that is networked, either input command (client to server) or sync state (server to client)
CVD_DECLARE_OPTIONAL_DATA_CHANNEL_EXTERN(JoltMoverNetworkedData, JOLTMOVER_API)
// This is additional joltMover data, local to each end point's simulation
CVD_DECLARE_OPTIONAL_DATA_CHANNEL_EXTERN(JoltMoverLocalSimData, JOLTMOVER_API)

#else

// Noop implementation in case this is compiled without Chaos Visual Debugger support (e.g. shipping)
class FJoltMoverCVDRuntimeTrace
{
public:
	static void TraceJoltMoverData(UJoltMoverComponent* JoltMoverComponent, const FJoltMoverInputCmdContext* InputCmd, const FJoltMoverSyncState* SyncState, const NamedDataCollections* FJoltMoverLocalSimState = nullptr) {}
	static void TraceJoltMoverData(uint32 SolverID, uint32 ParticleID, const FJoltMoverInputCmdContext* InputCmd, const FJoltMoverSyncState* SyncState, const NamedDataCollections* FJoltMoverLocalSimState = nullptr) {}
	static void UnwrapSimData(const FJoltMoverCVDSimDataWrapper& InSimDataWrapper, TSharedPtr<FJoltMoverInputCmdContext>& OutInputCmd, TSharedPtr<FJoltMoverSyncState>& OutSyncState, TSharedPtr<FJoltMoverDataCollection>& FJoltMoverLocalSimState) {}
	static void WrapSimData(uint32 SolverID, uint32 ParticleID, const FJoltMoverInputCmdContext& InInputCmd, const FJoltMoverSyncState& InSyncState, const FJoltMoverDataCollection* FJoltMoverLocalSimState, FJoltMoverCVDSimDataWrapper& OutSimDataWrapper) {}
};

#endif // WITH_CHAOS_VISUAL_DEBUGGER

} // namespace UE::JoltMoverUtils