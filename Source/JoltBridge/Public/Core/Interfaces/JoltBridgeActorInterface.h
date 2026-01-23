// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "JoltBridgeActorInterface.generated.h"

// This class does not need to be modified.
UINTERFACE()
class UJoltBridgeActorInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class JOLTBRIDGE_API IJoltBridgeActorInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	
	
	UFUNCTION(BlueprintNativeEvent, Category="Jolt Physics")
	UPrimitiveComponent* GetVisualProxyRootComponent() const;
	virtual UPrimitiveComponent* GetVisualProxyRootComponent_Implementation() const {return nullptr;};
};
