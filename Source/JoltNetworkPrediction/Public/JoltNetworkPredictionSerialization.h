// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Net/UnrealNetwork.h" // For MakeRelative
#include "JoltNetworkPredictionCVars.h"
#include "JoltNetworkPredictionReplicationProxy.h"
#include "JoltNetworkPredictionTickState.h"
#include "JoltNetworkPredictionTrace.h"
#include "Engine/PackageMapClient.h"
#include "Net/NetPing.h"
#include "Services/JoltNetworkPredictionInstanceData.h"
#include "UObject/CoreNet.h"

#define JOLTNETSIM_ENABLE_CHECKSUMS !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#if JOLTNETSIM_ENABLE_CHECKSUMS 
#define JOLTNETSIM_CHECKSUM(Ser) SerializeChecksum(Ser,0xA186A384, false);
#else
#define JOLTNETSIM_CHECKSUM
#endif

#ifndef JOLTNETSIM_NETCONSTANT_NUM_BITS_FRAME
#define JOLTNETSIM_NETCONSTANT_NUM_BITS_FRAME 8	// Allows you to override this setting via UBT, but access via cleaner FActorMotionNetworkingConstants::NUM_BITS_FRAME
#endif

namespace NetworkPredictionCVars
{
	JOLTNETSIM_DEVCVAR_SHIPCONST_INT(ForceSendDefaultInputCommands, 0, "j.np.ForceSendDefaultInputCommands", "While enabled on a client, it will send default input cmds to the server, rather than the locally-produced input. This is a way to introduce de-syncs/rollbacks for debugging.");
	JOLTNETSIM_DEVCVAR_SHIPCONST_INT(ToggleDeltaSerialize, 1, "j.np.ToggleDeltaSerialize", "Toggle Delta Serialization , 1 : Enabled , 0 : Disabled");
}

struct FJoltNetworkPredictionSerialization
{
	// How many bits we use to NetSerialize Frame numbers. This is only relevant AP Client <--> Server communication.
	// Frames are stored locally as 32 bit integers, but we use a smaller number of bits to NetSerialize.
	// The system internally guards from Frame numbers diverging. E.g, the client will not generate new frames if the
	// last serialization frame would be pushed out of the buffer. Server does not generate frames without input from client.
	enum { NUM_BITS_FRAME = JOLTNETSIM_NETCONSTANT_NUM_BITS_FRAME };

	// Abs max value we encode into the bit writer
	enum { MAX_FRAME_WRITE = 1 << NUM_BITS_FRAME };

	// This is the threshold at which we would wrap around and incorrectly assign a frame on the receiving side.
	// E.g, If there are FRAME_ERROR_THRESHOLD frames that do not make it across from sender->receiver, the
	// receiver will have incorrect local values. With 8 bits, this works out to be 128 frames or about 2 seconds at 60fps.
	enum { FRAME_ERROR_THRESHOLD = MAX_FRAME_WRITE / 2};

	// Helper to serialize an int32 frame as 8 bits. Returns the unpacked value (this will be same as input in the save path)
	static int32 SerializeFrame(FArchive& Ar, int32 Frame, int32 RelativeFrame)
	{
		if (Ar.IsSaving())
		{
			((FNetBitWriter&)Ar).WriteIntWrapped( Frame, MAX_FRAME_WRITE );
			return Frame;
		}

		return MakeRelative(((FNetBitReader&)Ar).ReadInt( MAX_FRAME_WRITE ), RelativeFrame, MAX_FRAME_WRITE );
	}

	// Disabled right now: this is causing issues with JIP
	static void WriteCompressedFrame(FArchive& Ar, int32 Frame)
	{
		Ar << Frame;

		//jnpCheckSlow(Ar.IsSaving());
		//((FNetBitWriter&)Ar).WriteIntWrapped( Frame, MAX_FRAME_WRITE );
	}

	// Disabled right now: this is causing issues with JIP
	static int32 ReadCompressedFrame(FArchive& Ar, int32 RelativeFrame)
	{
		int32 Frame = 0;
		Ar << Frame;
		return Frame;

		//const int32 SerializedInt = ((FNetBitReader&)Ar).ReadInt( MAX_FRAME_WRITE );
		//return MakeRelative(SerializedInt, RelativeFrame, MAX_FRAME_WRITE );
	}

	// For serializing timestamps
	static void SerializeTimeMS(FArchive& Ar, int32& TimestampMS)
	{
		// if this shows up in profiles, we may be able to do a MakeRelative scheme like frames
		Ar << TimestampMS;
	}

	// For serializing DeltaMS, expected to be small (< 1000)
	static void SerializeDeltaMS(FArchive& Ar, int32& DeltaTimeMS)
	{
		Ar.SerializeIntPacked((uint32&)DeltaTimeMS);
	}
};



// ---------------------------------------------------------------------------------------------------------------------------
//	AP Client -> Server replication
//
//	The Fixed/Independent ticking implementations are a more than trivially different so they are split into separate implementations.
//	Both currently send the last 'NumInputsPerSend' per serialization, determined from config settings. This could be improved with something more dynamic.
//
// ---------------------------------------------------------------------------------------------------------------------------

// ToDo: Kai : This class is Unused and Should Be cleaned up along with NetWorkPredictionProxy_Server and all original code related to input RPC
template<typename InModelDef>
class TFixedTickReplicator_Server
{
public:

	using ModelDef = InModelDef;
	using StateTypes = typename ModelDef::StateTypes;
	using InputType = typename StateTypes::InputType;

	static void SetNumInputsPerSend(int32 NumToSend) 
	{ 
		NumInputsPerSend = NumToSend; 
	}

