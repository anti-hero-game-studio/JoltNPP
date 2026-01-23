// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/BaseClasses/JoltStaticMeshActor.h"

#include "Core/BaseClasses/JoltStaticMeshComponent.h"


// Sets default values
AJoltStaticMeshActor::AJoltStaticMeshActor(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer.SetDefaultSubobjectClass(StaticMeshComponentName, UJoltStaticMeshComponent::StaticClass()))
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AJoltStaticMeshActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AJoltStaticMeshActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

