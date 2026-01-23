// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/BaseClasses/JoltPhysicsActor.h"


// Sets default values
AJoltPhysicsActor::AJoltPhysicsActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AJoltPhysicsActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AJoltPhysicsActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