	// ------------------------------------------------------------------------------------------------------------
	// TFixedTickReplicator_Server::NetRecv Server Receiving from AP client
	// ------------------------------------------------------------------------------------------------------------
	static void NetRecv(const FJoltNetSerializeParams& P, TJoltServerRecvData_Fixed<ModelDef>& ServerRecvData, TJoltModelDataStore<ModelDef>* DataStore, FJoltFixedTickState* TickState)
	{
		FArchive& Ar = P.Ar;
		int32 EndFrame = FJoltNetworkPredictionSerialization::ReadCompressedFrame(Ar, ServerRecvData.LastRecvFrame); // 1. StartFrame
		
		// -------------------------------------------------------------------------------------------- //
		
		const int32 StartFrame = EndFrame - NumInputsPerSend;
		const bool bIsStarvedForInput = ServerRecvData.LastRecvFrame <= ServerRecvData.LastConsumedFrame;
		
		for (int32 Frame=StartFrame; Frame < EndFrame; ++Frame)
		{
			if (Frame <= ServerRecvData.LastRecvFrame || Frame <= ServerRecvData.LastConsumedFrame)
			{
				EatCmd(P);
			}
			
			else
			{
				if (!bIsStarvedForInput)
				{
					for (int32 DroppedFrame = ServerRecvData.LastRecvFrame+1; DroppedFrame < Frame; ++DroppedFrame)
					{
						UE_JNP_TRACE_SYSTEM_FAULT("Gap in input stream detected on server. Client frames involved: LastConsumedFrame: %d LastRecvFrame: %d. DroppedFrame: %d", ServerRecvData.LastConsumedFrame, ServerRecvData.LastRecvFrame, DroppedFrame);
						if (DroppedFrame > 0)
						{
							// FixedTick can't skip frames like independent, so copy previous input
							ServerRecvData.InputBuffer[DroppedFrame] = ServerRecvData.InputBuffer[DroppedFrame-1];
						}
					}
				}
				else
				{
					//UE_JNP_TRACE_SYSTEM_FAULT("Recovering from input stream starvation on server. Advancing over dropped client frames [%d-%d]", ServerRecvData.LastConsumedFrame+1, StartFrame-1);
					//ServerRecvData.LastConsumedFrame = StartFrame-1;
				}

				jnpEnsure(Frame >= 0);

				FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(ServerRecvData.InputBuffer[Frame].Value, P); // 2. InputCmd

				ServerRecvData.LastRecvFrame = Frame;
				// Trace what we received
				const int32 ExpectedFrameDelay = ServerRecvData.LastRecvFrame - ServerRecvData.LastConsumedFrame;
				const int32 ExpectedConsumeFrame = TickState->PendingFrame + ExpectedFrameDelay - 1;
				UE_JNP_TRACE_NET_RECV(ExpectedConsumeFrame, ExpectedConsumeFrame * TickState->FixedStepMS);
				UE_JNP_TRACE_USER_STATE_INPUT(ModelDef, ServerRecvData.InputBuffer[Frame].Value.Get());
			}
		}
	}

	// ------------------------------------------------------------------------------------------------------------
	// TFixedTickReplicator_Server::NetSend AP Client sending to server
	// ------------------------------------------------------------------------------------------------------------
	static void NetSend(const FJoltNetSerializeParams& P, FJoltNetworkPredictionID ID, TJoltModelDataStore<ModelDef>* DataStore, FJoltFixedTickState* TickState)
	{
		FArchive& Ar = P.Ar;
		
		TJoltInstanceFrameState<ModelDef>* Frames = DataStore->Frames.Find(ID);
		jnpCheckSlow(Frames);

		FJoltNetworkPredictionSerialization::WriteCompressedFrame(Ar, TickState->PendingFrame); // 1. Client's PendingFrame number

		const int32 EndFrame = TickState->PendingFrame; // PendingFrame doesn't have an input written until right before it ticks, so don't send its contents
		const int32 StartFrame = EndFrame - NumInputsPerSend;
		
		for (int32 Frame = StartFrame; Frame < EndFrame; ++Frame)
		{
			if (Frame < 0)
			{
				EatCmd(P);
			}
			else
			{
				if (NetworkPredictionCVars::ForceSendDefaultInputCommands())
				{
					// for debugging, send blank default input instead of what we've produced locally
					static TJoltConditionalState<InputType> DefaultInputCmd;
					FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(DefaultInputCmd, P); 
				}
				else
				{
					FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(Frames->Buffer[Frame].InputCmd, P); // 2. InputCmd
				}
			}
		}
	}

	static void EatCmd(const FJoltNetSerializeParams& P)
	{
		TJoltConditionalState<InputType> Empty;
		FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(Empty, P);  // 2. InputCmd	
	}

private:
	inline static int32 NumInputsPerSend = 6;
};

template<typename InModelDef>
class TIndependentTickReplicator_Server
{
public:

	using ModelDef = InModelDef;

	using StateTypes = typename ModelDef::StateTypes;
	using InputType= typename StateTypes::InputType;

	static void SetNumInputsPerSend(int32 NumToSend) 
	{ 
		NumInputsPerSend=NumToSend; 
	}

	// ------------------------------------------------------------------------------------------------------------
	// TIndependentTickReplicator_Server::NetRecv AP Client sending to server
	// ------------------------------------------------------------------------------------------------------------
	static void NetRecv(const FJoltNetSerializeParams& P, TJoltServerRecvData_Independent<ModelDef>& ServerRecvData, TJoltModelDataStore<ModelDef>* DataStore)
	{
		FArchive& Ar = P.Ar;
		

		const int32 EndFrame = FJoltNetworkPredictionSerialization::ReadCompressedFrame(Ar, ServerRecvData.LastRecvFrame); // 1. StartFrame
		const int32 StartFrame = EndFrame - NumInputsPerSend;

		// Reset consumed frame if we detect a gap.
		// Note this could discard unprocessed commands we previously received (but didn't process) but handling this case doesn't seem necessary or practical
		if (ServerRecvData.LastConsumedFrame+1 < StartFrame)
		{
			ServerRecvData.LastConsumedFrame = StartFrame-1;
			ServerRecvData.LastRecvFrame = StartFrame-1;
		}
		
		int32 ExpectedTimeMS = ServerRecvData.TotalSimTimeMS; // SimTime we expect to process next command at
		for (int32 Frame=ServerRecvData.LastConsumedFrame+1; Frame >= 0 && Frame <= ServerRecvData.LastRecvFrame; ++Frame)
		{
			ExpectedTimeMS += ServerRecvData.InputBuffer[Frame].DeltaTimeMS;
		}

		for (int32 Frame=StartFrame; Frame < EndFrame; ++Frame)
		{
			if (Frame <= ServerRecvData.LastRecvFrame)
			{
				EatCmd(P);
			}
			else
			{
				jnpEnsure(Frame >= 0);

				for (int32 DroppedFrame = ServerRecvData.LastRecvFrame+1; DroppedFrame < Frame; ++DroppedFrame)
				{
					// FIXME: trace ID has to be better
					UE_JNP_TRACE_SYSTEM_FAULT("Gap in input stream detected on server. LastRecvFrame: %d. New Frame: %d", ServerRecvData.LastRecvFrame, DroppedFrame);
					ServerRecvData.InputBuffer[DroppedFrame].DeltaTimeMS = 0;
				}

				typename TJoltServerRecvData_Independent<ModelDef>::FFrame& RecvFrame = ServerRecvData.InputBuffer[Frame];

				FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(RecvFrame.InputCmd, P); // 2. InputCmd
				FJoltNetworkPredictionSerialization::SerializeDeltaMS(P.Ar, RecvFrame.DeltaTimeMS); // 3. DeltaTime

				// Trace what we received
				const int32 ExpectedFrameDelay = ServerRecvData.LastRecvFrame - ServerRecvData.LastConsumedFrame;
				const int32 ExpectedConsumeFrame = ServerRecvData.PendingFrame + ExpectedFrameDelay;
				
				jnpEnsure(ExpectedConsumeFrame >= 0);
				UE_JNP_TRACE_NET_RECV(ExpectedConsumeFrame, ExpectedTimeMS);
				UE_JNP_TRACE_USER_STATE_INPUT(ModelDef, ServerRecvData.InputBuffer[Frame].InputCmd.Get());

				// Advance
				ExpectedTimeMS += RecvFrame.DeltaTimeMS;
				ServerRecvData.LastRecvFrame = Frame;
			}
		}
	}

