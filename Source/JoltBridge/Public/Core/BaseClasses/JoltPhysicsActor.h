// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core/DataTypes/JoltBridgeTypes.h"
#include "GameFramework/Actor.h"
#include "JoltPhysicsActor.generated.h"

UCLASS()
class JOLTBRIDGE_API AJoltPhysicsActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AJoltPhysicsActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
};
