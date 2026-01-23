// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVisualDebugger/JoltMoverCVDRuntimeTrace.h"

#if WITH_CHAOS_VISUAL_DEBUGGER

#include "JoltMoverCVDDataWrappers.h"
#include "JoltMoverSimulationTypes.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "ChaosVisualDebugger/ChaosVisualDebuggerTrace.h"
#include "JoltMoverComponent.h"
#include "Chaos/PhysicsObjectInterface.h"
#include "Chaos/PhysicsObjectInternalInterface.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "ChaosVisualDebugger/ChaosVDTraceMacros.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Interfaces/IPhysicsComponent.h"
#include "Engine/World.h"

namespace UE::JoltMoverUtils
{

CVD_DEFINE_OPTIONAL_DATA_CHANNEL(JoltMoverNetworkedData, EChaosVDDataChannelInitializationFlags::CanChangeEnabledState)
CVD_DEFINE_OPTIONAL_DATA_CHANNEL(JoltMoverLocalSimData, EChaosVDDataChannelInitializationFlags::CanChangeEnabledState)

// FSkipObjectRefsMemoryWriter and FSkipObjectRefsMemoryReader are a 
// workaround for serializing JoltMover info structs with object references in them, such as the JoltMover base.
// It currently skips object references altogether, except if those are UScriptStruct objects, which it serializes
// as the script struct name, hoping the type exists on the receiving end. This allows us to pass FInstancedStruct
// as member properties of JoltMover info structs. It is not backwads compatible though, and may cause crashes if the
// underlying types have changed.
// Ultimately we need to implement better backwards compatibility, possiblly using FPropertyBags. When the referenced object
// is an actor with a primitive component though, we might want to attempt to translate the object reference to a particle ID and resolve that on the CVD side
// by linking to the corresponding CVD particle, if found.
class FSkipObjectRefsMemoryWriter : public FMemoryWriter
{
public:
	FSkipObjectRefsMemoryWriter(TArray<uint8, TSizedDefaultAllocator<32>>& InBytes, bool bIsPersistent = false, bool bSetOffset = false, const FName InArchiveName = NAME_None)
		: FMemoryWriter(InBytes, bIsPersistent, bSetOffset, InArchiveName)
	{
	}

	virtual FArchive& operator<<(struct FObjectPtr& Value) override
	{
		if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Value.Get()))
		{
			// The FullName of the script struct will be something like "ScriptStruct /Script/JoltMover.FCharacterDefaultInputs"
			FString FullStructName = ScriptStruct->GetFullName(nullptr);
			// We don't need to save the first part since we only ever save UScriptStructs (C++ structs)
			FString StructName = FullStructName.RightChop(13); // So we chop the "ScriptStruct " part (hence 13 characters)
			*this << StructName;
		}
		return *this;
	}

	virtual FArchive& operator<<(struct FWeakObjectPtr& Value) override
	{
		return *this;
	}

	virtual FArchive& operator<<(class UObject*& Res) override
	{
		if (Res)
		{
			if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Res))
			{
				// The FullName of the script struct will be something like "ScriptStruct /Script/JoltMover.FCharacterDefaultInputs"
				FString FullStructName = ScriptStruct->GetFullName(nullptr);
				// We don't need to save the first part since we only ever save UScriptStructs (C++ structs)
				FString StructName = FullStructName.RightChop(13); // So we chop the "ScriptStruct " part (hence 13 characters)
				*this << StructName;
			}
		}
		return *this;
	}
};
class FSkipObjectRefsMemoryReader : public FMemoryReader
{
public:
	FSkipObjectRefsMemoryReader(const TArray<uint8, TSizedDefaultAllocator<32>>& InBytes, bool bIsPersistent = false)
		: FMemoryReader(InBytes, bIsPersistent)
	{
	}

	virtual FArchive& operator<<(struct FObjectPtr& Value)
	{
		return *this;
	}

	virtual FArchive& operator<<(struct FWeakObjectPtr& Value)
	{
		return *this;
	}

	virtual FArchive& operator<<(class UObject*& Res) override
	{
		FString StructName;
		*this << StructName;
		Res = Cast<UScriptStruct>(FindObject<UStruct>(nullptr, *StructName));
		return *this;
	}
};