	// ------------------------------------------------------------------------------------------------------------
	// TIndependentTickReplicator_Server::NetRecv AP Client sending to server
	// ------------------------------------------------------------------------------------------------------------
	static void NetSend(const FJoltNetSerializeParams& P, FJoltNetworkPredictionID ID, TJoltModelDataStore<ModelDef>* DataStore, FJoltVariableTickState* TickState)
	{
		FArchive& Ar = P.Ar;
		

		TJoltInstanceFrameState<ModelDef>* Frames = DataStore->Frames.Find(ID);
		jnpCheckSlow(Frames);

		FJoltNetworkPredictionSerialization::WriteCompressedFrame(Ar, TickState->PendingFrame); // 1. Client's PendingFrame number

		const int32 EndFrame = TickState->PendingFrame; // PendingFrame doesn't have an input written until right before it ticks, so don't send its contents
		const int32 StartFrame = EndFrame - NumInputsPerSend;

		for (int32 Frame = StartFrame; Frame < EndFrame; ++Frame)
		{
			if (Frame < 0)
			{
				EatCmd(P);
			}
			else
			{
				if (NetworkPredictionCVars::ForceSendDefaultInputCommands())
				{
					// for debugging, send blank default input instead of what we've produced locally
					static TJoltConditionalState<InputType> DefaultInputCmd;
					FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(DefaultInputCmd, P);
				}
				else
				{
					FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(Frames->Buffer[Frame].InputCmd, P); // 2. InputCmd
				}

				FJoltNetworkPredictionSerialization::SerializeDeltaMS(P.Ar, TickState->Frames[Frame].DeltaMS); // 3. Delta InputCmd
			}
		}
	}

private:

	static void EatCmd(const FJoltNetSerializeParams& P)
	{
		TJoltConditionalState<InputType> Empty;
		FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(Empty, P);  // 2. InputCmd

		int32 TimeMS = 0;
		FJoltNetworkPredictionSerialization::SerializeDeltaMS(P.Ar, TimeMS); // 3. Delta InputCmd
	}

	inline static int32 NumInputsPerSend = 6;
};

// ---------------------------------------------------------------------------------------------------------------------------
//	Server -> AP Client
//
//	The Fixed/Independent ticking cases differ a bit but still share the same the core payload: Sync/Aux/Cues.
//	Fixed tick sends last consumed client input frame # ANd the server frame in order to correlate client/server frame numbers.
//	Independent tick sends last consumed client input frame # + TotalSimTime in order to detect dropped frames.
//
//	Where this data comes from differs between Fixed/Independent.
//
// ---------------------------------------------------------------------------------------------------------------------------

template<typename ModelDef>
class TCommonReplicator_AP
{
public:

	static void NetRecv(const FJoltNetSerializeParams& P, TInstanceData<ModelDef>& InstanceData, TJoltClientRecvData<ModelDef>& ClientRecvState, typename TJoltInstanceFrameState<ModelDef>::FFrame* BaseDeltaFrame)
	{
		// ** Modified By Kai Delta Serialization Support **//
		if(BaseDeltaFrame)
		{
			FJoltNetSerializeParams DeltaSerializationParams = P;
			DeltaSerializationParams.BaseDeltaStatePtr = (void*)BaseDeltaFrame->SyncState.Get();
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(ClientRecvState.SyncState, DeltaSerializationParams);	// 1. Sync
			DeltaSerializationParams.BaseDeltaStatePtr = (void*)BaseDeltaFrame->AuxState.Get();
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(ClientRecvState.AuxState, DeltaSerializationParams);	// 2. Aux
		}
		// ** End Modified By Kai Delta Serialization Support **//
		else
		{
			FJoltNetSerializeParams DeltaSerializationParams = P;
			DeltaSerializationParams.BaseDeltaStatePtr = nullptr;
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(ClientRecvState.SyncState, DeltaSerializationParams);	// 1. Sync
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(ClientRecvState.AuxState, DeltaSerializationParams);	// 2. Aux
		}
		
		UE_JNP_TRACE_USER_STATE_SYNC(ModelDef, ClientRecvState.SyncState.Get());
		UE_JNP_TRACE_USER_STATE_AUX(ModelDef, ClientRecvState.AuxState.Get());
		
	}

	static void NetSend(const FJoltNetSerializeParams& P, TInstanceData<ModelDef>& InstanceData, typename TJoltInstanceFrameState<ModelDef>::FFrame& FrameData, typename TJoltInstanceFrameState<ModelDef>::FFrame* BaseDeltaFrame)
	{
		// ** Modified By Kai Delta Serialization Support **//
		if (BaseDeltaFrame)
		{
			FJoltNetSerializeParams DeltaSerializationParams = P;
			DeltaSerializationParams.BaseDeltaStatePtr = (void*)(BaseDeltaFrame->SyncState.Get());
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(FrameData.SyncState, DeltaSerializationParams);	// 1. Sync
			DeltaSerializationParams.BaseDeltaStatePtr = (void*) BaseDeltaFrame->AuxState.Get();
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(FrameData.AuxState, DeltaSerializationParams);	// 2. Aux
		}
		// ** End Modified By Kai Delta Serialization Support **//
		else
		{
			FJoltNetSerializeParams DeltaSerializationParams = P;
			DeltaSerializationParams.BaseDeltaStatePtr = nullptr;
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(FrameData.SyncState, DeltaSerializationParams);	// 1. Sync
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(FrameData.AuxState, DeltaSerializationParams);	// 2. Aux
		}
		
	}
};

