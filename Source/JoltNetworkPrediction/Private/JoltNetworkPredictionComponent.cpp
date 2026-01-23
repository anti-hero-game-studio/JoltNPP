// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltNetworkPredictionComponent.h"
#include "Engine/World.h"
#include "JoltNetworkPredictionWorldManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltNetworkPredictionComponent)

UJoltNetworkPredictionComponent::UJoltNetworkPredictionComponent()
{
	SetIsReplicatedByDefault(true);
	
}

void UJoltNetworkPredictionComponent::InitializeComponent()
{
	Super::InitializeComponent();

	UWorld* World = GetWorld();	
	UJoltNetworkPredictionWorldManager* NetworkPredictionWorldManager = GetWorld()->GetSubsystem<UJoltNetworkPredictionWorldManager>();
	if (NetworkPredictionWorldManager)
	{
		// Init RepProxies
		ReplicationProxy_ServerRPC.Init(&NetworkPredictionProxy, EJoltReplicationProxyTarget::ServerRPC);
		ReplicationProxy_Autonomous.Init(&NetworkPredictionProxy, EJoltReplicationProxyTarget::AutonomousProxy);
		ReplicationProxy_Simulated.Init(&NetworkPredictionProxy, EJoltReplicationProxyTarget::SimulatedProxy);
		ReplicationProxy_Replay.Init(&NetworkPredictionProxy, EJoltReplicationProxyTarget::Replay);

		InitializeNetworkPredictionProxy();

		CheckOwnerRoleChange();
	}
}

void UJoltNetworkPredictionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Super::EndPlay(Reason);
	NetworkPredictionProxy.EndPlay();
}

void UJoltNetworkPredictionComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);
	
	CheckOwnerRoleChange();

	// We have to update our replication proxies so they can be accurately compared against client shadowstate during property replication. ServerRPC proxy does not need to do this.
	ReplicationProxy_Autonomous.OnPreReplication();
	ReplicationProxy_Simulated.OnPreReplication();
	ReplicationProxy_Replay.OnPreReplication();
}

void UJoltNetworkPredictionComponent::PreNetReceive()
{
	Super::PreNetReceive();
	CheckOwnerRoleChange();
}

void UJoltNetworkPredictionComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME( UJoltNetworkPredictionComponent, NetworkPredictionProxy);
	DOREPLIFETIME_CONDITION( UJoltNetworkPredictionComponent, ReplicationProxy_Autonomous, COND_AutonomousOnly);
	DOREPLIFETIME_CONDITION( UJoltNetworkPredictionComponent, ReplicationProxy_Simulated, COND_SimulatedOnlyNoReplay);
	DOREPLIFETIME_CONDITION( UJoltNetworkPredictionComponent, ReplicationProxy_Replay, COND_ReplayOnly);
}

void UJoltNetworkPredictionComponent::InitializeForNetworkRole(ENetRole Role, const bool bHasNetConnection,UJoltNetworkPredictionPlayerControllerComponent* RPCHandler)
{
	NetworkPredictionProxy.InitForNetworkRole(Role, bHasNetConnection,RPCHandler);
}

bool UJoltNetworkPredictionComponent::CheckOwnerRoleChange()
{
	AActor* OwnerActor = GetOwner();
	const ENetRole CurrentRole = OwnerActor->GetLocalRole();
	const bool bHasNetConnection = OwnerActor->GetNetConnection() != nullptr;
	UJoltNetworkPredictionPlayerControllerComponent* RPCHandler = NetworkPredictionProxy.GetCachedRPCHandler();
	if (CurrentRole != ROLE_SimulatedProxy && bHasNetConnection && !IsValid(NetworkPredictionProxy.GetCachedRPCHandler()))
	{
		if (GetOwner()->GetNetConnection()->OwningActor)
		{
			RPCHandler = GetOwner()->GetNetConnection()->OwningActor->GetComponentByClass<UJoltNetworkPredictionPlayerControllerComponent>();
			if (!RPCHandler)
			{
				// Create and register a new component dynamically
				RPCHandler = NewObject<UJoltNetworkPredictionPlayerControllerComponent>(GetOwner()->GetNetConnection()->OwningActor);

				if (RPCHandler)
				{
					RPCHandler->SetNetAddressable();
					RPCHandler->SetIsReplicated(true);
					RPCHandler->RegisterComponent();
					RPCHandler->InitializeComponent();
					RPCHandler->Activate(true);
				}
			}
		}
	}
	
	if (CurrentRole != NetworkPredictionProxy.GetCachedNetRole() || bHasNetConnection != NetworkPredictionProxy.GetCachedHasNetConnection()
		|| RPCHandler != NetworkPredictionProxy.GetCachedRPCHandler())
	{
		InitializeForNetworkRole(CurrentRole, bHasNetConnection,RPCHandler);
		return true;
	}

	return false;
}

bool UJoltNetworkPredictionComponent::ServerReceiveClientInput_Validate(const FJoltServerReplicationRPCParameter& ProxyParameter)
{
	return true;
}

void UJoltNetworkPredictionComponent::ServerReceiveClientInput_Implementation(const FJoltServerReplicationRPCParameter& ProxyParameter)
{
	// The const_cast is unavoidable here because the replication system only allows by value (forces copy, bad) or by const reference. This use case is unique because we are using the RPC parameter as a temp buffer.
	const_cast<FJoltServerReplicationRPCParameter&>(ProxyParameter).NetSerializeToProxy(ReplicationProxy_ServerRPC);
}

void UJoltNetworkPredictionComponent::CallServerRPC()
{
	// Temp hack to make sure the ServerRPC doesn't get suppressed from bandwidth limiting
	// (system hasn't been optimized and not mature enough yet to handle gaps in input stream)
	FJoltScopedBandwidthLimitBypass BandwidthBypass(GetOwner());

	FJoltServerReplicationRPCParameter ProxyParameter(ReplicationProxy_ServerRPC);
	ServerReceiveClientInput(ProxyParameter);
}

// --------------------------------------------------------------