void FJoltMoverCVDRuntimeTrace::UnwrapSimData(const FJoltMoverCVDSimDataWrapper& InSimDataWrapper, TSharedPtr<FJoltMoverInputCmdContext>& OutInputCmd, TSharedPtr<FJoltMoverSyncState>& OutSyncState, TSharedPtr<FJoltMoverDataCollection>& OutLocalSimData)
{
	// Consider using Chaos::VisualDebugger::FChaosVDMemoryReader

	// Input Cmd
	{
		// Deserialize into a new struct allocated dynamically
		FMemoryReader ArReader(InSimDataWrapper.InputCmdBytes, true);
		OutInputCmd = MakeShared<FJoltMoverInputCmdContext>();
		FJoltMoverInputCmdContext::StaticStruct()->SerializeBin(ArReader, OutInputCmd.Get());
		// Input cmd's Collection of custom structs
		FSkipObjectRefsMemoryReader ArInputCollectionReader(InSimDataWrapper.InputJoltMoverDataCollectionBytes, true);
		OutInputCmd->Collection.SerializeDebugData(ArInputCollectionReader);
	}

	// Sync State
	{
		// Deserialize into a new struct allocated dynamically
		FMemoryReader ArReader(InSimDataWrapper.SyncStateBytes, true);
		OutSyncState = MakeShared<FJoltMoverSyncState>();
		FJoltMoverSyncState::StaticStruct()->SerializeBin(ArReader, OutSyncState.Get());
		// Sync State's Collection of custom structs
		FSkipObjectRefsMemoryReader ArSyncStateCollectionReader(InSimDataWrapper.SyncStateDataCollectionBytes, true);
		OutSyncState->Collection.SerializeDebugData(ArSyncStateCollectionReader);
	}

	{
		FSkipObjectRefsMemoryReader ArReader(InSimDataWrapper.LocalSimDataBytes, true);
		OutLocalSimData = MakeShared<FJoltMoverDataCollection>();
		OutLocalSimData->SerializeDebugData(ArReader);
	}
}

void FJoltMoverCVDRuntimeTrace::WrapSimData(uint32 SolverID, uint32 ParticleID, const FJoltMoverInputCmdContext& InInputCmd, const FJoltMoverSyncState& InSyncState, const FJoltMoverDataCollection* LocalSimData, FJoltMoverCVDSimDataWrapper& OutSimDataWrapper)
{
	OutSimDataWrapper.SolverID = SolverID;
	OutSimDataWrapper.ParticleID = ParticleID;

	// Consider using Chaos::VisualDebugger::FChaosVDMemoryWriter

	// Input Cmd
	{
		FMemoryWriter ArWriter(OutSimDataWrapper.InputCmdBytes, true);
		// This is not version friendly, we need to instead use SerializeTagParticles. Slower and Sergio is working on a faster version, but it's not available yet.
		InInputCmd.StaticStruct()->SerializeBin(ArWriter, &const_cast<FJoltMoverInputCmdContext&>(InInputCmd));
	}
	// Input cmd's Collection of custom structs
	{
		FSkipObjectRefsMemoryWriter ArWriter(OutSimDataWrapper.InputJoltMoverDataCollectionBytes, true);
		const_cast<FJoltMoverInputCmdContext&>(InInputCmd).Collection.SerializeDebugData(ArWriter);
	}

	// Sync State
	{
		FMemoryWriter ArWriter(OutSimDataWrapper.SyncStateBytes, true);
		// This is not version friendly, we need to instead use SerializeTagParticles. Slower and Sergio is working on a faster version, but it's not available yet.
		InSyncState.StaticStruct()->SerializeBin(ArWriter, &const_cast<FJoltMoverSyncState&>(InSyncState));
	}
	{
		FSkipObjectRefsMemoryWriter ArWriter(OutSimDataWrapper.SyncStateDataCollectionBytes, true);
		const_cast<FJoltMoverSyncState&>(InSyncState).Collection.SerializeDebugData(ArWriter);
	}

	// Local sim data (catch all other structs we want to record)
	{
		FSkipObjectRefsMemoryWriter ArWriter(OutSimDataWrapper.LocalSimDataBytes, true);
		if (LocalSimData)
		{
			(const_cast<FJoltMoverDataCollection*>(LocalSimData))->SerializeDebugData(ArWriter);
		}
		else
		{
			FJoltMoverDataCollection EmptyDataCollection;
			EmptyDataCollection.SerializeDebugData(ArWriter);
		}
	}
}

USTRUCT()
struct FJoltMoverMergeableDataCollection : FJoltMoverDataCollection
{
	void AppendShallow(const FJoltMoverDataCollection& Other)
	{
		const FJoltMoverMergeableDataCollection& MergeableOther = reinterpret_cast<const FJoltMoverMergeableDataCollection&>(Other);
		for (int i = 0; i < MergeableOther.DataArray.Num(); ++i)
		{
			if (MergeableOther.DataArray[i].IsValid())
			{
				DataArray.Add(MergeableOther.DataArray[i]);
			}
		}
	}
};