template<typename InModelDef>
class TFixedTickReplicator_AP
{
public:

	using ModelDef = InModelDef;
	

	// ------------------------------------------------------------------------------------------------------------
	// AP Client receives from the server
	// ------------------------------------------------------------------------------------------------------------
	static void NetRecv(const FJoltNetSerializeParams& P, TJoltClientRecvData<ModelDef>& ClientRecvState, TJoltModelDataStore<ModelDef>* DataStore, FJoltFixedTickState* TickState)
	{
		FArchive& Ar = P.Ar;

		typename TJoltInstanceFrameState<ModelDef>::FFrame* BaseDeltaFrame = nullptr;
		// *** Modified By Kai Delta Serialization Support *** //
		bool HasAckedFrame = false;
		Ar.SerializeBits(&HasAckedFrame,1); // 1. Has delta frame
		int32 DeltaStateFrame = INDEX_NONE;
		if (HasAckedFrame)
		{
			DeltaStateFrame = FJoltNetworkPredictionSerialization::ReadCompressedFrame(Ar, 0); // 2. acked delta number
		}

		uint32 DataSize = 0;
		P.Ar.SerializeIntPacked(DataSize); // 3 . Data Size (in case it's invalid to throw away)
		
		if (DeltaStateFrame != INDEX_NONE)
		{
			BaseDeltaFrame = ClientRecvState.AckedFrames.Find(DeltaStateFrame);
			// if we received a valid frame index , but we can't find it in our saved acked frame, discard this update it is out of order
			if (!BaseDeltaFrame)
			{
				TArray<uint8> DiscardedData;
				DiscardedData.SetNumZeroed((DataSize + 7 )/ 8);
				P.Ar.SerializeBits(DiscardedData.GetData(),DataSize); // Read all invalid together from the buffer and do nothing with it.
				UE_LOG(LogJoltNetworkPrediction,Warning,TEXT("Client ID %d Discarded Update from Missing Delta State %d"),ClientRecvState.ID,DeltaStateFrame)
				return;
			}
			
			// Remove any frames saved that are older than this Received delta frame.
			// once server uses specific frame for delta serialization,
			// if we receive an update that uses an older one, meaning out-of-order packet we will discard it
			for (auto It = ClientRecvState.AckedFrames.CreateIterator(); It; ++It)
			{
				if (It.Key() < DeltaStateFrame)
				{
					It.RemoveCurrent();
				}
			}
		}
		
		const int32 LastConsumedInputFrame = FJoltNetworkPredictionSerialization::ReadCompressedFrame(Ar, TickState->PendingFrame); // 4. Last Consumed (Client) Input Frame
		const int32 ServerFrame = FJoltNetworkPredictionSerialization::ReadCompressedFrame(Ar, TickState->PendingFrame + TickState->Offset); // 5. Server's Frame
		
		
		// this can happen if client sends an input at the start then because of hitch from loading or otherwise
		// big frame delta , so client doesn't tick for a couple of frames but server still ticking and advancing the input.
		// in this case we need the client to jump his simulation forward by the amount of time it was stopped and consider the state unchanged during this lag spike
		
		jnpEnsure(LastConsumedInputFrame <= TickState->PendingFrame);
		jnpEnsure(ServerFrame >= 0);
		

		if (LastConsumedInputFrame != INDEX_NONE)
		{
			// Calculate TickState::Offset the difference between client and server frame numbers.
			// LocalFrame = ServerFrame - Offset

			// LastConsumedInputFrame was used as input to produce ServerFrame on the server,
			// So ServerFrame/LastConsumedInputFrame are intrinsically "one frame off".

			// We want:
			// LocalFrame + Offset = ServerFrame.

			// There for:
			// LastConsumedInputFrame + 1 + Offset = ServerFrame.
			
			TickState->Offset = ServerFrame - LastConsumedInputFrame - 1;
		}
		
		// AP recv drives fixed tick interpolation
		TickState->Interpolation.LatestRecvFrameAP = ServerFrame;
		TickState->ConfirmedFrame = ServerFrame - TickState->Offset;
		
		ClientRecvState.ServerFrame = ServerFrame;
		UE_JNP_TRACE_NET_RECV(ServerFrame, ServerFrame * TickState->FixedStepMS);

		jnpEnsureSlow(ClientRecvState.InstanceIdx >= 0);
		TInstanceData<ModelDef>& InstanceData = DataStore->Instances.GetByIndexChecked(ClientRecvState.InstanceIdx);

		TCommonReplicator_AP<ModelDef>::NetRecv(P, InstanceData, ClientRecvState,BaseDeltaFrame); // 6. Common
		//Set The Acked Frame Data in Client Rcv Struct and Save the frame to be sent to server
		if (TickState->Offset > 0 && ClientRecvState.ID >= 0)
		{
			typename TJoltInstanceFrameState<ModelDef>::FFrame& AckedFrameData = ClientRecvState.AckedFrames.FindOrAdd(ServerFrame);
			ClientRecvState.SyncState.CopyTo(AckedFrameData.SyncState);
			ClientRecvState.AuxState.CopyTo(AckedFrameData.AuxState);
			
			uint32& AckedFrame = TickState->LocalAckedFrames.IDsToAckedFrames.FindOrAdd(ClientRecvState.ID);
			AckedFrame = ServerFrame;
		}
		
		InstanceData.CueDispatcher->NetRecvSavedCues(P.Ar, true, ServerFrame, 0); // 7. JoltNetSimCues
	}

