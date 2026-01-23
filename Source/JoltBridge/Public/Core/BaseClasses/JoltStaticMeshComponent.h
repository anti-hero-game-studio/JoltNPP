// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Core/DataTypes/JoltBridgeTypes.h"
#include "Core/Interfaces/JoltPrimitiveComponentInterface.h"
#include "JoltStaticMeshComponent.generated.h"

/**
 * 
 */
UCLASS(ClassGroup=(Jolt), meta=(BlueprintSpawnableComponent), PrioritizeCategories="Jolt Physics", 
	HideCategories=(Mobility, VirtualTexture, Physics))
class JOLTBRIDGE_API UJoltStaticMeshComponent : public UStaticMeshComponent, public IJoltPrimitiveComponentInterface
{
	GENERATED_BODY()

public:

	UJoltStaticMeshComponent(const FObjectInitializer& ObjectInitializer);
	virtual void InitializeComponent() override;
	
	virtual void SetSimulatePhysics(bool bSimulate) override;
	virtual bool IsSimulatingPhysics(FName BoneName = NAME_None) const override;
	virtual bool IsAnySimulatingPhysics() const override;
	virtual bool IsAnyRigidBodyAwake() override;
	
	virtual ECollisionEnabled::Type GetCollisionEnabled() const override;
	virtual ECollisionResponse GetCollisionResponseToChannel(ECollisionChannel Channel) const override;
	virtual const FCollisionResponseContainer& GetCollisionResponseToChannels() const override;
	
	
	virtual FJoltBodyOptions& GetShapeOptions() override {return ShapeOptions;};
	virtual const FJoltBodyOptions& GetShapeOptions() const override { return ShapeOptions; };
	virtual const FCollisionResponseContainer& GetDefaultResponseContainer() const override { return BodyInstance.GetResponseToChannels();}
	
	
protected:
	
	virtual bool UpdateOverlapsImpl(const TOverlapArrayView* PendingOverlaps = nullptr, bool bDoNotifies = true, const TOverlapArrayView* OverlapsAtEndLocation = nullptr) override;
	
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category="Jolt Physics")
	FJoltBodyOptions ShapeOptions;
};
