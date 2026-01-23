// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Components/ActorComponent.h"
#include "JoltNetworkPredictionProxy.h"
#include "JoltNetworkPredictionReplicationProxy.h"

#include "JoltNetworkPredictionComponent.generated.h"

enum ENetRole : int;
namespace EEndPlayReason { enum Type : int; }
class UJoltNetworkPredictionPlayerControllerComponent;
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
//	UJoltNetworkPredictionComponent
//	This is the base component for running a TJoltNetworkedSimulationModel through an actor component. This contains the boiler plate hooks into getting the system
//	initialized and plugged into the UE replication system.
//
//	This is an abstract component and cannot function on its own. It must be subclassed and InitializeNetworkPredictionProxy must be implemented.
//	Ticking and RPC sending will be handled automatically.
//
//	Its also worth pointing out that nothing about being a UActorComponent is essential here. All that this component does could be done within an AActor itself.
//	An actor component makes sense for flexible/reusable code provided by the plugin. But there is nothing stopping you from copying this directly into an actor if you had reason to.
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
UCLASS(Abstract)
class JOLTNETWORKPREDICTION_API UJoltNetworkPredictionComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UJoltNetworkPredictionComponent();

	virtual void InitializeComponent() override;
	virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;
	virtual void PreNetReceive() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason);

	// Invoke the ServerRPC, called from UJoltNetworkPredictionWorldManager via the TJoltServerRPCService.
	void CallServerRPC();

protected:
	
	// Classes must initialize the NetworkPredictionProxy (register with the NetworkPredictionSystem) here. EndPlay will unregister.
	virtual void InitializeNetworkPredictionProxy() { check(false); }

	// Finalizes initialization when NetworkRole changes. Does not need to be overridden.
	virtual void InitializeForNetworkRole(ENetRole Role, const bool bHasNetConnection,UJoltNetworkPredictionPlayerControllerComponent* RPCHandler);

	// Helper: Checks if the owner's role has changed and calls InitializeForNetworkRole if necessary.
	bool CheckOwnerRoleChange();

	// The actual ServerRPC. This needs to be a UFUNCTION but is generic and not dependent on the simulation instance
	UFUNCTION(Server, unreliable, WithValidation)
	void ServerReceiveClientInput(const FJoltServerReplicationRPCParameter& ProxyParameter);

	// Proxy to interface with the NetworkPrediction system
	UPROPERTY(Replicated, transient)
	FJoltNetworkPredictionProxy NetworkPredictionProxy;

	// ReplicationProxies are just pointers to the data/NetSerialize functions within the JoltNetworkSim
	UPROPERTY()
	FJoltReplicationProxy ReplicationProxy_ServerRPC;

private:

	UPROPERTY(Replicated, transient)
	FJoltReplicationProxy ReplicationProxy_Autonomous;

	UPROPERTY(Replicated, transient)
	FJoltReplicationProxy ReplicationProxy_Simulated;

	UPROPERTY(Replicated, transient)
	FJoltReplicationProxy ReplicationProxy_Replay;

protected:

	FJoltReplicationProxySet GetReplicationProxies()
	{
		return FJoltReplicationProxySet{ &ReplicationProxy_ServerRPC, &ReplicationProxy_Autonomous, &ReplicationProxy_Simulated, &ReplicationProxy_Replay };
	}
};