	// ------------------------------------------------------------------------------------------------------------
	// Server sends to AP client
	// ------------------------------------------------------------------------------------------------------------
	static void NetSend(const FJoltNetSerializeParams& P, FJoltNetworkPredictionID ID, TJoltModelDataStore<ModelDef>* DataStore, const FJoltFixedTickState* TickState)
	{
		// *** Modified By Kai for delta serialization , and unified input handling *** //
		FArchive& Ar = P.Ar;
		UPackageMapClient* PackageMapClient = Cast<UPackageMapClient>(P.Map);
		UNetConnection* NetConnection = PackageMapClient->GetConnection();
		jnpCheckSlow(NetConnection);
		
		TInstanceData<ModelDef>* Instance = DataStore->Instances.Find(ID);
		jnpCheckSlow(Instance);

		TJoltInstanceFrameState<ModelDef>* Frames = DataStore->Frames.Find(ID);
		jnpCheckSlow(Frames);

		int32 LastConsumedFrame = INDEX_NONE;
		int32 LastReceivedFrame = INDEX_NONE; 
		
		if (UJoltNetworkPredictionPlayerControllerComponent* RPCHandler = Instance->Info.RPCHandler)
		{
			LastConsumedFrame = RPCHandler->LastConsumedFrame;
			LastReceivedFrame = RPCHandler->LastReceivedFrame; 
		}

		const int32 PendingFrame = TickState->PendingFrame;
		jnpEnsureSlow(PendingFrame >= 0);

		//** Delta Serialization **// 
		typename TJoltInstanceFrameState<ModelDef>::FFrame* BaseDeltaFrame = nullptr;
		int32 AckedFrame = INDEX_NONE;
		bool HasAckedFrame = false;
		if (LastConsumedFrame != INDEX_NONE)
		{
			if (const FJoltAckedFrames* NetConnectionAckedFrames = TickState->ServerAckedFrames.ConnectionsAckedFrames.Find(NetConnection))
			{
				if (const uint32* FoundAckedFrame = NetConnectionAckedFrames->IDsToAckedFrames.Find(ID))
				{
					int32 AckedFrameAsSigned = (int32)*FoundAckedFrame;
					if (AckedFrameAsSigned < PendingFrame && PendingFrame - AckedFrameAsSigned < Frames->Buffer.Capacity())
					{
						AckedFrame = *FoundAckedFrame;
						HasAckedFrame = true;
					}
				}
			}
		}

		if (NetworkPredictionCVars::ToggleDeltaSerialize() == 0)
		{
			HasAckedFrame = false;
		}
		
		if(HasAckedFrame )
		{
			BaseDeltaFrame = &Frames->Buffer[AckedFrame];
		}
		
		Ar.SerializeBits(&HasAckedFrame,1); //1. Has Delta Frame
		if (HasAckedFrame)
		{
			FJoltNetworkPredictionSerialization::WriteCompressedFrame(Ar, AckedFrame);// 2. Delta frame number
		}

		// 1 - After delta state index Serialize the rest of the data in another archive.
		// 2- Send local archive data size to client
		// 3 - add serialized bytes to main archive
		// this allows client to discard this update if out of order causing delta state to be unavailable on reader side (client)
		FNetBitWriter* MainWriter = static_cast<FNetBitWriter*>(&P.Ar);
		FNetBitWriter LocalNetWriter (P.Map , (MainWriter->GetMaxBits() + 7) / 8);
		FJoltNetSerializeParams LocalParams(LocalNetWriter,P.Map,P.ReplicationTarget);

		//-----------------------------------------------------------------------------------//
		
		FJoltNetworkPredictionSerialization::WriteCompressedFrame(LocalParams.Ar, LastConsumedFrame); // 4. Last Consumed Input Frame (Client's frame)
		FJoltNetworkPredictionSerialization::WriteCompressedFrame(LocalParams.Ar, PendingFrame); // 5. PendingFrame (Server's frame)

		TCommonReplicator_AP<ModelDef>::NetSend(LocalParams, *Instance, Frames->Buffer[PendingFrame],BaseDeltaFrame); // 6. Common

		Instance->CueDispatcher->NetSendSavedCues(LocalParams.Ar, EJoltNetSimCueReplicationTarget::AutoProxy, true); // 7. JoltNetSimCues
		
		// After serializing the delta frame index , we serialized all rest of data in a local archive.
		// we now send the client the data size and add this data to main network archive.
		// to allow client to discard it safely or read it as if it was serialized normally.
		uint32 DataSize = (uint32)LocalNetWriter.GetNumBits();
		P.Ar.SerializeIntPacked(DataSize);  // 3. Data Size Num (this will be read before 4+ and after delta frame num)
		P.Ar.SerializeBits(LocalNetWriter.GetData(),LocalNetWriter.GetNumBits());
	}
};

template<typename InModelDef>
class TIndependentTickReplicator_AP
{
public:

	using ModelDef = InModelDef;

	// ------------------------------------------------------------------------------------------------------------
	// AP Client receives from the server
	// ------------------------------------------------------------------------------------------------------------
	static void NetRecv(const FJoltNetSerializeParams& P, TJoltClientRecvData<ModelDef>& ClientRecvState, TJoltModelDataStore<ModelDef>* DataStore, FJoltVariableTickState* TickState)
	{
		FArchive& Ar = P.Ar;
		const int32 LastConsumedInputFrame = FJoltNetworkPredictionSerialization::ReadCompressedFrame(Ar, TickState->PendingFrame); // 1. Last Consumed (Client) Input Frame
		ClientRecvState.ServerFrame = LastConsumedInputFrame + 1;
		jnpEnsure(ClientRecvState.ServerFrame >= 0);

		TickState->ConfirmedFrame = ClientRecvState.ServerFrame;

		FJoltNetworkPredictionSerialization::SerializeTimeMS(P.Ar, ClientRecvState.SimTimeMS); // 2. TotalSimTime

		UE_JNP_TRACE_NET_RECV(ClientRecvState.ServerFrame, ClientRecvState.SimTimeMS);

		TInstanceData<ModelDef>& InstanceData = DataStore->Instances.GetByIndexChecked(ClientRecvState.InstanceIdx);
		TCommonReplicator_AP<ModelDef>::NetRecv(P, InstanceData, ClientRecvState,nullptr); // 3. Common
	}

	// ------------------------------------------------------------------------------------------------------------
	// Server sends to AP client
	// ------------------------------------------------------------------------------------------------------------
	static void NetSend(const FJoltNetSerializeParams& P, FJoltNetworkPredictionID ID, TJoltModelDataStore<ModelDef>* DataStore, TJoltServerRecvData_Independent<ModelDef>& ServerRecvData, const FJoltVariableTickState* VariableTickState)
	{
		FArchive& Ar = P.Ar;
		FJoltNetworkPredictionSerialization::WriteCompressedFrame(Ar, ServerRecvData.LastConsumedFrame); // 1. Last Consumed Input Frame (Client's frame)
		FJoltNetworkPredictionSerialization::SerializeTimeMS(Ar, ServerRecvData.TotalSimTimeMS); // 2. TotalSimTime

		TInstanceData<ModelDef>& InstanceData = DataStore->Instances.GetByIndexChecked(ServerRecvData.InstanceIdx);
		TJoltInstanceFrameState<ModelDef>& Frames = DataStore->Frames.GetByIndexChecked(ServerRecvData.FramesIdx);
		
		TCommonReplicator_AP<ModelDef>::NetSend(P, InstanceData, Frames.Buffer[ServerRecvData.PendingFrame],nullptr); // 3. Common
	}
};

