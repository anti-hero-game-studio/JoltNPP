// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Core/DataTypes/JoltBridgeTypes.h"
#include "JoltPrimitiveComponentInterface.generated.h"

// This class does not need to be modified.
UINTERFACE()
class UJoltPrimitiveComponentInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class JOLTBRIDGE_API IJoltPrimitiveComponentInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	
	virtual const FJoltBodyOptions& GetShapeOptions() const = 0;
	
	virtual FJoltBodyOptions& GetShapeOptions() = 0;
	
	virtual const FCollisionResponseContainer& GetDefaultResponseContainer() const = 0;
	//virtual ECollisionChannel GetDefaultCollisionChannel() const = 0;
};