void CombineDataCollections(const NamedDataCollections& DataCollections, FJoltMoverDataCollection& OutDataCollection)
{
	FJoltMoverMergeableDataCollection& OutMergeableDataCollection = reinterpret_cast<FJoltMoverMergeableDataCollection&>(OutDataCollection);
	for (const TPair<FName, const FJoltMoverDataCollection*>& DataCollectionEntry : DataCollections)
	{
		if (const FJoltMoverDataCollection* DataCollectionToAppend = DataCollectionEntry.Value)
		{
			OutMergeableDataCollection.AppendShallow(*DataCollectionToAppend);
		}
	}
}

void FJoltMoverCVDRuntimeTrace::TraceJoltMoverData(UJoltMoverComponent* JoltMoverComponent, const FJoltMoverInputCmdContext* InputCmd, const FJoltMoverSyncState* SyncState, const NamedDataCollections* LocalSimDataCollections /*= nullptr*/)
{
	if (!FChaosVisualDebuggerTrace::IsTracing())
	{
		return;
	}

	if (!CVDDC_JoltMoverNetworkedData->IsChannelEnabled())
	{
		return;
	}

	if (!InputCmd || !SyncState)
	{
		return;
	}

	if (UWorld* World = JoltMoverComponent->GetWorld())
	{
		int32 ParticleID = INDEX_NONE;
		if (const IPhysicsComponent* PhysicsComponent = Cast<IPhysicsComponent>(JoltMoverComponent->GetUpdatedComponent()))
		{
			Chaos::FReadPhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetRead();
			const Chaos::FPhysicsObject* PhysicsObject = PhysicsComponent ? PhysicsComponent->GetPhysicsObjectById(0) : nullptr; // get the root physics object
			const Chaos::FGeometryParticleHandle* ParticleHandle = PhysicsObject? Interface.GetParticle(PhysicsObject) : nullptr;
			ParticleID = ParticleHandle ? ParticleHandle->UniqueIdx().Idx : INDEX_NONE;
		}

		int32 SolverID = CVD_TRACE_GET_SOLVER_ID_FROM_WORLD(World);
		const FJoltMoverDataCollection* RecordedLocalSimData = nullptr;
		FJoltMoverDataCollection MergedDataCollection;
		if (CVDDC_JoltMoverLocalSimData->IsChannelEnabled() && LocalSimDataCollections)
		{
			// LocalSimState could add a lot of extra bytes, especially without some sort of delta serialization, so it is only optionally recorded
			CombineDataCollections(*LocalSimDataCollections, MergedDataCollection);
			RecordedLocalSimData = &MergedDataCollection;
		}
		TraceJoltMoverDataPrivate(SolverID, ParticleID, InputCmd, SyncState, RecordedLocalSimData);
	}
}

void FJoltMoverCVDRuntimeTrace::TraceJoltMoverData(uint32 SolverID, uint32 ParticleID, const FJoltMoverInputCmdContext* InputCmd, const FJoltMoverSyncState* SyncState, const NamedDataCollections* LocalSimDataCollections /*= nullptr*/)
{
	if (!FChaosVisualDebuggerTrace::IsTracing())
	{
		return;
	}

	if (!CVDDC_JoltMoverNetworkedData->IsChannelEnabled())
	{
		return;
	}

	if (!InputCmd || !SyncState)
	{
		return;
	}
		
	const FJoltMoverDataCollection* RecordedLocalSimData = nullptr;
	FJoltMoverDataCollection MergedDataCollection;
	if (CVDDC_JoltMoverLocalSimData->IsChannelEnabled() && LocalSimDataCollections)
	{
		// LocalSimState could add a lot of extra bytes, especially without some sort of delta serialization, so it is only optionally recorded
		CombineDataCollections(*LocalSimDataCollections, MergedDataCollection);
		RecordedLocalSimData = &MergedDataCollection;
	}
	TraceJoltMoverDataPrivate(SolverID, ParticleID, InputCmd, SyncState, RecordedLocalSimData);
}

void FJoltMoverCVDRuntimeTrace::TraceJoltMoverDataPrivate(uint32 SolverID, uint32 ParticleID, const FJoltMoverInputCmdContext* InputCmd, const FJoltMoverSyncState* SyncState, const FJoltMoverDataCollection* LocalSimData /*= nullptr*/)
{
	FJoltMoverCVDSimDataWrapper SimDataWrapper;
	WrapSimData(SolverID, ParticleID, *InputCmd, *SyncState, LocalSimData, SimDataWrapper);
	SimDataWrapper.MarkAsValid();

	FChaosVDScopedTLSBufferAccessor TLSDataBuffer;
	Chaos::VisualDebugger::WriteDataToBuffer(TLSDataBuffer.BufferRef, SimDataWrapper);

	FChaosVisualDebuggerTrace::TraceBinaryData(TLSDataBuffer.BufferRef, FJoltMoverCVDSimDataWrapper::WrapperTypeName);
}

} // namespace UE::JoltMoverUtils

#endif // WITH_CHAOS_VISUAL_DEBUGGER