// ---------------------------------------------------------------------------------------------------------------------------
//	Server -> SP Client
//	Like the AP case, the core payload is the same between fixed and independent: Input/Sync/Aux/Cues
//
//	There are actually 3 cases to consider here:
//	1. Fixed Tick: only sends server frame #
//	2. Independent Tick, remotely controlled: send total sim time, which comes from the server's TJoltServerRecvData_Independent for the controlling client.
//	3. Independent Tick, locally controlled: send total sim time, which comes from the server's local VariableTickState.
//
// ---------------------------------------------------------------------------------------------------------------------------

template<typename InModelDef>
class TCommonReplicator_SP
{
public:

	using ModelDef = InModelDef;

	template<typename ClientRecvDataType>
	static void NetRecv(const FJoltNetSerializeParams& P, ClientRecvDataType& ClientRecvState, TJoltModelDataStore<ModelDef>* DataStore, typename TJoltInstanceFrameState<ModelDef>::FFrame* BaseDeltaFrame)
	{
		// ** Modified By Kai Delta Serialization Support **//
		if (BaseDeltaFrame)
		{
			FJoltNetSerializeParams DeltaSerializationParams = P;
			DeltaSerializationParams.BaseDeltaStatePtr = (void*)BaseDeltaFrame->InputCmd.Get();
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(ClientRecvState.InputCmd, DeltaSerializationParams);	// 1. Input

			DeltaSerializationParams.BaseDeltaStatePtr = (void*)BaseDeltaFrame->SyncState.Get();
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(ClientRecvState.SyncState, DeltaSerializationParams);	// 2. Sync

			DeltaSerializationParams.BaseDeltaStatePtr = (void*)BaseDeltaFrame->AuxState.Get();
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(ClientRecvState.AuxState, DeltaSerializationParams);	// 3. Aux
		}
		// ** End Modified By Kai Delta Serialization Support **//
		else
		{
			FJoltNetSerializeParams DeltaSerializationParams = P;
			DeltaSerializationParams.BaseDeltaStatePtr = nullptr;
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(ClientRecvState.InputCmd, DeltaSerializationParams);	// 1. Input
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(ClientRecvState.SyncState, DeltaSerializationParams);	// 2. Sync
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(ClientRecvState.AuxState, DeltaSerializationParams);	// 3. Aux
		}
		

		UE_JNP_TRACE_USER_STATE_INPUT(ModelDef, ClientRecvState.InputCmd.Get());
		UE_JNP_TRACE_USER_STATE_SYNC(ModelDef, ClientRecvState.SyncState.Get());
		UE_JNP_TRACE_USER_STATE_AUX(ModelDef, ClientRecvState.AuxState.Get());
	}
	
	static void NetSend(const FJoltNetSerializeParams& P, FJoltNetworkPredictionID ID, TJoltModelDataStore<ModelDef>* DataStore, TInstanceData<ModelDef>* InstanceData, int32 PendingFrame, typename TJoltInstanceFrameState<ModelDef>::FFrame* BaseDeltaFrame)
	{
		jnpCheckSlow(InstanceData);
		
		TJoltInstanceFrameState<ModelDef>* Frames = DataStore->Frames.Find(ID);
		jnpCheckSlow(Frames);
		// ** Modified By Kai Delta Serialization Support **//
		typename TJoltInstanceFrameState<ModelDef>::FFrame& FrameData = Frames->Buffer[PendingFrame];
		if (BaseDeltaFrame)
		{
			FJoltNetSerializeParams DeltaSerializationParams = P;
			DeltaSerializationParams.BaseDeltaStatePtr = (void*)BaseDeltaFrame->InputCmd.Get();
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(FrameData.InputCmd, DeltaSerializationParams);	// 1. Input

			DeltaSerializationParams.BaseDeltaStatePtr = (void*)BaseDeltaFrame->SyncState.Get();
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(FrameData.SyncState, DeltaSerializationParams);	// 2. Sync

			DeltaSerializationParams.BaseDeltaStatePtr = (void*)BaseDeltaFrame->AuxState.Get();
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(FrameData.AuxState, DeltaSerializationParams);	// 3. Aux
		}
		// ** End Modified By Kai Delta Serialization Support **//
		else
		{
			FJoltNetSerializeParams DeltaSerializationParams = P;
			DeltaSerializationParams.BaseDeltaStatePtr = nullptr;
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(FrameData.InputCmd, DeltaSerializationParams);	// 1. Input
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(FrameData.SyncState, DeltaSerializationParams);	// 2. Sync
			FJoltNetworkPredictionDriver<ModelDef>::NetSerialize(FrameData.AuxState, DeltaSerializationParams);	// 3. Aux
		}
		
	}
};

template<typename InModelDef>
class TFixedTickReplicator_SP
{
public:

	using ModelDef = InModelDef;

