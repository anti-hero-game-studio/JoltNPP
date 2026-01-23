// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ActorFactories/ActorFactoryStaticMesh.h"
#include "JoltStaticMeshActorFactory.generated.h"

/**
 * 
 */
UCLASS()
class JOLTEDITOR_API UJoltStaticMeshActorFactory : public UActorFactory
{
	GENERATED_BODY()
	
public:
	UJoltStaticMeshActorFactory(const FObjectInitializer& ObjectInitializer);
	
	
	//~ Begin UActorFactory Interface
	virtual bool CanCreateActorFrom( const FAssetData& AssetData, FText& OutErrorMsg ) override;
	virtual void PostSpawnActor( UObject* Asset, AActor* NewActor) override;
	virtual UObject* GetAssetFromActorInstance(AActor* ActorInstance) override;
	virtual FQuat AlignObjectToSurfaceNormal(const FVector& InSurfaceNormal, const FQuat& ActorRotation) const override;
	//~ End UActorFactory Interface
	
};
