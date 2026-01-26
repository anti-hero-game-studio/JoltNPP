// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "Core/DataTypes/JoltBridgeTypes.h"
#include "Core/Interfaces/JoltPrimitiveComponentInterface.h"
#include "JoltSphereComponent.generated.h"


UCLASS(ClassGroup=(Jolt), meta=(BlueprintSpawnableComponent), PrioritizeCategories="Jolt Physics", 
	HideCategories=(Mobility, VirtualTexture, Physics))
class JOLTBRIDGE_API UJoltSphereComponent : public USphereComponent, public IJoltPrimitiveComponentInterface
{
	GENERATED_BODY()

public:
	UJoltSphereComponent(const FObjectInitializer& ObjectInitializer);
	virtual void InitializeComponent() override;
	
	
	virtual void SetSimulatePhysics(bool bSimulate) override;
	virtual bool IsSimulatingPhysics(FName BoneName = NAME_None) const override;
	virtual bool IsAnySimulatingPhysics() const override;
	virtual bool IsAnyRigidBodyAwake() override;
	
	virtual ECollisionEnabled::Type GetCollisionEnabled() const override;
	virtual ECollisionResponse GetCollisionResponseToChannel(ECollisionChannel Channel) const override;
	virtual const FCollisionResponseContainer& GetCollisionResponseToChannels() const override;
	

protected:
	// Called when the game starts
	virtual void BeginPlay() override;
	
	virtual bool UpdateOverlapsImpl(const TOverlapArrayView* PendingOverlaps = nullptr, bool bDoNotifies = true, const TOverlapArrayView* OverlapsAtEndLocation = nullptr) override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
	
	virtual FJoltPhysicsBodySettings& GetJoltPhysicsBodySettings() override {return ShapeOptions;};
	virtual const FJoltPhysicsBodySettings& GetJoltPhysicsBodySettings() const override { return ShapeOptions; };
	virtual const FCollisionResponseContainer& GetDefaultResponseContainer() const override { return BodyInstance.GetResponseToChannels();}
	
	
protected:
	
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category="Jolt Physics")
	FJoltPhysicsBodySettings ShapeOptions;
};