	// ------------------------------------------------------------------------------------------------------------
	// SP Client receives from the server
	// ------------------------------------------------------------------------------------------------------------
	static void NetRecv(const FJoltNetSerializeParams& P, TJoltClientRecvData<ModelDef>& ClientRecvState, TJoltModelDataStore<ModelDef>* DataStore, FJoltFixedTickState* TickState)
	{
		
		UPackageMapClient* PackageMapClient = Cast<UPackageMapClient>(P.Map);
		typename TJoltInstanceFrameState<ModelDef>::FFrame* BaseDeltaFrame = nullptr;
		// ** Modified By Kai Delta Serialization Support **//
		bool HasAckedFrame = false;
		P.Ar.SerializeBits(&HasAckedFrame,1); //1 . has Delta Frame
		int32 DeltaStateFrame = INDEX_NONE;
		if (HasAckedFrame)
		{
			DeltaStateFrame = FJoltNetworkPredictionSerialization::ReadCompressedFrame(P.Ar, 0); // 2. Delta Frame Num
		}

		uint32 DataSize = 0;
		P.Ar.SerializeIntPacked(DataSize); // 3 . Data Size (in case it's invalid to throw away)

		if (DeltaStateFrame != INDEX_NONE)
		{
			BaseDeltaFrame = ClientRecvState.AckedFrames.Find(DeltaStateFrame);
			// if we received a valid frame index , but we can't find it in our saved acked frame, discard this update it is out of order
			if (!BaseDeltaFrame)
			{
				TArray<uint8> DiscardedData;
				DiscardedData.SetNumZeroed((DataSize + 7 )/ 8);
				P.Ar.SerializeBits(DiscardedData.GetData(),DataSize); // Read all invalid together from the buffer and do nothing with it.
				UE_LOG(LogJoltNetworkPrediction,Warning,TEXT("Client ID %d Discarded Update from Missing Delta State %d"),ClientRecvState.ID,DeltaStateFrame)
				return;
			}
			
			// Remove any frames saved that are older than this Received delta frame.
			// once server uses specific frame for delta serialization,
			// if we receive an update that uses an older one, meaning out-of-order packet we will discard it
			for (auto It = ClientRecvState.AckedFrames.CreateIterator(); It; ++It)
			{
				if (It.Key() < DeltaStateFrame)
				{
					It.RemoveCurrent();
				}
			}
		}
		// ** End Modified By Kai Delta Serialization Support **//
		const int32 PrevRecv = ClientRecvState.ServerFrame;
		ClientRecvState.ServerFrame = FJoltNetworkPredictionSerialization::ReadCompressedFrame(P.Ar, 0); // 4. PendingFrame (Server Frame)
		
		jnpEnsure(ClientRecvState.ServerFrame >= 0);

		TickState->Interpolation.LatestRecvFrameSP = FMath::Max(TickState->Interpolation.LatestRecvFrameSP, ClientRecvState.ServerFrame);

		UE_JNP_TRACE_NET_RECV(ClientRecvState.ServerFrame, ClientRecvState.ServerFrame * TickState->FixedStepMS);
		
		TCommonReplicator_SP<ModelDef>::NetRecv(P, ClientRecvState, DataStore, BaseDeltaFrame); // 5. Common
		// Add Acked frame to Tick state acked frames , auto proxy is in his RPC responsible for sending it to server
		if (ClientRecvState.ServerFrame > 0)
		{
			uint32& AckedFrame = TickState->LocalAckedFrames.IDsToAckedFrames.FindOrAdd(ClientRecvState.ID);
			AckedFrame = ClientRecvState.ServerFrame;
			typename TJoltInstanceFrameState<ModelDef>::FFrame& AckedFrameData = ClientRecvState.AckedFrames.FindOrAdd(AckedFrame);
			ClientRecvState.SyncState.CopyTo(AckedFrameData.SyncState);
			ClientRecvState.AuxState.CopyTo(AckedFrameData.AuxState);
			ClientRecvState.InputCmd.CopyTo(AckedFrameData.InputCmd);
		}
		jnpEnsureSlow(ClientRecvState.InstanceIdx >= 0);
		TInstanceData<ModelDef>& InstanceData = DataStore->Instances.GetByIndexChecked(ClientRecvState.InstanceIdx);

		const bool bSerializeCueFrames = true; // Fixed tick can use Frame numbers for SP serialization
		InstanceData.CueDispatcher->NetRecvSavedCues(P.Ar, bSerializeCueFrames, ClientRecvState.ServerFrame, 0); // 6. JoltNetSimCues
	}

	// ------------------------------------------------------------------------------------------------------------
	// Server sends to SP Client
	// ------------------------------------------------------------------------------------------------------------
	static void NetSend(const FJoltNetSerializeParams& P, FJoltNetworkPredictionID ID, TJoltModelDataStore<ModelDef>* DataStore, const FJoltFixedTickState* TickState)
	{
		const int32 PendingFrame = TickState->PendingFrame;
		jnpEnsure(PendingFrame >= 0);

		TInstanceData<ModelDef>* Instance = DataStore->Instances.Find(ID);
		jnpCheckSlow(Instance);
		
		TJoltInstanceFrameState<ModelDef>* Frames = DataStore->Frames.Find(ID);
		jnpCheckSlow(Frames);

		// ** Added By Kai Delta Serialization Support **//
		UPackageMapClient* PackageMapClient = Cast<UPackageMapClient>(P.Map);
		UNetConnection* NetConnection = PackageMapClient->GetConnection();
		jnpCheckSlow(NetConnection);
		
		// ** Added By Kai Delta Serialization **//
		typename TJoltInstanceFrameState<ModelDef>::FFrame* BaseDeltaFrame = nullptr;
		int32 AckedFrame = INDEX_NONE;
		if (const FJoltAckedFrames* AckedFrames = TickState->ServerAckedFrames.ConnectionsAckedFrames.Find(NetConnection))
		{
			if (const uint32* FoundAckedFrame = AckedFrames->IDsToAckedFrames.Find(ID))
			{
				int32 AckedFrameAsSigned = (int32)*FoundAckedFrame;
				if ((PendingFrame - AckedFrameAsSigned) < Frames->Buffer.Capacity())
				{
					AckedFrame = *FoundAckedFrame;
				}
			}
		}
		bool HasAckedFrame = AckedFrame != INDEX_NONE;
		if (NetworkPredictionCVars::ToggleDeltaSerialize() == 0)
		{
			HasAckedFrame = false;
		}
		P.Ar.SerializeBits(&HasAckedFrame,1); //1. Has Delta Frame
		if (HasAckedFrame)
		{
			BaseDeltaFrame = &Frames->Buffer[AckedFrame]; 
			FJoltNetworkPredictionSerialization::WriteCompressedFrame(P.Ar, AckedFrame); // 2. Delta Frame Num
		}

		// 1 - After delta state index Serialize the rest of the data in another archive.
		// 2- Send local archive data size to client
		// 3 - add serialized bytes to main archive
		// this allows client to discard this update if out of order causing delta state to be unavailable on reader side (client)
		FNetBitWriter* MainWriter = static_cast<FNetBitWriter*>(&P.Ar);
		FNetBitWriter LocalNetWriter (P.Map , (MainWriter->GetMaxBits() + 7) / 8);
		FJoltNetSerializeParams LocalParams(LocalNetWriter,P.Map,P.ReplicationTarget);

		FJoltNetworkPredictionSerialization::WriteCompressedFrame(LocalParams.Ar, PendingFrame); // 4. PendingFrame (Server's frame)
		
		TCommonReplicator_SP<ModelDef>::NetSend(LocalParams, ID, DataStore, Instance, PendingFrame,BaseDeltaFrame); // 5. Common

		const bool bSerializeCueFrames = true; // Fixed tick can use Frame numbers for SP serialization
		Instance->CueDispatcher->NetSendSavedCues(LocalParams.Ar, EJoltNetSimCueReplicationTarget::SimulatedProxy | EJoltNetSimCueReplicationTarget::Interpolators, bSerializeCueFrames); // 6. JoltNetSimCues

		// After serializing the delta frame index , we serialized all rest of data in a local archive.
		// we now send the client the data size and add this data to main network archive.
		// to allow client to discard it safely or read it as if it was serialized normally.
		uint32 DataSize = (uint32)LocalNetWriter.GetNumBits();
		P.Ar.SerializeIntPacked(DataSize); // 3. Data Size Num (this will be read before 4,5,6 and after delta frame num)
		P.Ar.SerializeBits(LocalNetWriter.GetData(),LocalNetWriter.GetNumBits());
	}
};

