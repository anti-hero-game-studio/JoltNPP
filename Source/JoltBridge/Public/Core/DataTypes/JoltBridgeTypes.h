// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "JoltBridgeTypes.generated.h"


USTRUCT()
struct FUnrealShape
{
	GENERATED_BODY()
	
	FUnrealShape()
	{
		
	}
	
	FUnrealShape(UPrimitiveComponent* NewPrimitive)
	{
		bIsRootComponent = false;
		Shape = NewPrimitive;
	}
	
	uint8 bIsRootComponent : 1 = false;
	uint32 Id = 0;
	TWeakObjectPtr<UPrimitiveComponent> Shape = nullptr;
	FCollisionResponseContainer CollisionResponses;
	float ShapeRadius = 0.f; // X
	float ShapeWidth = 0.f; // Y
	float ShapeHeight = 0.f;// Z
};


USTRUCT()
struct FUnrealShapeDescriptor
{
	GENERATED_BODY()
	
	FUnrealShapeDescriptor()
	{
		
	}
	
	TWeakObjectPtr<AActor> ShapeOwner = nullptr;
	
	TArray<FUnrealShape> Shapes;
	
	FCollisionResponseContainer CollisionResponseContainer = FCollisionResponseContainer();
	
	void Add(UPrimitiveComponent* C, const bool& bIsRoot)
	{
		Shapes.Add(FUnrealShape(C));
		Shapes.Last().bIsRootComponent = bIsRoot;
	}
	
	UPrimitiveComponent* GetRootComponent() const
	{
		for (const FUnrealShape& Shape : Shapes)
		{
			if (!Shape.bIsRootComponent) continue;
			
			return Shape.Shape.Get();
		}
		
		return nullptr;
	}
	
	uint32 GetRootColliderId() const
	{
		for (const FUnrealShape& Shape : Shapes)
		{
			if (!Shape.bIsRootComponent) continue;
			
			return Shape.Id;
		}
		
		return 0;
	}
	
	int GetColliderId(const UPrimitiveComponent* Target) const
	{
		for (const FUnrealShape& Shape : Shapes)
		{
			if (Shape.Shape != Target) continue;
			
			return Shape.Id;
		}
		
		return INDEX_NONE;
	}
	
	UPrimitiveComponent* FindClosestPrimitive(const FVector& Location) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FUnrealShapeDescriptor::FindClosestPrimitive);
		
		if (Shapes.Num() == 1)
		{
			return Shapes[0].Shape.Get();
		}
		
		float Distance = TNumericLimits<float>::Max();
		UPrimitiveComponent* NearestComponent = nullptr;
		for (const FUnrealShape& S : Shapes)
		{
			TWeakObjectPtr<UPrimitiveComponent> Shape = S.Shape;
			
			if (!Shape.Get()) continue;
			
			float CurrentDistance = 0.f;
			
			CurrentDistance = FVector::Distance(Shape.Get()->GetComponentLocation(), Location);

			if (CurrentDistance < Distance)
			{
				Distance = CurrentDistance;
				NearestComponent = Shape.Get();
			}
		}

		return NearestComponent;
	}
	
	int32 Find(const UPrimitiveComponent* T) const
	{
		for (const FUnrealShape& S : Shapes)
		{
			if (!S.Shape.Get()) continue;
			if (S.Shape.Get() != T) continue;
			return S.Id;
		}
		
		return INDEX_NONE;
	}
	
	const FCollisionResponseContainer& GetCollisionResponseContainer(const UPrimitiveComponent* Target) const
	{
		for (const FUnrealShape& S : Shapes)
		{
			if (!S.Shape.Get()) continue;
			if (S.Shape.Get() != Target) continue;
			return S.CollisionResponses;
		}
		
		return CollisionResponseContainer;
	}
	
};


UENUM(BlueprintType)
enum class EJoltShapeType : uint8
{
	STATIC = 0,
	DYNAMIC = 1,
	KINEMATIC = 2,
};

UENUM(BlueprintType)
enum class EGravityOverrideType : uint8
{
	NONE = 0,
	STATIC_VECTOR = 1,
	VECTOR_CURVE = 2 UMETA(Hidden), // TODO:@GreggoryAddison
	STATIC_FLOAT = 3,
	FLOAT_CURVE = 4,
};



USTRUCT(BlueprintType)
struct FJoltBodyOptions
{
	GENERATED_BODY()
	
	FJoltBodyOptions()
	{
		
	}
	
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	EJoltShapeType ShapeType = EJoltShapeType::STATIC;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bAutomaticallyActivate = false;
	
	/* Useful for player controlled bodies that should never be sent to sleep*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bCanBodyEverSleep = false;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bUsePhysicsMaterial = false;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bGenerateOverlapEventsInJolt = true;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bGenerateOverlapEventsInChaos = false;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bGenerateCollisionEventsInJolt = true;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bGenerateCollisionEventsInChaos = false;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bKeepShapeVertical = false;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(EditCondition="bUsePhysicsMaterial", EditConditionHides))
	TObjectPtr<UPhysicalMaterial> PhysMaterial;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(EditCondition="!bUsePhysicsMaterial", EditConditionHides))
	float Restitution = 1.f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(EditCondition="!bUsePhysicsMaterial", EditConditionHides))
	float Friction = 1.f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float Mass = 10.f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(EditCondition="ShapeType != EJoltShapeType::STATIC", EditConditionHides))
	EGravityOverrideType GravityOverrideType = EGravityOverrideType::NONE;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(EditCondition="GravityOverrideType == EGravityOverrideType::STATIC_VECTOR && ShapeType != EJoltShapeType::STATIC", EditConditionHides))
	FVector GravityOverride = FVector(0, 0, -980.f);
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(EditCondition="GravityOverrideType == EGravityOverrideType::STATIC_FLOAT && ShapeType != EJoltShapeType::STATIC", EditConditionHides), DisplayName="Gravity Scale")
	float GravityScale_Static = 1.0f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(EditCondition="GravityOverrideType == EGravityOverrideType::FLOAT_CURVE && ShapeType != EJoltShapeType::STATIC", EditConditionHides), DisplayName="Gravity Scale")
	TObjectPtr<UCurveFloat> GravityScale_Dynamic;;
	
};


struct FJoltUserData
{
	static constexpr uint32 MagicValue = 0xB011E7DA; // any constant you like

	uint32 Magic = MagicValue;

	// For hit construction/gameplay (not used by collision filtering)
	USceneComponent* Component = nullptr;
	AActor* OwnerActor = nullptr;
	UPhysicalMaterial* PhysMaterial = nullptr;
	
	float ShapeRadius = 1.f;
	float ShapeWidth = 1.f;
	float ShapeHeight = 1.f;

	float DefaultSlidingFriction = 0.f;
	float DefaultRollingFriction = 0.f;
	float DefaultSpinningFriction = 0.f;
	float DefaultRestitution = 1.f;

	// Collision policy data used in hot paths
	uint8  ObjectChannel = 0;    // 0..31 (ECollisionChannel as uint8)
	uint8  bQueryEnabled = 1;    // optional
	uint8  bPhysicsEnabled = 1;  // optional
	uint8  Pad = 0;

	uint32 BlockMask = 0;        // bits for channels this blocks
	uint32 OverlapMask = 0;      // bits for channels this overlaps (optional)c.)
	uint32 CombinedMask = 0;      // bits for channels this overlaps (optional)c.)
};
