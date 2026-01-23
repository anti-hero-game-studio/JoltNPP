// Fill out your copyright notice in the Description page of Project Settings.


#include "Factories/JoltStaticMeshActorFactory.h"
#include "Core/BaseClasses/JoltStaticMeshActor.h"

UJoltStaticMeshActorFactory::UJoltStaticMeshActorFactory(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
	NewActorClass = AJoltStaticMeshActor::StaticClass();
}


bool UJoltStaticMeshActorFactory::CanCreateActorFrom( const FAssetData& AssetData, FText& OutErrorMsg )
{
	if ( !AssetData.IsValid() || !AssetData.IsInstanceOf( UStaticMesh::StaticClass() ) )
	{
		OutErrorMsg = NSLOCTEXT("CanCreateActor", "NoStaticMesh", "A valid static mesh must be specified.");
		return false;
	}

	return true;
}

void UJoltStaticMeshActorFactory::PostSpawnActor( UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	UStaticMesh* StaticMesh = CastChecked<UStaticMesh>(Asset);

	// Change properties
	AStaticMeshActor* StaticMeshActor = CastChecked<AStaticMeshActor>( NewActor );
	UStaticMeshComponent* StaticMeshComponent = StaticMeshActor->GetStaticMeshComponent();
	check(StaticMeshComponent);

	StaticMeshComponent->UnregisterComponent();

	StaticMeshComponent->SetStaticMesh(StaticMesh);
	if (StaticMesh->GetRenderData())
	{
		StaticMeshComponent->StaticMeshDerivedDataKey = StaticMesh->GetRenderData()->DerivedDataKey;
	}

	// Init Component
	StaticMeshComponent->RegisterComponent();
}

UObject* UJoltStaticMeshActorFactory::GetAssetFromActorInstance(AActor* Instance)
{
	check(Instance->IsA(NewActorClass));
	AStaticMeshActor* SMA = CastChecked<AStaticMeshActor>(Instance);

	check(SMA->GetStaticMeshComponent());
	return SMA->GetStaticMeshComponent()->GetStaticMesh();
}

FQuat UJoltStaticMeshActorFactory::AlignObjectToSurfaceNormal(const FVector& InSurfaceNormal, const FQuat& ActorRotation) const
{
	// Meshes align the Z (up) axis with the surface normal
	return FindActorAlignmentRotation(ActorRotation, FVector(0.f, 0.f, 1.f), InSurfaceNormal);
}