template<typename InModelDef>
class TIndependentTickReplicator_SP
{
public:

	using ModelDef = InModelDef;

	// ------------------------------------------------------------------------------------------------------------
	// SP Client receives from the server
	// ------------------------------------------------------------------------------------------------------------
	static void NetRecv(const FJoltNetSerializeParams& P, TJoltClientRecvData<ModelDef>& ClientRecvState, TJoltModelDataStore<ModelDef>* DataStore, FJoltVariableTickState* TickState)
	{
		FJoltNetworkPredictionSerialization::SerializeTimeMS(P.Ar, ClientRecvState.SimTimeMS); // 1. ServerTotalSimTime

		
#if UE_JNP_TRACE_ENABLED
		int32 TraceSimTime = 0;
		FJoltNetworkPredictionSerialization::SerializeTimeMS(P.Ar, TraceSimTime); // 2. ServerTotalSimTime
#else
		int32 TraceSimTime = ClientRecvState.SimTimeMS;
#endif

		// SP timestamps drive independent interpolation
		// (AP frame/time can't help here - that is the nature of independent ticking!)
		TickState->Interpolation.LatestRecvTimeMS = FMath::Max(TickState->Interpolation.LatestRecvTimeMS, ClientRecvState.SimTimeMS);
		
		// This is kinda wrong but not clear what it should be. The server's frame# is irrelevant in independent tick for SPs.
		// Should we not trace it and have insights handle this case explicitly? Or guess where it would go roughly?
		// Just tracing it as "latest" for now.
		const int32 TraceFrame = TickState->PendingFrame;
		jnpEnsure(TraceFrame >= 0);

		UE_JNP_TRACE_NET_RECV(TraceFrame, TraceSimTime);
		
		TCommonReplicator_SP<ModelDef>::NetRecv(P, ClientRecvState, DataStore,nullptr); // 3. Common

		jnpEnsureSlow(ClientRecvState.InstanceIdx >= 0);
		TInstanceData<ModelDef>& InstanceData = DataStore->Instances.GetByIndexChecked(ClientRecvState.InstanceIdx);

		const bool bSerializeCueFrames = true; // Fixed tick can use Frame numbers for SP serialization
		InstanceData.CueDispatcher->NetRecvSavedCues(P.Ar, bSerializeCueFrames, INDEX_NONE, ClientRecvState.SimTimeMS); // 4. JoltNetSimCues
	}

	// ------------------------------------------------------------------------------------------------------------
	// Server sends to SP Client
	// ------------------------------------------------------------------------------------------------------------
	
	// For locally controlled/ticked actors on the server
	
	static void NetSend(const FJoltNetSerializeParams& P, FJoltNetworkPredictionID ID, TJoltModelDataStore<ModelDef>* DataStore, const FJoltVariableTickState* TickState)
	{
		const int32 TotalSimTime = TickState->Frames[TickState->PendingFrame].TotalMS;
		NetSend(P, ID, DataStore, TotalSimTime, TotalSimTime, TickState->PendingFrame);
	}

	// For remotely controlled/ticked actors on the server
	static void NetSend(const FJoltNetSerializeParams& P, FJoltNetworkPredictionID ID, TJoltModelDataStore<ModelDef>* DataStore, const TJoltServerRecvData_Independent<ModelDef>& IndependentTickState, const FJoltVariableTickState* VariableTickState)
	{
		// Note we are sending the (Server's) local variable tick sim time as the timestamp, not the actual independent tick.
		// Reasoning: The VariableTick timestamp is when the last tick took place on the server. Its when the stuff that appened in tick "actually happened" relative to everything else.
		// The independent tick time is really between the AP client and server. Letting this time "leak" to the SP clients means they have to deal with aligning/reconcile the timestamps
		// of the remote controlled sim differently than the non remote controlled sim. (remote controlled on the server).
		//
		// Practical reason: cues are timestamped with the variable tick time. (AP client will use frames, SP clients will use time. Easier to align the times server side than have
		// each client do it independently for each independently ticking remote controlled simulation.
		const int32 VariableTickTimeMS = VariableTickState->Frames[VariableTickState->PendingFrame].TotalMS;

		TJoltServerRecvData_Independent<ModelDef>* IndependentTickData =  DataStore->ServerRecv_IndependentTick.Find(ID);
		jnpCheckSlow(IndependentTickData);
		const int32 IndependentSimTimeMS = IndependentTickData->TotalSimTimeMS;

		NetSend(P, ID, DataStore, IndependentSimTimeMS, VariableTickTimeMS, IndependentTickState.PendingFrame);
	}	

private:

	static void NetSend(const FJoltNetSerializeParams& P, FJoltNetworkPredictionID ID, TJoltModelDataStore<ModelDef>* DataStore, int32 IndependentSimTime, int32 ServerTotalSimTime, int32 PendingFrame)
	{
		TInstanceData<ModelDef>* Instance = DataStore->Instances.Find(ID);
		jnpCheckSlow(Instance);

		FJoltNetworkPredictionSerialization::SerializeTimeMS(P.Ar, ServerTotalSimTime); // 1. ServerTotalSimTime

#if UE_JNP_TRACE_ENABLED
		FJoltNetworkPredictionSerialization::SerializeTimeMS(P.Ar, IndependentSimTime); // 2. IndependentSimTime
#endif

		TCommonReplicator_SP<ModelDef>::NetSend(P, ID, DataStore, Instance, PendingFrame,nullptr); // 3. Common

		const bool bSerializeCueFrames = false; // Independent tick cannot use Frame numbers for SP serialization (use time instead)
		Instance->CueDispatcher->NetSendSavedCues(P.Ar, EJoltNetSimCueReplicationTarget::SimulatedProxy | EJoltNetSimCueReplicationTarget::Interpolators, bSerializeCueFrames); // 4. JoltNetSimCues
	}
};

// ---------------------------------------------------------------------------------------------------------------------------
