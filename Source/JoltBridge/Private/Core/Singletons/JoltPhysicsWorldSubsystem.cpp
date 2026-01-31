// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/Singletons/JoltPhysicsWorldSubsystem.h"
#include "Core/DataTypes/JoltBridgeTypes.h"
#include "JoltBridgeCoreSettings.h"
#include "JoltBridgeLogChannels.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Core/Collision/JoltCallBackContactListener.h"
#include "Core/Collision/Collectors/RaycastCollector_AllHits.h"
#include "Core/Collision/Collectors/RaycastCollector_Single.h"
#include "Core/Collision/Collectors/SweepCastCollector_AllHits.h"
#include "Core/Collision/Collectors/SweepCastCollector_Single.h"
#include "Core/CollisionFilters/JoltFilters.h"
#include "Core/CollisionFilters/UnrealGroupFilter.h"
#include "Core/Debug/JoltDebugRenderer.h"
#include "Core/Interfaces/JoltPrimitiveComponentInterface.h"
#include "Core/Simulation/JoltWorker.h"
#include "GameFramework/PhysicsVolume.h"
#include "Jolt/Physics/Constraints/TwoBodyConstraint.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConvexElem.h"

int32 DrawDebugShapes = 0;
static FAutoConsoleVariableRef CVarDrawDebugShapes(
	TEXT("j.debug.draw.shapes"),
	DrawDebugShapes,
	TEXT("Show the jolt collision Shapes according to the jolt world view"),
	ECVF_Default);

float DrawDebugTraces = 0;
static FAutoConsoleVariableRef CVarDrawDebugTraces(
	TEXT("j.debug.draw.traces"),
	DrawDebugTraces,
	TEXT("Show the jolt trace queries according to the jolt world view. The value you enter is also the amount of time the traces will be drawn"),
	ECVF_Default);


const FVector UE_WORLD_ORIGIN = FVector(0);

void UJoltPhysicsWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	if (GetWorld() == nullptr) {
		UE_LOG(LogTemp, Warning, TEXT("UJoltPhysicsWorldSubsystem::GetWorld() returned null"));
		return;
	}
	
	JPH::Trace = JoltHelpers::UnrealTrace;
#ifdef JPH_ENABLE_ASSERTS
	JPH::AssertFailed = JoltHelpers::UEAssertFailed;
#endif
	JPH::RegisterDefaultAllocator();
	JPH::Factory::sInstance = new JPH::Factory();
	JPH::RegisterTypes();
	JoltSettings = GetDefault<UJoltSettings>();
	StaticBodyIDX = JoltSettings->StaticBodyIDStart;
	DynamicBodyIDX = JoltSettings->DynamicBodyIDStart;
	InitPhysicsSystem(JoltSettings->MaxBodies, JoltSettings->NumBodyMutexes, JoltSettings->MaxBodyPairs, JoltSettings->MaxContactConstraints);
	UE_LOG(LogTemp, Warning, TEXT("UJoltPhysicsWorldSubsystem:: JoltBridge world init"));

	
}

void UJoltPhysicsWorldSubsystem::InitPhysicsSystem(
	int cMaxBodies,
	int cNumBodyMutexes,
	int cMaxBodyPairs,
	int cMaxContactConstraints)
{

#ifdef JPH_DEBUG_RENDERER
	DrawSettings = new JPH::BodyManager::DrawSettings;
	DrawSettings->mDrawShape = true;		// Draw the shapes of the bodies
	DrawSettings->mDrawBoundingBox = false; // Optionally, draw bounding boxes
	DrawSettings->mDrawShapeWireframe = false;
	DrawSettings->mDrawWorldTransform = true;
	// DrawSettings->mDrawShapeWireframe
#endif

	BroadPhaseLayerInterface = new FBroadPhaseLayerInterfaceImpl;
	// Create class that filters object vs broadphase layers
	// Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
	ObjectVsBroadphaseLayerFilter = new ObjectVsBroadPhaseLayerFilterImpl;

	// Create class that filters object vs object layers
	// Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
	ObjectVsObjectLayerFilter = new ObjectLayerPairFilterImpl;

	MainPhysicsSystem = new JPH::PhysicsSystem;

#ifdef JPH_DEBUG_RENDERER
	JoltDebugRendererImpl = new FJoltDebugRenderer(GetWorld());
#endif
	// Jolt uses Y axis as the up direction, and unreal uses the Z axis. So, set gravity for Y
	
	MainPhysicsSystem->SetGravity(JoltHelpers::ToJoltVector3(JoltSettings->WorldGravityAcceleration)); 
	MainPhysicsSystem->Init(
		cMaxBodies,
		cNumBodyMutexes,
		cMaxBodyPairs,
		cMaxContactConstraints,
		*BroadPhaseLayerInterface,
		*ObjectVsBroadphaseLayerFilter,
		*ObjectVsObjectLayerFilter);

	BodyInterface = &MainPhysicsSystem->GetBodyInterface();
	ContactListener = new FJoltCallBackContactListener();
	MainPhysicsSystem->SetContactListener(ContactListener);
	// Spawn jolt worker
	UE_LOG(LogJoltBridge, Log, TEXT("Jolt subsystem init complete"));
}


void UJoltPhysicsWorldSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	CleanUpJoltBridgeWorld();
	Super::OnWorldEndPlay(InWorld);
}

void UJoltPhysicsWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	
	Super::OnWorldBeginPlay(InWorld);
	
	UE_LOG(LogJoltBridge, Log, TEXT("Jolt worker running "));
	AddAllJoltActors(GetWorld());
	

	// We were adding bodies one by one above, so need to call this.
	// TODO: need to look into adding bodies as a batch, as recommended by jolt
	// https://jrouwe.github.io/JoltPhysics/#creating-bodies
	MainPhysicsSystem->OptimizeBroadPhase();

	WorkerOptions = new FJoltWorkerOptions(
		MainPhysicsSystem,
		JoltSettings->MaxPhysicsJobs,
		JoltSettings->MaxPhysicsBarriers,
		JoltSettings->MaxThreads,
		JoltSettings->FixedDeltaTime,
		JoltSettings->InCollisionSteps,
		JoltSettings->PreAllocatedMemory,
		JoltSettings->bEnableMultithreading);

	JoltWorker = new FJoltWorker(WorkerOptions);
	
}


void UJoltPhysicsWorldSubsystem::RegisterJoltRigidBody(AActor* Target)
{
	FUnrealShapeDescriptor Descriptor = GlobalShapeDescriptorDataCache.Contains(Target) ? GlobalShapeDescriptorDataCache[Target] : FUnrealShapeDescriptor();
	Descriptor.ShapeOwner = Target;
	
	ExtractPhysicsGeometry(Target,[Target, this, &Descriptor](const JPH::Shape* Shape, const FTransform& RelTransform, const FJoltPhysicsBodySettings& Options)
	{
		// Every sub-collider in the Actor is passed to this callback function
		// We're baking this in world space, so apply Actor transform to relative
		const FTransform FinalXform = RelTransform;
		FJoltUserData* UserData = AllocUserData();

		if (UPrimitiveComponent* P = Descriptor.Shapes.Last().Shape.Get())
		{
			const FUnrealShape& UnrealShape = Descriptor.Shapes.Last();
			IJoltPrimitiveComponentInterface* I = Cast<IJoltPrimitiveComponentInterface>(P);
			if (!I) return;
			
			const FCollisionResponseContainer& ResponseContainer = I->GetDefaultResponseContainer();
			
			JoltHelpers::BuildResponseMasks(ResponseContainer, UserData->BlockMask, UserData->OverlapMask, UserData->CombinedMask);
			UserData->ObjectChannel = (uint8)P->GetCollisionObjectType();
			
			UserData->DefaultRestitution = Options.GetDesiredRestitution(); 
			UserData->DefaultSlidingFriction = Options.GetDesiredFriction(); 
			UserData->ShapeRadius = UnrealShape.ShapeRadius;
			UserData->ShapeHeight = UnrealShape.ShapeHeight;
			UserData->ShapeWidth = UnrealShape.ShapeWidth;
			UserData->OwnerActor = Target;
			UserData->PhysMaterial = Options.bUsePhysicsMaterial ? Options.PhysMaterial : nullptr;
			UserData->bGenerateOverlapEvents = Options.bGenerateOverlapEventsInJolt;
			UserData->bGenerateHitEvents = Options.bGenerateCollisionEventsInJolt;
			
			if (!Options.bGenerateCollisionEventsInChaos)
			{
				P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				P->GetBodyInstance()->bNotifyRigidBodyCollision = false;
				P->SetShouldUpdatePhysicsVolume(false);
			}
			
			if (!Options.bGenerateOverlapEventsInChaos)
			{
				P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				P->SetGenerateOverlapEvents(false);
				P->SetShouldUpdatePhysicsVolume(false);
				if (AActor* A = P->GetOwner())
				{
					A->bGenerateOverlapEventsDuringLevelStreaming = false;
				}
			}

			P->SetCanEverAffectNavigation(Options.bCanBodyEverAffectNavigation);
			
			//TODO:@GreggoryAddison::CodeOptimization || If both bGenerateOverlapEventsInChaos && bGenerateCollisionEventsInChaos destroy the Chaos FBodyInstance.
			
			UserData->Component = P;
			Descriptor.Shapes.Last().CollisionResponses = ResponseContainer;
		}
		
		if (Options.ShapeType == EJoltShapeType::DYNAMIC || Options.ShapeType == EJoltShapeType::KINEMATIC)
		{
			if (JPH::Body* CollisionObject = AddRigidBodyCollider(Target, RelTransform, Shape, Options, UserData))
			{
				Descriptor.Shapes.Last().Id = CollisionObject->GetID().GetIndexAndSequenceNumber();
			}
			
			GlobalShapeDescriptorDataCache.Add(Target, Descriptor);
			return;
		}
		
		// For now all sensors will be static bodies
		if (JPH::Body* CollisionObject = AddStaticCollider(Shape, FinalXform, Options, UserData))
		{
			Descriptor.Shapes.Last().Id = CollisionObject->GetID().GetIndexAndSequenceNumber();
		}
			
		GlobalShapeDescriptorDataCache.Add(Target, Descriptor);
		
	}, Descriptor);
}

void UJoltPhysicsWorldSubsystem::RegisterJoltCharacter(const APawn* Target, const JPH::CharacterVirtualSettings& Settings, uint32& CharacterId)
{
	if (!Target) return;
	const FTransform ownTrans = Target->GetTransform();
	JPH::CharacterVirtual* m_pCharacter = new JPH::CharacterVirtual(&Settings, JoltHelpers::ToJoltPosition(ownTrans.GetLocation()),JoltHelpers::ToJoltRotation(ownTrans.GetRotation()), MainPhysicsSystem);
	m_pCharacter->AddRef();
	
	CharacterId = m_pCharacter->GetID().GetValue();
	VirtualCharacterMap.Add(m_pCharacter->GetID().GetValue(), m_pCharacter);
}

JPH::CharacterVirtual* UJoltPhysicsWorldSubsystem::GetCharacterFromId(const uint32& CharacterId) const
{
	if (VirtualCharacterMap.Contains(CharacterId))
	{
		return VirtualCharacterMap[CharacterId];
	}
	
	return nullptr;
}

void UJoltPhysicsWorldSubsystem::K2_SetPhysicsState(const UPrimitiveComponent* Target, const FTransform& Transforms, const FVector& Velocity, const FVector& AngularVelocity)
{
	SetPhysicsState(Target, Transforms, Velocity, AngularVelocity);
}


void UJoltPhysicsWorldSubsystem::DrawDebugLines() const
{
	if (MainPhysicsSystem == nullptr || DrawSettings == nullptr || JoltDebugRendererImpl == nullptr)
	{
		UE_LOG(LogJoltBridge, Warning, TEXT("Debug renderer disabled"));
		return;
	}
	MainPhysicsSystem->DrawBodies(*DrawSettings, JoltDebugRendererImpl);
}

const JPH::BoxShape* UJoltPhysicsWorldSubsystem::GetBoxCollisionShape(const FVector& Dimensions, const JoltPhysicsMaterial* Material)
{
	
	// Simple brute force lookup for now, probably doesn't need anything more clever
	JPH::Vec3 HalfSize = JoltHelpers::ToJoltVector3(Dimensions * 0.5);
	for (const JPH::BoxShape*& S : BoxShapes)
	{
		JPH::Vec3 Sz = S->GetHalfExtent();

		if (!FMath::IsNearlyEqual(Sz.GetX(), HalfSize.GetX()) || !FMath::IsNearlyEqual(Sz.GetY(), HalfSize.GetY()) || !FMath::IsNearlyEqual(Sz.GetZ(), HalfSize.GetZ()))
		{
			continue;
		}

		// Material check (if Material specified)
		if (Material && S->GetMaterial() != Material)
		{
			continue;
		}

		return S;
	}

	// Not found, create
	JPH::Ref<JPH::BoxShape> S = new JPH::BoxShape(HalfSize);
	S->AddRef();
	S->SetMaterial(Material);
	BoxShapes.Add(S);
	return S;

}

const JPH::SphereShape* UJoltPhysicsWorldSubsystem::GetSphereCollisionShape(float Radius, const JoltPhysicsMaterial* Material)
{
	// Simple brute force lookup for now, probably doesn't need anything more clever
	float Rad = JoltHelpers::ToJoltFloat(Radius);

	for (const JPH::SphereShape*& S : SphereShapes)
	{
		if (!FMath::IsNearlyEqual(S->GetRadius(), Rad))
		{
			continue;
		}

		if (Material && S->GetMaterial() != Material)
		{
			continue;
		}

		return S;
	}

	// Not found, create
	JPH::Ref<JPH::SphereShape> S = new JPH::SphereShape(Rad);
	S->AddRef();
	S->SetMaterial(Material);
	SphereShapes.Add(S);

	return S;

}

const JPH::CapsuleShape* UJoltPhysicsWorldSubsystem::GetCapsuleCollisionShape(float Radius, float Height, const JoltPhysicsMaterial* Material)
{
	// Simple brute force lookup for now, probably doesn't need anything more clever
	float R = JoltHelpers::ToJoltFloat(Radius);
	float H = JoltHelpers::ToJoltFloat(Height);
	float HalfH = H * 0.5f;

	for (const JPH::CapsuleShape*& S : CapsuleShapes)
	{
		if (!FMath::IsNearlyEqual(S->GetRadius(), R) || !FMath::IsNearlyEqual(S->GetHalfHeightOfCylinder(), HalfH))
		{
			continue;
		}

		if (Material && S->GetMaterial() != Material)
		{
			continue;
		}

		return S;
	}

	JPH::Ref<JPH::CapsuleShape> capsule = new JPH::CapsuleShape(HalfH, R);
	capsule->AddRef();
	capsule->SetMaterial(Material);
	CapsuleShapes.Add(capsule);

	return capsule;

}

const JPH::ConvexHullShape* UJoltPhysicsWorldSubsystem::GetConvexHullCollisionShape(UBodySetup* BodySetup, int ConvexIndex, const FVector& Scale, const JoltPhysicsMaterial* Material)
{
	for (const ConvexHullShapeHolder& S : ConvexShapes)
	{
		if (S.BodySetup != BodySetup || S.HullIndex != ConvexIndex)
		{
			continue;
		}

		if (!S.Scale.Equals(Scale))
		{
			continue;
		}

		if (Material && S.Shape->GetMaterial() != Material)
		{
			continue;
		}

		return S.Shape;
	}

	const FKConvexElem&	  Elem = BodySetup->AggGeom.ConvexElems[ConvexIndex];
	JPH::Array<JPH::Vec3> points;
	for (const FVector& P : Elem.VertexData)
	{
		points.push_back(JoltHelpers::ToJoltVector3(P * Scale));
	}

	JPH::ConvexHullShapeSettings val(points);
	JPH::Shape::ShapeResult		 result;

	JPH::Ref<JPH::ConvexHullShape> Shape = new JPH::ConvexHullShape(val, result);
	Shape->AddRef();
	Shape->SetMaterial(Material);

	ConvexShapes.Add(ConvexHullShapeHolder{ BodySetup, ConvexIndex, Scale, Shape });
	return Shape;
}

void UJoltPhysicsWorldSubsystem::CleanUpJoltBridgeWorld()
{
	if (!MainPhysicsSystem)
	{
		return;
	}
	
	MainPhysicsSystem->SetContactListener(nullptr);
	
	JPH::BodyIDVector Ids;
	MainPhysicsSystem->GetBodies(Ids);
	
	for (int i = Ids.size() - 1; i >= 0; i--)
	{
		MainPhysicsSystem->GetBodyInterfaceNoLock().RemoveBody(Ids[i]);
		MainPhysicsSystem->GetBodyInterface().DestroyBody(Ids[i]);
	}

	for (TTuple<unsigned, JPH::CharacterVirtual*> C : VirtualCharacterMap)
	{
		delete C.Value;
	}
	
	VirtualCharacterMap.Empty();
	JPH::CharacterID::sSetNextCharacterID();
	
	// delete collision Shapes
	for (int i = 0; i < BoxShapes.Num(); i++)
		delete BoxShapes[i];
	BoxShapes.Empty();
	for (int i = 0; i < SphereShapes.Num(); i++)
		delete SphereShapes[i];
	SphereShapes.Empty();
	for (int i = 0; i < CapsuleShapes.Num(); i++)
		delete CapsuleShapes[i];
	CapsuleShapes.Empty();
	for (int i = 0; i < ConvexShapes.Num(); i++)
		delete ConvexShapes[i].Shape;
	ConvexShapes.Empty();
	
	UserDataStore.Empty();
	

	delete JoltWorker;
	

#ifdef JPH_DEBUG_RENDERER
	delete JoltDebugRendererImpl;
	delete DrawSettings;
#endif
	

	MainPhysicsSystem = nullptr;
	WorkerOptions = nullptr;
	BodyInterface = nullptr;
	ContactListener = nullptr;
	JoltWorker = nullptr;
	UEGroupFilter = nullptr;	
	
#ifdef JPH_DEBUG_RENDERER
	JoltDebugRendererImpl = nullptr;
	DrawSettings = nullptr;
#endif
	

	// Clear our type-specific arrays (duplicate refs)
	BodyIDBodyMap.Empty();
}

FUnrealShapeDescriptor UJoltPhysicsWorldSubsystem::GetShapeDescriptorData(const AActor* Actor) const
{
	if (!Actor) return FUnrealShapeDescriptor();
	if (GlobalShapeDescriptorDataCache.IsEmpty()) return FUnrealShapeDescriptor();
	if (!GlobalShapeDescriptorDataCache.Contains(Actor)) return FUnrealShapeDescriptor();
	
	return GlobalShapeDescriptorDataCache[Actor];
}

int32 UJoltPhysicsWorldSubsystem::GetActorRootShapeId(const AActor* Actor) const
{
	if (!Actor) return INDEX_NONE;
	if (GlobalShapeDescriptorDataCache.IsEmpty()) return INDEX_NONE;
	if (!GlobalShapeDescriptorDataCache.Contains(Actor)) return INDEX_NONE;
	
	return GlobalShapeDescriptorDataCache[Actor].GetRootColliderId(); 
}

int32 UJoltPhysicsWorldSubsystem::FindShapeId(const UPrimitiveComponent* Target) const
{
	if (!Target) return INDEX_NONE;
	if (!IsBodyValid(Target)) return INDEX_NONE;
	
	const int32 BlockingId =  GlobalShapeDescriptorDataCache[Target->GetOwner()].Find(Target);
	return BlockingId != INDEX_NONE ? BlockingId : GlobalShapeDescriptorDataCache[Target->GetOwner()].Find(Target); 
}

bool UJoltPhysicsWorldSubsystem::IsBodyValid(const UPrimitiveComponent* Target) const
{
	if (!Target) return false;
	
	if (!Target->GetOwner()) return false;
	
	if (GlobalShapeDescriptorDataCache.IsEmpty()) return false;
	
	if (!GlobalShapeDescriptorDataCache.Contains(Target->GetOwner())) return false;
	
	return true;
}

bool UJoltPhysicsWorldSubsystem::HasRigidBodyBeenCreated(const UPrimitiveComponent* Target) const
{
	if (!IsBodyValid(Target)) return false;
	
	const FUnrealShapeDescriptor& Desc = GlobalShapeDescriptorDataCache[Target->GetOwner()];

	const JPH::BodyID ID(Desc.Find(Target));
	
	return BodyInterface->IsAdded(ID);
}

bool UJoltPhysicsWorldSubsystem::HasSensorBodyBeenCreated(const UPrimitiveComponent* Target) const
{
	if (!IsBodyValid(Target)) return false;
	
	const FUnrealShapeDescriptor& Desc = GlobalShapeDescriptorDataCache[Target->GetOwner()];

	const JPH::BodyID ID(Desc.Find(Target));
	
	return BodyInterface->IsAdded(ID) && BodyInterface->IsSensor(ID);
}

bool UJoltPhysicsWorldSubsystem::IsCollisionBodyActive(const UPrimitiveComponent* Target) const
{
	if (!IsBodyValid(Target)) return false;
	
	const FUnrealShapeDescriptor& Desc = GlobalShapeDescriptorDataCache[Target->GetOwner()];

	const JPH::BodyID ID(Desc.Find(Target));
	
	return BodyInterface->IsActive(ID);
}


void UJoltPhysicsWorldSubsystem::SetRigidBodyActiveState(const UPrimitiveComponent* Target, const bool Active) const
{
	if (!IsBodyValid(Target)) return;
	
	const FUnrealShapeDescriptor& Desc = GlobalShapeDescriptorDataCache[Target->GetOwner()];

	const JPH::BodyID ID(Desc.Find(Target));
	
	if (Active)
	{
		BodyInterface->ActivateBody(ID);
	}
	else
	{
		BodyInterface->DeactivateBody(ID);
	}
}

const FCollisionResponseContainer& UJoltPhysicsWorldSubsystem::GetCollisionResponseContainer(const UPrimitiveComponent* Target) const
{
	if (!IsBodyValid(Target)) return DefaultCollisionResponseContainer;
	
	const FUnrealShapeDescriptor& Desc = GlobalShapeDescriptorDataCache[Target->GetOwner()];
	
	return Desc.GetCollisionResponseContainer(Target); 
}

UPrimitiveComponent* UJoltPhysicsWorldSubsystem::GetPrimitiveComponent(const uint32& Id) const
{
	for (const TTuple<TWeakObjectPtr<AActor>, FUnrealShapeDescriptor>& Cache : GlobalShapeDescriptorDataCache)
	{
		UPrimitiveComponent* P = Cache.Value.Find(Id);
		if (P != nullptr)
		{
			return P;
		}
	}
	
	return nullptr;
}

UPrimitiveComponent* UJoltPhysicsWorldSubsystem::GetPrimitiveComponent(const uint64& UserDataPtr)
{
	const FJoltUserData* D = reinterpret_cast<const FJoltUserData*>(UserDataPtr);
	if (!D) return nullptr;
	return Cast<UPrimitiveComponent>(D->Component);
}

JPH::Body* UJoltPhysicsWorldSubsystem::GetRigidBody(const FHitResult& Hit) const
{
	if (!Hit.GetComponent()) return nullptr;
	const int32 BodyId = FindShapeId(Hit.GetComponent());
	if (BodyId == INDEX_NONE) return nullptr;
	
	return GetBody(BodyId);
}

JPH::Body* UJoltPhysicsWorldSubsystem::GetRigidBody(const UPrimitiveComponent* Target) const
{
	if (!Target) return nullptr;
	const int32 BodyId = FindShapeId(Target);
	if (BodyId == INDEX_NONE) return nullptr;
	return GetBody(BodyId);
}

const FJoltUserData* UJoltPhysicsWorldSubsystem::GetUserData(const UPrimitiveComponent* Target) const
{
	const int32 ID = FindShapeId(Target);
	if (ID == INDEX_NONE) return nullptr;

	const uint64 Data = BodyInterface->GetUserData(JPH::BodyID(ID));
	
	return reinterpret_cast<const FJoltUserData*>(Data);
}

const FJoltUserData* UJoltPhysicsWorldSubsystem::GetUserData(const uint64& UserDataPtr)
{
	return reinterpret_cast<const FJoltUserData*>(UserDataPtr);
}

JPH::Body* UJoltPhysicsWorldSubsystem::AddRigidBodyCollider(AActor* Actor, const FTransform& FinalTransform, const JPH::Shape* Shape,  const FJoltPhysicsBodySettings& Options, const FJoltUserData* UserData)
{
	JPH::BodyCreationSettings shapeSettings = MakeBodyCreationSettings(Shape, FinalTransform, Options, UserData);

	DynamicBodyIDX++;
	JPH::BodyID* bodyID = new JPH::BodyID(DynamicBodyIDX);
	return AddBodyToSimulation(bodyID, shapeSettings, Options, UserData);
}

JPH::Body* UJoltPhysicsWorldSubsystem::AddRigidBodyCollider(USkeletalMeshComponent* Skel, const FTransform& PhysicsAssetTransform, const JPH::Shape* CollisionShape, const FJoltPhysicsBodySettings& Options, const FJoltUserData* UserData)
{
	return nullptr;
}

JPH::Body* UJoltPhysicsWorldSubsystem::AddStaticCollider(const JPH::Shape* Shape, const FTransform& Transform, const FJoltPhysicsBodySettings& Options, const FJoltUserData* UserData)
{
	check(Shape != nullptr);
	JPH::BodyCreationSettings shapeSettings = MakeBodyCreationSettings(Shape, Transform, Options, UserData);
	

	StaticBodyIDX++;
	JPH::BodyID* bodyID = new JPH::BodyID(StaticBodyIDX);
	return AddBodyToSimulation(bodyID, shapeSettings, Options, UserData);
}

JPH::Body* UJoltPhysicsWorldSubsystem::AddBodyToSimulation(const JPH::BodyID* BodyID, const JPH::BodyCreationSettings& ShapeSettings, const FJoltPhysicsBodySettings& Options, const FJoltUserData* UserData)
{
	check(BodyInterface != nullptr);
	check(BodyID != nullptr);
	JPH::Body* createdBody = BodyInterface->CreateBodyWithID(*BodyID, ShapeSettings);
	if (createdBody == nullptr)
	{
		UE_LOG(LogJoltBridge, Error, TEXT("failed to create %s body with ID: %d"), *JoltHelpers::EMotionTypeToString(ShapeSettings.mMotionType), BodyID->GetIndexAndSequenceNumber());
		return nullptr;
	}
	createdBody->SetRestitution(Options.Restitution);
	createdBody->SetFriction(Options.Friction);
	createdBody->SetUserData(reinterpret_cast<uint64>(UserData));

	BodyIDBodyMap.Add(createdBody->GetID().GetIndexAndSequenceNumber(), createdBody);
	BodyInterface->AddBody(createdBody->GetID(), Options.bAutomaticallyActivate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	return createdBody;
}

JPH::BodyCreationSettings UJoltPhysicsWorldSubsystem::MakeBodyCreationSettings(const JPH::Shape* Shape, const FTransform& T,const FJoltPhysicsBodySettings& Options, const FJoltUserData* UserData)
{
	check(Shape != nullptr);
	
	
	JPH::EMotionType MotionType = JPH::EMotionType::Static;
	JPH::ObjectLayer Layer = Layers::NON_MOVING;
	switch (Options.ShapeType)
	{
	case EJoltShapeType::STATIC:
		MotionType = JPH::EMotionType::Static;
		break;
	case EJoltShapeType::DYNAMIC:
		MotionType = JPH::EMotionType::Dynamic;
		Layer = Layers::MOVING;
		break;
	case EJoltShapeType::KINEMATIC:
		MotionType = JPH::EMotionType::Kinematic;
		Layer = Layers::MOVING;
		break;
	}
	
	
	JPH::BodyCreationSettings ShapeSettings(
		Shape,
		JoltHelpers::ToJoltPosition(T.GetLocation()),
		JoltHelpers::ToJoltRotation(T.GetRotation()),
		MotionType,
		Layer);
	
	
	ShapeSettings.mAllowSleeping = Options.bCanBodyEverSleep;

	if (Options.ShapeType == EJoltShapeType::DYNAMIC)
	{
		JPH::MassProperties msp;
		msp.ScaleToMass(Options.Mass);
		ShapeSettings.mMassPropertiesOverride = msp;
		ShapeSettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
		
		if (Options.bKeepShapeVertical)
		{
			ShapeSettings.mAllowedDOFs = JPH::EAllowedDOFs::RotationY | JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
		}
	}

	if (Options.bGenerateOverlapEventsInJolt && !Options.bGenerateCollisionEventsInJolt)
	{
		ShapeSettings.mIsSensor = true;
	}

	if (Options.GravityOverrideType == EGravityOverrideType::FROM_MOVER)
	{
		// Velocity will come directly from the mover component.
		ShapeSettings.mGravityFactor = 0;
		ShapeSettings.mLinearDamping = 0.f;
		ShapeSettings.mAngularDamping = 0.f;
	}
	
	
	// In your subsystem (lifetime >= bodies):
	if (!UEGroupFilter)
	{
		UEGroupFilter = new FUnrealGroupFilter();
	};
	
	uint32 Lo, Hi;
	JoltHelpers::PackDataToGroupIDs(UserData, Lo, Hi);

	JPH::CollisionGroup CG;
	CG.SetGroupFilter(UEGroupFilter);
	CG.SetGroupID(Lo);
	CG.SetSubGroupID(Hi);
	
	ShapeSettings.mCollisionGroup = CG;

	// TODO:@GreggoryAddison::CodeCompletion || Figure out how to handle an object that can be both a collider and a sensor
	// This will require using the overlap and collision masks to make a filter.
	
	
	
	return ShapeSettings;
}

void UJoltPhysicsWorldSubsystem::SetPhysicsState(const UPrimitiveComponent* Target, const FTransform& Transforms, const FVector& Velocity, const FVector& AngularVelocity) const
{
	const int32& ShapeId = FindShapeId(Target);
	if (ShapeId == INDEX_NONE) return;
	
	const JPH::BodyID ID(ShapeId);
	
	BodyInterface->SetPositionRotationAndVelocity
	(
		ID, 
		JoltHelpers::ToJoltPosition(Transforms.GetLocation()),
		JoltHelpers::ToJoltRotation(Transforms.GetRotation()), 
		JoltHelpers::ToJoltVector3(Velocity),
		JoltHelpers::ToJoltVector3(JoltHelpers::DegreesPerSecToRadiansPerSec(AngularVelocity))
	);
}

void UJoltPhysicsWorldSubsystem::GetPhysicsState(const UPrimitiveComponent* Target, FTransform& Transforms, FVector& Velocity, FVector& AngularVelocity,FVector& Force)
{
	if (!IsBodyValid(Target)) return;
	
	const int32& ShapeId = FindShapeId(Target);
	
	if (ShapeId == INDEX_NONE) return;
	
	const JPH::BodyID ID(ShapeId);
	JPH::Vec3 OutLinearVelocity, OutAngularVelocity;
	
	BodyInterface->GetLinearAndAngularVelocity(ID, OutLinearVelocity, OutAngularVelocity);
	AngularVelocity = JoltHelpers::ToUnrealVector3(OutAngularVelocity);
	Velocity = JoltHelpers::ToUnrealVector3(OutLinearVelocity);
	Transforms = JoltHelpers::ToUnrealTransform(BodyInterface->GetCenterOfMassTransform(ID));
	
	
}

bool UJoltPhysicsWorldSubsystem::BroadcastPendingAddedContactEvents()
{
	FContactAddedInfo ContactInfo;
	while (ContactListener && ContactListener->ConsumeAddedContacts(ContactInfo))
	{
		if (!BodyIDBodyMap.Contains(ContactInfo.BodyID1) || !BodyIDBodyMap.Contains(ContactInfo.BodyID2)) return true;
		if (ContactInfo.bIsOverlap)
		{
			const FJoltUserData* UD1 = GetUserData(BodyIDBodyMap[ContactInfo.BodyID1]->GetUserData());
			const FJoltUserData* UD2 = GetUserData(BodyIDBodyMap[ContactInfo.BodyID2]->GetUserData());
			if (!UD1 || !UD2) return true;
			
			UPrimitiveComponent* P1 = Cast<UPrimitiveComponent>(UD1->Component);
			UPrimitiveComponent* P2 = Cast<UPrimitiveComponent>(UD2->Component);
			
			if (!P1 || !P2) return true;

			if (UD1->bGenerateOverlapEvents && P1->OnComponentBeginOverlap.IsBound())
			{
				P1->OnComponentBeginOverlap.Broadcast(P1, P2->GetOwner(), P2, ContactInfo.BodyID2, false, FHitResult(NoInit));
			}
			
			if (UD2->bGenerateOverlapEvents && P2->OnComponentBeginOverlap.IsBound())
			{
				P2->OnComponentBeginOverlap.Broadcast(P2, P1->GetOwner(), P1, ContactInfo.BodyID1, false, FHitResult(NoInit));
			}
		}
		else
		{
			const FJoltUserData* UD1 = GetUserData(BodyIDBodyMap[ContactInfo.BodyID1]->GetUserData());
			const FJoltUserData* UD2 = GetUserData(BodyIDBodyMap[ContactInfo.BodyID2]->GetUserData());
			if (!UD1 || !UD2) return true;
			
			UPrimitiveComponent* P1 = Cast<UPrimitiveComponent>(UD1->Component);
			UPrimitiveComponent* P2 = Cast<UPrimitiveComponent>(UD2->Component);
			
			if (!P1 || !P2) return true;
			
			const FVector Impulse = ContactInfo.NormalDir * ContactInfo.NormalImpulse;
			FHitResult Hit(NoInit);
			if (UD1->bGenerateHitEvents && P1->OnComponentHit.IsBound())
			{
				
				Hit.bBlockingHit = true;
				Hit.Component = P2;
				Hit.HitObjectHandle = P2->GetOwner();
				Hit.Location = ContactInfo.BodyID1ContactLocation;
				Hit.ImpactPoint = ContactInfo.BodyID1ContactLocation;
				Hit.Distance = FVector::Distance(ContactInfo.BodyID1ContactLocation, ContactInfo.BodyID2ContactLocation);
				Hit.Normal = ContactInfo.NormalDir;
				Hit.ImpactNormal = ContactInfo.NormalDir;
				P1->OnComponentHit.Broadcast(P1, P2->GetOwner(), P2, Impulse, Hit);
			}
			
			if (UD2->bGenerateHitEvents && P2->OnComponentHit.IsBound())
			{
				Hit.bBlockingHit = true;
				Hit.Component = P1;
				Hit.HitObjectHandle = P1->GetOwner();
				Hit.Location = ContactInfo.BodyID2ContactLocation;
				Hit.ImpactPoint = ContactInfo.BodyID2ContactLocation;
				Hit.Distance = FVector::Distance(ContactInfo.BodyID1ContactLocation, ContactInfo.BodyID2ContactLocation);
				Hit.Normal = ContactInfo.NormalDir;
				Hit.ImpactNormal = ContactInfo.NormalDir;
				P2->OnComponentHit.Broadcast(P2, P1->GetOwner(), P1, Impulse, Hit);
			}
		}
	}
	return false;
}

bool UJoltPhysicsWorldSubsystem::BroadcastPendingRemovedContactEvents()
{
	FContactRemovedInfo ContactInfo;
	while (ContactListener && ContactListener->ConsumeRemovedContacts(ContactInfo))
	{
		if (!BodyIDBodyMap.Contains(ContactInfo.BodyID1) || !BodyIDBodyMap.Contains(ContactInfo.BodyID2)) return false;
		const FJoltUserData* UD1 = GetUserData(BodyIDBodyMap[ContactInfo.BodyID1]->GetUserData());
		const FJoltUserData* UD2 = GetUserData(BodyIDBodyMap[ContactInfo.BodyID2]->GetUserData());
		if (!UD1 || !UD2) return false;
			
		UPrimitiveComponent* P1 = Cast<UPrimitiveComponent>(UD1->Component);
		UPrimitiveComponent* P2 = Cast<UPrimitiveComponent>(UD2->Component);
			
		if (!P1 || !P2) return false;

		if (UD1->bGenerateOverlapEvents && P1->OnComponentEndOverlap.IsBound())
		{
			P1->OnComponentEndOverlap.Broadcast(P1, P2->GetOwner(), P2, ContactInfo.BodyID2);
		}
			
		if (UD2->bGenerateOverlapEvents && P2->OnComponentEndOverlap.IsBound())
		{
			P2->OnComponentEndOverlap.Broadcast(P2, P1->GetOwner(), P1, ContactInfo.BodyID1);
		}
	}
	
	return true;
}

void UJoltPhysicsWorldSubsystem::StepPhysics(float FixedTimeStep)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(StepPhysics);
	
	if (OnPrePhysicsStep.IsBound())
	{
		OnPrePhysicsStep.Broadcast(FixedTimeStep);
	}
	
	JoltWorker->StepPhysics();
#ifdef JPH_DEBUG_RENDERER
	if (DrawDebugShapes == 1) 
	{
		DrawDebugLines();
	}
	
#endif

#if WITH_EDITOR
	
#endif

	if (OnPostPhysicsStep.IsBound())
	{
		OnPostPhysicsStep.Broadcast(FixedTimeStep);
	}

	if (BroadcastPendingAddedContactEvents())
	{
		//UE_LOG(LogJoltBridge, Log, TEXT("Finished Broadcasting Newly Added Contact Events"))
	}
	
	if (BroadcastPendingRemovedContactEvents())
	{
		//UE_LOG(LogJoltBridge, Log, TEXT("Finished Broadcasting Newly Removed Contact Events"))
	}
}

void UJoltPhysicsWorldSubsystem::StepVirtualCharacters(float FixedTimeStep)
{
	for (TTuple<unsigned, JPH::CharacterVirtual*>& C : VirtualCharacterMap)
	{
		JPH::CharacterVirtual::ExtendedUpdateSettings update_settings;
		C.Value->ExtendedUpdate(FixedTimeStep, MainPhysicsSystem->GetGravity(), update_settings, MainPhysicsSystem->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
				MainPhysicsSystem->GetDefaultLayerFilter(Layers::MOVING),
				{ },
				{ },
				*JoltWorker->GetAllocator());
	}
}

void UJoltPhysicsWorldSubsystem::AddImpulse(AActor* Target, const FVector Impulse)
{
	int32 Id = INDEX_NONE;
	const FUnrealShapeDescriptor& Descriptor = GetShapeDescriptorData(Target);
	Id = Descriptor.GetRootColliderId();
	
	if (Id == INDEX_NONE) return;
	JPH::BodyID JoltBodyId(Id);
	BodyInterface->AddImpulse(JoltBodyId, JoltHelpers::ToJoltVector3(Impulse));
}

void UJoltPhysicsWorldSubsystem::AddForce(AActor* Target, const FVector Force)
{
	int32 Id = INDEX_NONE;
	const FUnrealShapeDescriptor& Descriptor = GetShapeDescriptorData(Target);
	Id = Descriptor.GetRootColliderId();
	
	if (Id == INDEX_NONE) return;
	JPH::BodyID JoltBodyId(Id);
	BodyInterface->AddForce(JoltBodyId, JoltHelpers::ToJoltVector3(Force));
}

void UJoltPhysicsWorldSubsystem::SetGravityFactor(const UPrimitiveComponent* Target, const float GravityFactor)
{
	const int32 ShapeId = FindShapeId(Target);
	if (ShapeId == INDEX_NONE) return;
	
	JPH::BodyID ID(ShapeId);
	
	BodyInterface->SetGravityFactor(ID, GravityFactor);
}

void UJoltPhysicsWorldSubsystem::SetLinearVelocity(const UPrimitiveComponent* Target, const FVector LinearVelocity)
{
	const int32 Id = FindShapeId(Target);
	
	if (Id == INDEX_NONE) return;
	JPH::BodyID JoltBodyId(Id);
	BodyInterface->SetLinearVelocity(JoltBodyId, JoltHelpers::ToJoltVector3(LinearVelocity));
}

void UJoltPhysicsWorldSubsystem::RestoreCharacterState(const int32 Id, const FTransform Transform, const FVector LinearVelocity)
{
	if (!VirtualCharacterMap.Contains(Id)) return;
	
	JPH::CharacterVirtual* C = VirtualCharacterMap[Id];
	
	if (!C) return;
	
	C->SetLinearVelocity(JoltHelpers::ToJoltVector3(LinearVelocity));
	C->SetRotation(JoltHelpers::ToJoltRotation(Transform.GetRotation()));
	C->SetPosition(JoltHelpers::ToJoltVector3(Transform.GetTranslation()));
}


void UJoltPhysicsWorldSubsystem::SetAngularVelocity(const UPrimitiveComponent* Target, const FVector AngularVelocity)
{
	const int32 Id = FindShapeId(Target);
	
	if (Id == INDEX_NONE) return;
	JPH::BodyID JoltBodyId(Id);
	BodyInterface->SetAngularVelocity(JoltBodyId, JoltHelpers::ToJoltVector3(JoltHelpers::DegreesPerSecToRadiansPerSec(AngularVelocity)));
}

void UJoltPhysicsWorldSubsystem::ApplyVelocity(const UPrimitiveComponent* Target, const FVector LinearVelocity, const FVector AngularVelocity)
{
	const int32 Id = FindShapeId(Target);
	if (Id == INDEX_NONE) return;
	JPH::BodyID JoltBodyId(Id);
	
	const FVector AngularVelocityRadPerSec = JoltHelpers::DegreesPerSecToRadiansPerSec(AngularVelocity);
	//BodyInterface->SetLinearAndAngularVelocity(JoltBodyId, JoltHelpers::ToJoltVector3(LinearVelocity), JoltHelpers::ToJoltVector3(AngularVelocityRadPerSec));
	BodyInterface->SetLinearVelocity(JoltBodyId, JoltHelpers::ToJoltVector3(LinearVelocity));
	//BodyInterface->AddAngularImpulse(JoltBodyId, JoltHelpers::ToJoltVector3(AngularVelocityRadPerSec));
}

void UJoltPhysicsWorldSubsystem::WakeBody(const UPrimitiveComponent* Target)
{
	const int32 Id = FindShapeId(Target);
	if (Id == INDEX_NONE) return;
	JPH::BodyID JoltBodyId(Id);
	BodyInterface->ActivateBody(JoltBodyId);
}

void UJoltPhysicsWorldSubsystem::SleepBody(const UPrimitiveComponent* Target)
{
	const int32 Id = FindShapeId(Target);
	if (Id == INDEX_NONE) return;
	JPH::BodyID JoltBodyId(Id);
	BodyInterface->DeactivateBody(JoltBodyId);
}

void UJoltPhysicsWorldSubsystem::ZeroActorVelocity(AActor* Target)
{
	int32 Id = INDEX_NONE;
	const FUnrealShapeDescriptor& Descriptor = GetShapeDescriptorData(Target);
	Id = Descriptor.GetRootColliderId();
	
	if (Id == INDEX_NONE) return;
	JPH::BodyID JoltBodyId(Id);
	BodyInterface->SetLinearAndAngularVelocity(JoltBodyId,JoltHelpers::ToJoltVector3(FVector(0)), JoltHelpers::ToJoltVector3(FVector(0)));
}

TArray<AActor*> UJoltPhysicsWorldSubsystem::GetOverlappingActors(AActor* Target) const
{
	TArray<AActor*> OverlappingActors;
	
	return OverlappingActors;
}

float UJoltPhysicsWorldSubsystem::GetGravity(const UPrimitiveComponent* Target) const
{
	if (!IsBodyValid(Target)) return GetWorld()->GetGravityZ();
	
	const FUnrealShapeDescriptor& Desc = GlobalShapeDescriptorDataCache[Target->GetOwner()];
	const int32 ShapeId = FindShapeId(Target);
	if (ShapeId == INDEX_NONE) return GetWorld()->GetGravityZ();
	
	return BodyInterface->GetGravityFactor(JPH::BodyID(ShapeId));
}

FHitResult UJoltPhysicsWorldSubsystem::LineTraceSingleByChannel(const FVector Start, const FVector End, const TEnumAsByte<ECollisionChannel> Channel, const TArray<AActor*>& ActorsToIgnore, int32& HitBodyId)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UJoltPhysicsWorldSubsystem::LineTraceSingleByChannel);
	FHitResult Hit(NoInit);
	HitBodyId = LineTraceSingle(Start, End, Channel, ActorsToIgnore, Hit);
	
	return Hit;
}

TArray<FHitResult> UJoltPhysicsWorldSubsystem::LineTraceMultiByChannel(const FVector Start, const FVector End,
	const TEnumAsByte<ECollisionChannel> Channel, const TArray<AActor*>& ActorsToIgnore, TArray<int32>& HitBodyIds)
{
	TArray<FHitResult> Hits;
	HitBodyIds = LineTraceMulti(Start, End, Channel, ActorsToIgnore, Hits);
	
	return Hits;
}

FHitResult UJoltPhysicsWorldSubsystem::SweepSphereSingleByChannel(const float Radius, const FVector Start, const FVector End, 
                                                                    const TEnumAsByte<ECollisionChannel> Channel, const TArray<AActor*>& ActorsToIgnore, int32& HitBodyId)
{
	FHitResult Hit(NoInit);

	const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
	HitBodyId = SweepTraceSingle(Shape, Start, End, FQuat::Identity, Channel, ActorsToIgnore, Hit);
	
	return Hit;
}

TArray<FHitResult> UJoltPhysicsWorldSubsystem::SweepSphereMultiByChannel(const float Radius, const FVector Start,
	const FVector End, const TEnumAsByte<ECollisionChannel> Channel, const TArray<AActor*>& ActorsToIgnore,
	TArray<int32>& HitBodyIds)
{
	TArray<FHitResult> Hits;

	const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
	HitBodyIds = SweepTraceMulti(Shape, Start, End, FQuat::Identity, Channel, ActorsToIgnore, Hits);
	
	return Hits;
}

FHitResult UJoltPhysicsWorldSubsystem::SweepCapsuleSingleByChannel(const float Radius, const float HalfHeight,
                                                                     const FVector Start, const FVector End, const FRotator Rotation, const TEnumAsByte<ECollisionChannel> Channel, const TArray<AActor*>& ActorsToIgnore, int32& HitBodyId)
{
	FHitResult Hit(NoInit);

	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	HitBodyId = SweepTraceSingle(Shape, Start, End,Rotation.Quaternion(), Channel, ActorsToIgnore, Hit);
	
	return Hit;
}

TArray<FHitResult> UJoltPhysicsWorldSubsystem::SweepCapsuleMultiByChannel(const float Radius, const float HalfHeight,
	const FVector Start, const FVector End, const FRotator Rotation, const TEnumAsByte<ECollisionChannel> Channel,
	const TArray<AActor*>& ActorsToIgnore, TArray<int32>& HitBodyIds)
{
	TArray<FHitResult> Hits;

	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	HitBodyIds = SweepTraceMulti(Shape, Start, End,Rotation.Quaternion(), Channel, ActorsToIgnore, Hits);
	
	return Hits;
}

FHitResult UJoltPhysicsWorldSubsystem::SweepBoxSingleByChannel(const FVector BoxExtents, const FVector Start, const FVector End, 
                                                                 const FRotator Rotation, const TEnumAsByte<ECollisionChannel> Channel, const TArray<AActor*>& ActorsToIgnore, int32& HitBodyId)
{
	FHitResult Hit(-1);

	const FCollisionShape Shape = FCollisionShape::MakeBox(BoxExtents);
	HitBodyId = SweepTraceSingle(Shape, Start, End, Rotation.Quaternion(), Channel, ActorsToIgnore, Hit);
	
	return Hit;
}

TArray<FHitResult> UJoltPhysicsWorldSubsystem::SweepBoxMultiByChannel(const FVector BoxExtents, const FVector Start,
	const FVector End, const FRotator Rotation, const TEnumAsByte<ECollisionChannel> Channel,
	const TArray<AActor*>& ActorsToIgnore, TArray<int32>& HitBodyIds)
{
	TArray<FHitResult> Hits;

	const FCollisionShape Shape = FCollisionShape::MakeBox(BoxExtents);
	HitBodyIds = SweepTraceMulti(Shape, Start, End, Rotation.Quaternion(), Channel, ActorsToIgnore, Hits);
	
	return Hits;
}

int32 UJoltPhysicsWorldSubsystem::LineTraceSingle(const FVector& Start, const FVector& End, const TEnumAsByte<ECollisionChannel> Channel, const TArray<AActor*>& ActorsToIgnore, FHitResult& OutHit)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UJoltPhysicsWorldSubsystem::LineTraceSingle);
	
	if (!MainPhysicsSystem) 
	{
		UE_LOG(LogTemp, Warning, TEXT("UJoltPhysicsWorldSubsystem::RayTest: loaded without a jolt wouldn't work"));
		return INDEX_NONE;
	} 
	
	JPH::RayCastSettings	 Settings;
	FVector					 dir = End - Start;
	JPH::RRayCast			 ray{ JoltHelpers::ToJoltPosition(Start), JoltHelpers::ToJoltVector3(dir) };
	FRaycastCollector_FirstHit Collector(*MainPhysicsSystem, ray);
	
	
	if (ActorsToIgnore.IsEmpty())
	{
		MainPhysicsSystem->GetNarrowPhaseQuery().CastRay(ray, Settings, Collector, {}, {}, {});
	}
	else
	{
		JPH::IgnoreMultipleBodiesFilter Filter;
		for (AActor* IgnoredActor : ActorsToIgnore)
		{
			if (!GlobalShapeDescriptorDataCache.Contains(IgnoredActor)) continue;
			const FUnrealShapeDescriptor& Ref = GlobalShapeDescriptorDataCache[IgnoredActor];
			for (const FUnrealShape& S : Ref.Shapes)
			{
				Filter.IgnoreBody(JPH::BodyID(S.Id));
			}
		}
		
		
		MainPhysicsSystem->GetNarrowPhaseQuery().CastRay(ray, Settings, Collector, {}, {}, Filter);
	}

	const UPhysicalMaterial* UEMat = nullptr;
	if (Collector.mHasHit)
	{
		const JPH::PhysicsMaterial* foundMat = BodyInterface->GetMaterial(Collector.mBodyID, Collector.mSubShapeID2);
		UEMat = GetUEPhysicsMaterial(static_cast<const JoltPhysicsMaterial*>(foundMat));
	}
	
	ConstructHitResult(Collector, OutHit);
	
	if (DrawDebugTraces > 0)
	{
		DrawDebugLine(GetWorld(), Start, End, FColor::Green, false, DrawDebugTraces, 0, 1);
		
		if (OutHit.bBlockingHit)
		{
			DrawDebugLine(GetWorld(), Start, OutHit.Location, FColor::Green, false, DrawDebugTraces, 0, 1);
			DrawDebugSolidBox(GetWorld(), OutHit.Location, FVector(10.f), FColor::Red, false, DrawDebugTraces, 1);
		}
		else
		{
			DrawDebugLine(GetWorld(), Start, End, FColor::Green, false, DrawDebugTraces, 0, 1);
		}
	}
	
	return Collector.mBodyID.GetIndex();
}

TArray<int32> UJoltPhysicsWorldSubsystem::LineTraceMulti(const FVector& Start, const FVector& End, const TEnumAsByte<ECollisionChannel> Channel, 
	const TArray<AActor*>& ActorsToIgnore, TArray<FHitResult>& OutHits)
{
	
	TRACE_CPUPROFILER_EVENT_SCOPE(UJoltPhysicsWorldSubsystem::LineTraceMulti);
	TArray<int32> Results;
	
	if (!MainPhysicsSystem) 
	{
		UE_LOG(LogTemp, Warning, TEXT("UJoltPhysicsWorldSubsystem::LineTraceMulti: loaded without a jolt wouldn't work"));
		return Results;
	} 

	
	JPH::RayCastSettings	 Settings;
	FVector					 dir = End - Start;
	JPH::RRayCast			 ray{ JoltHelpers::ToJoltPosition(Start), JoltHelpers::ToJoltVector3(dir) };
	FRaycastCollector_AllHits Collector(*MainPhysicsSystem, ray);
	
	if (ActorsToIgnore.IsEmpty())
	{
		MainPhysicsSystem->GetNarrowPhaseQuery().CastRay(ray, Settings, Collector, {}, {}, {});
	}
	else
	{
		JPH::IgnoreMultipleBodiesFilter Filter;
		for (AActor* IgnoredActor : ActorsToIgnore)
		{
			if (!GlobalShapeDescriptorDataCache.Contains(IgnoredActor)) continue;
			const FUnrealShapeDescriptor& Ref = GlobalShapeDescriptorDataCache[IgnoredActor];
			for (const FUnrealShape& S : Ref.Shapes)
			{
				Filter.IgnoreBody(JPH::BodyID(S.Id));
			}
		}
		
		
		MainPhysicsSystem->GetNarrowPhaseQuery().CastRay(ray, Settings, Collector, {}, {}, Filter);
	}
	
	
	ConstructHitResult(Collector, OutHits);
	
	if (DrawDebugTraces > 0)
	{
		for (const FHitResult& Hit : OutHits)
		{
			if (Hit.bBlockingHit)
			{
				DrawDebugLine(GetWorld(), Start, Hit.Location, FColor::Green, false, DrawDebugTraces, 0, 1);
				DrawDebugSolidBox(GetWorld(), Hit.Location, FVector(10.f), FColor::Red, false, DrawDebugTraces, 1);
			}
			else
			{
				DrawDebugLine(GetWorld(), Start, End, FColor::Green, false, DrawDebugTraces, 0, 1);
			}
		}
	}

	for (int i = 0; i < Collector.mHits.size(); ++i)
	{
		const JPH::RayCastResult& Hit = Collector.mHits.at(i);
		Results.Add(Hit.mBodyID.GetIndex());
	}
	
	return Results;
	
}

int32 UJoltPhysicsWorldSubsystem::SweepTraceSingle(const FCollisionShape& Shape, const FVector& Start, const FVector& End, 
	const FQuat& Rotation, const TEnumAsByte<ECollisionChannel>& Channel, const TArray<AActor*>& ActorsToIgnore, FHitResult& OutHit)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UJoltPhysicsWorldSubsystem::SweepTraceSingle);
	if (!MainPhysicsSystem) {
		UE_LOG(LogTemp, Warning, TEXT("UJoltPhysicsWorldSubsystem::RayTestSingle: loaded without a jolt wouldn't work"));
		return INDEX_NONE;
	} 
	
	const JPH::Shape* CollisionShape = ProcessShapeElement(Shape);

	DebugTraces(Shape, Start, End, Rotation);
	
	
	FVector FinalEnd = End;
	if (Start.Equals(End))
	{
		FinalEnd.X += SMALL_NUMBER;
		FinalEnd.Y += SMALL_NUMBER;
		FinalEnd.Z += SMALL_NUMBER;
	}
	
	
	JPH::RMat44 FromTransform = JoltHelpers::ToJoltTransform(FTransform(Rotation, Start));
	JPH::RMat44 ToTransform = JoltHelpers::ToJoltTransform(FTransform(Rotation, FinalEnd));
	JPH::Vec3 Dir = JoltHelpers::ToJoltVector3(End - Start);
	
	JPH::RShapeCast ShapeCast = JPH::RShapeCast::sFromWorldTransform(CollisionShape, JPH::RVec3::sOne(), FromTransform, Dir);
	/*{ 
		CollisionShape,
		JoltHelpers::ToJoltVector3(FVector(1), false),
		FromTransform,
		Dir
	};*/
	
	JPH::ShapeCastSettings Settings;
	Settings.mReturnDeepestPoint = false;
	Settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
	Settings.mBackFaceModeConvex = JPH::EBackFaceMode::CollideWithBackFaces;
	
	FClosestShapeCastHitCollector Collector(*MainPhysicsSystem, ShapeCast);

	if (ActorsToIgnore.Num() > 0)
	{
		JPH::IgnoreMultipleBodiesFilter Filter;
		for (AActor* IgnoredActor : ActorsToIgnore)
		{
			if (!GlobalShapeDescriptorDataCache.Contains(IgnoredActor)) continue;
			const FUnrealShapeDescriptor& Ref = GlobalShapeDescriptorDataCache[IgnoredActor];
			for (const FUnrealShape& S : Ref.Shapes)
			{
				Filter.IgnoreBody(JPH::BodyID(S.Id));
			}
		}
		
		MainPhysicsSystem->GetNarrowPhaseQuery().CastShape
		(
			ShapeCast,
			Settings,
			ShapeCast.mCenterOfMassStart.GetTranslation(),
			Collector,
			{},
			{},
			Filter
		);
	}
	else
	{
		MainPhysicsSystem->GetNarrowPhaseQuery().CastShape
		(
			ShapeCast,
			Settings,
			ShapeCast.mCenterOfMassStart.GetTranslation(),
			Collector,
			{},
			{},
			{}
		);
	}
	
	ConstructHitResult(Collector, OutHit);
	
	if (DrawDebugTraces > 0)
	{
		if (OutHit.bBlockingHit)
		{
			DrawDebugLine(GetWorld(), Start, OutHit.Location, FColor::Green, false, DrawDebugTraces, 0, 1);
			DrawDebugSolidBox(GetWorld(), OutHit.Location, FVector(10.f), FColor::Red, false, DrawDebugTraces, 1);
		}
		else
		{
			DrawDebugLine(GetWorld(), Start, End, FColor::Green, false, DrawDebugTraces, 0, 1);
		}
	}
	
	return Collector.mBodyID.GetIndex();
}

void UJoltPhysicsWorldSubsystem::DebugTraces(const FCollisionShape& Shape, const FVector& Start, const FVector& End, const FQuat& Rotation) const
{
	if (DrawDebugTraces > 0)
	{
		if (Shape.IsBox())
		{
			DrawDebugBox(GetWorld(), Start, Shape.GetBox(), Rotation, FColor::Magenta, false, DrawDebugTraces);
			DrawDebugLine(GetWorld(), Start, End, FColor::Yellow, false, DrawDebugTraces);
			DrawDebugBox(GetWorld(), End, Shape.GetBox(), Rotation, FColor::Green, false, DrawDebugTraces);
		}
		else if (Shape.IsSphere())
		{
			DrawDebugSphere(GetWorld(), Start, Shape.GetCapsuleRadius(), 12, FColor::Magenta, false, DrawDebugTraces);
			DrawDebugLine(GetWorld(), Start, End, FColor::Yellow, false, DrawDebugTraces);
			DrawDebugSphere(GetWorld(), End, Shape.GetCapsuleRadius(), 12, FColor::Magenta, false, DrawDebugTraces);
		}
		else if (Shape.IsCapsule())
		{
			DrawDebugCapsule(GetWorld(), Start, Shape.GetCapsuleHalfHeight(), Shape.GetCapsuleRadius(), Rotation, FColor::Magenta, false, DrawDebugTraces);
			DrawDebugLine(GetWorld(), Start, End, FColor::Yellow, false, DrawDebugTraces);
			DrawDebugCapsule(GetWorld(), End, Shape.GetCapsuleHalfHeight(), Shape.GetCapsuleRadius(), Rotation, FColor::Green, false, DrawDebugTraces);
		}
	}
}

TArray<int32> UJoltPhysicsWorldSubsystem::SweepTraceMulti(const FCollisionShape& Shape, const FVector& Start,
                                                          const FVector& End, const FQuat& Rotation, const TEnumAsByte<ECollisionChannel>& Channel,
                                                          const TArray<AActor*>& ActorsToIgnore, TArray<FHitResult>& OutHits)
{
	
	TRACE_CPUPROFILER_EVENT_SCOPE(UJoltPhysicsWorldSubsystem::SweepTraceMulti);
	TArray<int32> Results;
	if (!MainPhysicsSystem) {
		UE_LOG(LogTemp, Warning, TEXT("UJoltPhysicsWorldSubsystem::RayTestSingle: loaded without a jolt wouldn't work"));
		return Results;
	} 
	
	const JPH::Shape* CollisionShape = ProcessShapeElement(Shape);

	DebugTraces(Shape, Start, End, Rotation);
	
	
	FVector FinalEnd = End;
	if (Start.Equals(End))
	{
		FinalEnd.X += SMALL_NUMBER;
		FinalEnd.Y += SMALL_NUMBER;
		FinalEnd.Z += SMALL_NUMBER;
	}
	
	
	JPH::RMat44 FromTransform = JoltHelpers::ToJoltTransform(FTransform(Rotation, Start));
	JPH::RMat44 ToTransform = JoltHelpers::ToJoltTransform(FTransform(Rotation, FinalEnd));
	JPH::Vec3 Dir = JoltHelpers::ToJoltVector3(End - Start);
	
	JPH::RShapeCast ShapeCast = JPH::RShapeCast::sFromWorldTransform(CollisionShape, JPH::RVec3::sOne(), FromTransform, Dir);
	/*{ 
		CollisionShape,
		JoltHelpers::ToJoltVector3(FVector(1), true),
		FromTransform,
		Dir
	};*/
	
	JPH::ShapeCastSettings Settings;
	/*Settings.mReturnDeepestPoint = false;
	Settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
	Settings.mBackFaceModeConvex = JPH::EBackFaceMode::CollideWithBackFaces;*/
	
	FSweepCastCollector_AllHits Collector(*MainPhysicsSystem, ShapeCast);

	if (ActorsToIgnore.Num() > 0)
	{
		JPH::IgnoreMultipleBodiesFilter Filter;
		for (AActor* IgnoredActor : ActorsToIgnore)
		{
			if (!GlobalShapeDescriptorDataCache.Contains(IgnoredActor)) continue;
			const FUnrealShapeDescriptor& Ref = GlobalShapeDescriptorDataCache[IgnoredActor];
			for (const FUnrealShape& S : Ref.Shapes)
			{
				Filter.IgnoreBody(JPH::BodyID(S.Id));
			}
		}
		
		MainPhysicsSystem->GetNarrowPhaseQuery().CastShape
		(
			ShapeCast,
			Settings,
			JPH::RVec3::sZero(),
			Collector,
			{},
			{},
			Filter
		);
	}
	else
	{
		MainPhysicsSystem->GetNarrowPhaseQuery().CastShape
		(
			ShapeCast,
			Settings,
			JPH::RVec3::sZero(),
			Collector,
			{},
			{},
			{}
		);
	}
	
	ConstructHitResult(Collector, OutHits);
	
	if (DrawDebugTraces > 0)
	{
		for (const FHitResult& Hit : OutHits)
		{
			if (Hit.bBlockingHit)
			{
				DrawDebugLine(GetWorld(), Start, Hit.Location, FColor::Green, false, DrawDebugTraces, 0, 1);
				DrawDebugSolidBox(GetWorld(), Hit.Location, FVector(10.f), FColor::Red, false, DrawDebugTraces, 1);
			}
			else
			{
				DrawDebugLine(GetWorld(), Start, End, FColor::Green, false, DrawDebugTraces, 0, 1);
			}
		}
	}
	
	for (int i = 0; i < Collector.mHits.size(); ++i)
	{
		const JPH::ShapeCastResult& Hit = Collector.mHits.at(i);
		Results.Add(Hit.mBodyID2.GetIndex());
	}
	
	return Results;
}

FVector UJoltPhysicsWorldSubsystem::GetVelocity(const JPH::BodyID& ID) const
{
	return JoltHelpers::ToUnrealVector3(BodyInterface->GetLinearVelocity(ID));
}

void UJoltPhysicsWorldSubsystem::ConstructHitResult(const FRaycastCollector_FirstHit& Result, FHitResult& OutHit) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UJoltPhysicsWorldSubsystem::ConstructHitResult);
	UPhysicalMaterial* UEMat = nullptr;

	const FVector HitLocation = JoltHelpers::ToUnrealPosition(Result.mContactPosition, UE_WORLD_ORIGIN);
	const FVector ImpactNormal = JoltHelpers::ToUnrealNormal(Result.mContactNormal);
	const FVector From = JoltHelpers::ToUnrealPosition(Result.mRay.mOrigin, UE_WORLD_ORIGIN);
	
	OutHit.bBlockingHit = Result.HasHit();
	OutHit.Location = HitLocation;
	OutHit.ImpactPoint = HitLocation;
	OutHit.ImpactNormal = ImpactNormal;
	OutHit.Normal = ImpactNormal;
	OutHit.Distance = FVector::Distance(HitLocation, From);
	
	if (!Result.mBody) return;
	
	const FJoltUserData* UserData =  reinterpret_cast<FJoltUserData*>(Result.mBody->GetUserData());
	if (!UserData) return;
	
	AActor* HitActor = nullptr;
	if (Result.HasHit())
	{
		HitActor = UserData->OwnerActor;
	}
	
	if (!HitActor) return;
	
	if (GlobalShapeDescriptorDataCache.Contains(HitActor))
	{
		const FUnrealShapeDescriptor& Data = GlobalShapeDescriptorDataCache[HitActor];
		UPrimitiveComponent* HitComp = Data.FindClosestPrimitive(HitLocation);
		OutHit.Component = HitComp;
		OutHit.HitObjectHandle = FActorInstanceHandle(HitActor);
		
		OutHit.PhysMaterial = UserData->PhysMaterial;
	}

}

void UJoltPhysicsWorldSubsystem::ConstructHitResult(const FClosestShapeCastHitCollector& Result, FHitResult& OutHit) const
{
	
	TRACE_CPUPROFILER_EVENT_SCOPE(UJoltPhysicsWorldSubsystem::ConstructHitResult);
	UPhysicalMaterial* UEMat = nullptr;

	const FVector HitLocation = JoltHelpers::ToUnrealPosition(Result.mContactPosition, UE_WORLD_ORIGIN);
	const FVector ImpactNormal = JoltHelpers::ToUnrealNormal(Result.mContactNormal);
	const FVector From = JoltHelpers::ToUnrealPosition(Result.mRay.mCenterOfMassStart.GetTranslation(), UE_WORLD_ORIGIN);
	
	OutHit.bBlockingHit = Result.HasHit();
	OutHit.Location = HitLocation;
	OutHit.ImpactPoint = HitLocation;
	OutHit.ImpactNormal = ImpactNormal;
	OutHit.Normal = ImpactNormal;
	OutHit.Distance = FVector::Distance(HitLocation, From);
	
	if (!Result.mBody) return;
	
	const FJoltUserData* UserData =  reinterpret_cast<FJoltUserData*>(Result.mBody->GetUserData());
	if (!UserData) return;
	
	AActor* HitActor = nullptr;
	if (Result.HasHit())
	{
		HitActor = UserData->OwnerActor;
	}
	
	
	
	if (!HitActor) return;
	if (GlobalShapeDescriptorDataCache.Contains(HitActor))
	{
		const FUnrealShapeDescriptor& Data = GlobalShapeDescriptorDataCache[HitActor];
		UPrimitiveComponent* HitComp = Data.FindClosestPrimitive(HitLocation);
		OutHit.Component = HitComp;
		OutHit.HitObjectHandle = FActorInstanceHandle(HitActor);
		OutHit.PhysMaterial = UserData->PhysMaterial;
	}
}

void UJoltPhysicsWorldSubsystem::ConstructHitResult(const FRaycastCollector_AllHits& Result, TArray<FHitResult>& OutHits) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UJoltPhysicsWorldSubsystem::ConstructHitResults);
	for (int i = 0; i < Result.mHits.size(); ++i)
	{
		FHitResult OutHit;
		
		const JPH::RayCastResult& Hit = Result.mHits.at(i);
		FVector HitLocation;
		FVector ImpactNormal;
		JPH::BodyID HitBodyId;
		JPH::SubShapeID SubShapeId;
		Result.GetData(Hit, HitBodyId, SubShapeId, HitLocation, ImpactNormal);
		const FJoltUserData* UserData = reinterpret_cast<const FJoltUserData*>(BodyInterface->GetUserData(HitBodyId));
		if (!UserData) return;
		
		UPhysicalMaterial* UEMat = nullptr;
	
		AActor* HitActor = UserData->OwnerActor;
	
		OutHit.bBlockingHit = true;
		OutHit.Location = HitLocation;
		OutHit.ImpactPoint = HitLocation;
		OutHit.ImpactNormal = ImpactNormal;
		OutHit.Normal = ImpactNormal;
		OutHit.Distance = FVector::Distance(HitLocation, JoltHelpers::ToUnrealPosition(Result.mRay.mOrigin));
	
		if (!HitActor) return;
	
		if (GlobalShapeDescriptorDataCache.Contains(HitActor))
		{
			const FUnrealShapeDescriptor& Data = GlobalShapeDescriptorDataCache[HitActor];
			UPrimitiveComponent* HitComp = Data.FindClosestPrimitive(HitLocation);
			OutHit.Component = HitComp;
			OutHit.HitObjectHandle = FActorInstanceHandle(HitActor);
			OutHit.PhysMaterial = UserData->PhysMaterial;
		}
		
		OutHits.Add(OutHit);
	}
}

void UJoltPhysicsWorldSubsystem::ConstructHitResult(const FSweepCastCollector_AllHits& Result, TArray<FHitResult>& OutHits) const
{
	
	TRACE_CPUPROFILER_EVENT_SCOPE(UJoltPhysicsWorldSubsystem::ConstructHitResults);
	for (int i = 0; i < Result.mHits.size(); ++i)
	{
		FHitResult OutHit;
		
		const JPH::ShapeCastResult& Hit = Result.mHits.at(i);
		const FJoltUserData* UserData = reinterpret_cast<const FJoltUserData*>(BodyInterface->GetUserData(Hit.mBodyID2));
		if (!UserData) return;
		const FVector& HitLocation = JoltHelpers::ToUnrealVector3(Hit.mContactPointOn2);
		const FVector& ImpactNormal = JoltHelpers::ToUnrealNormal(-Hit.mPenetrationAxis.Normalized());
		
		UPhysicalMaterial* UEMat = nullptr;
	
		AActor* HitActor = UserData->OwnerActor;
	
		OutHit.bBlockingHit = true;
		OutHit.Location = HitLocation;
		OutHit.ImpactPoint = HitLocation;
		OutHit.ImpactNormal = ImpactNormal;
		OutHit.Normal = ImpactNormal;
		OutHit.Distance = FVector::Distance(HitLocation, JoltHelpers::ToUnrealPosition(Result.mRay.mCenterOfMassStart.GetTranslation()));
	
		if (!HitActor) return;
	
		if (GlobalShapeDescriptorDataCache.Contains(HitActor))
		{
			const FUnrealShapeDescriptor& Data = GlobalShapeDescriptorDataCache[HitActor];
			UPrimitiveComponent* HitComp = Data.FindClosestPrimitive(HitLocation);
			OutHit.Component = HitComp;
			OutHit.HitObjectHandle = FActorInstanceHandle(HitActor);
			OutHit.PhysMaterial = UserData->PhysMaterial;
		}
		
		OutHits.Add(OutHit);
	}
}

void UJoltPhysicsWorldSubsystem::AddAllJoltActors(const UWorld* World)
{
	TArray<AActor*> dynamicActors;

	if (!World)
	{
		UE_LOG(LogJoltBridge, Warning, TEXT("Invalid World context."));
		return;
	}
	
	// Iterate over all Actors in the world
	for (TActorIterator<AActor> ActorItr(World); ActorItr; ++ActorItr)
	{
		AActor* Actor = *ActorItr;
		if (!Actor)
			continue;
		
		bool bShouldRegister = false;
		TInlineComponentArray<UPrimitiveComponent*, 20> Components;
		
		// Collisions from Meshes
		Actor->GetComponents(UPrimitiveComponent::StaticClass(), Components);
		for (UPrimitiveComponent* Comp : Components)
		{
			if (!Comp) continue;
			if (IJoltPrimitiveComponentInterface* I = Cast<IJoltPrimitiveComponentInterface>(Comp))
			{
				bShouldRegister = I->GetJoltPhysicsBodySettings().bAutomaticallyRegisterWithJolt;
				break;
			}
		}
		
		if (!bShouldRegister) continue;
		
		dynamicActors.Add(Actor);
	}

	// Might not be needed, but keeping it because I don't want to debug
	// deterministic behaviour changes across multiple instances...
	dynamicActors.Sort([](const AActor& A, const AActor& B) {
		return A.GetName() < B.GetName();
	});
	

	for (AActor* Actor : dynamicActors)
	{
		if (!Actor) continue;

		bool bShouldRegister = true;
		
		if (GlobalShapeDescriptorDataCache.Contains(Actor))
		{
			bShouldRegister = false;
		}
		
		if (!bShouldRegister) continue;
		
		RegisterJoltRigidBody(Actor);
	}
}

void UJoltPhysicsWorldSubsystem::ExtractPhysicsGeometry(const AActor* Actor, PhysicsGeometryCallback CB, FUnrealShapeDescriptor& ShapeDescriptor)
{
	TInlineComponentArray<UPrimitiveComponent*, 20> Components;
	// Used to easily get a component's transform relative to Actor, not parent component
	const FTransform InvActorTransform = Actor->GetActorTransform();

	// Collisions from Meshes
	Actor->GetComponents(UPrimitiveComponent::StaticClass(), Components);
	for (UPrimitiveComponent* Comp : Components)
	{
		IJoltPrimitiveComponentInterface* I = Cast<IJoltPrimitiveComponentInterface>(Comp);
		if (!I) continue;

		const FJoltPhysicsBodySettings& ShapeOptions = I->GetJoltPhysicsBodySettings();
		if (!ShapeOptions.bGenerateCollisionEventsInJolt && !ShapeOptions.bGenerateOverlapEventsInJolt)
		{
			continue;
		}
		
		const bool bIsRootComponent = Actor->GetRootComponent() == Comp;
		ShapeDescriptor.Add(Comp, bIsRootComponent);
		
		if (Cast<UStaticMeshComponent>(Comp))
		{
			ExtractPhysicsGeometry(Cast<UStaticMeshComponent>(Comp), InvActorTransform, CB, ShapeDescriptor);
		}
		else if (Cast<UShapeComponent>(Comp))
		{
			ExtractPhysicsGeometry(Cast<UShapeComponent>(Comp), InvActorTransform, CB, ShapeDescriptor);
		}
		else if (Cast<USkeletalMeshComponent>(Comp))
		{
			// Extract Shapes From Physics Asset.
		}
	}
}


void UJoltPhysicsWorldSubsystem::ExtractComplexPhysicsGeometry(const FTransform& XformSoFar, UStaticMeshComponent* Mesh,
	PhysicsGeometryCallback Callback, FUnrealShapeDescriptor& ShapeDescriptor)
{
	IJoltPrimitiveComponentInterface* I = Cast<IJoltPrimitiveComponentInterface>(Mesh);
	if (!I) return;	

		FStaticMeshRenderData* renderData = Mesh->GetStaticMesh()->GetRenderData();
	if (!renderData)
	{
		UE_LOG(LogJoltBridge, Error, TEXT("Invalid render data. (complex collision extraction)"));
		return;
	}
	if (renderData->LODResources.Num() == 0)
	{
		UE_LOG(LogJoltBridge, Error, TEXT("LODResources zero. (complex collision extraction)"));
		return;
	}
	FStaticMeshLODResources& LODResources = renderData->LODResources[0];

	const FPositionVertexBuffer& VertexBuffer = LODResources.VertexBuffers.PositionVertexBuffer;

	JPH::VertexList vertices;
	FVector	Scale = XformSoFar.GetScale3D();

	for (uint32 i = 0; i < VertexBuffer.GetNumVertices(); i++)
	{
		vertices.push_back(JoltHelpers::ToJoltFloat3(
			FVector3f(VertexBuffer.VertexPosition(i).X * Scale.X,
				VertexBuffer.VertexPosition(i).Y * Scale.Y,
				VertexBuffer.VertexPosition(i).Z * Scale.Z)));
	}

	JPH::IndexedTriangleList triangles;
	JPH::PhysicsMaterialList physicsMaterialList;
	const FIndexArrayView	 Indices = LODResources.IndexBuffer.GetArrayView();

	/*Only supporting 1 Material for the Mesh for now*/
	const int MaterialIDX = 0;
	for (int32 i = 0; i < Indices.Num(); i += 3)
	{
		uint32 verIdx1 = Indices[i];
		uint32 verIdx2 = Indices[i + 1];
		uint32 verIdx3 = Indices[i + 2];

		// Validate indices
		if (verIdx1 >= vertices.size() || verIdx2 >= vertices.size() || verIdx3 >= vertices.size())
		{
			UE_LOG(LogJoltBridge, Error, TEXT("Invalid triangle indices detected!"));
			continue;
		}

		triangles.push_back(JPH::IndexedTriangle(verIdx1, verIdx2, verIdx3, MaterialIDX));
	}

	if (Mesh->GetBodySetup())
	{
		physicsMaterialList.push_back(GetJoltPhysicsMaterial(Mesh->GetBodySetup()->GetPhysMaterial()));
	}
	// TODO: Caching mechanism for MeshShapes
	JPH::MeshShapeSettings	MeshSettings(vertices, triangles, physicsMaterialList);
	JPH::Shape::ShapeResult res = MeshSettings.Create();

	if (!res.IsValid())
	{
		UE_LOG(LogJoltBridge, Error, TEXT("Failed to create Mesh. Error: %s"), *FString(res.GetError().c_str()));
	}

	Callback(res.Get(), XformSoFar, I->GetJoltPhysicsBodySettings());
}

void UJoltPhysicsWorldSubsystem::ExtractPhysicsGeometry(UStaticMeshComponent* SMC, const FTransform& InvActorXform, PhysicsGeometryCallback CB, FUnrealShapeDescriptor& ShapeDescriptor)
{
	if (!SMC) return;
	IJoltPrimitiveComponentInterface* I = Cast<IJoltPrimitiveComponentInterface>(SMC);
	if (!I) return;	
	UStaticMesh* Mesh = SMC->GetStaticMesh();
	if (!Mesh) return;

	const FTransform CompTransform = SMC->GetComponentTransform();
	switch (Mesh->GetBodySetup()->CollisionTraceFlag)
	{

	case ECollisionTraceFlag::CTF_UseComplexAsSimple:
		{
			if (SMC->Mobility != EComponentMobility::Type::Movable)
			{
				// complex geo should not move.
				ExtractComplexPhysicsGeometry(CompTransform, SMC, CB, ShapeDescriptor);
			}
			else
			{
				ExtractPhysicsGeometry(SMC, CompTransform, Mesh->GetBodySetup(), CB, ShapeDescriptor);
			}
			break;
		}
	case ECollisionTraceFlag::CTF_UseDefault:
		{
			ExtractPhysicsGeometry(SMC, CompTransform, Mesh->GetBodySetup(), CB, ShapeDescriptor);
			break;
		}
	default:
		
		break;
	}

}


void UJoltPhysicsWorldSubsystem::ExtractPhysicsGeometry(UShapeComponent* Sc, const FTransform& InvActorXform, PhysicsGeometryCallback CB, FUnrealShapeDescriptor& ShapeDescriptor)
{
	// We want the complete transform from Actor to this component, not just relative to parent
	FTransform CompFullRelXForm = Sc->GetComponentTransform();
	ExtractPhysicsGeometry(Sc, CompFullRelXForm, Sc->ShapeBodySetup, CB, ShapeDescriptor);
}


void UJoltPhysicsWorldSubsystem::ExtractPhysicsGeometry(UPrimitiveComponent* PrimitiveComponent, const FTransform& XformSoFar, UBodySetup* BodySetup, PhysicsGeometryCallback CB, FUnrealShapeDescriptor& ShapeDescriptor)
{
	if (!ensure(BodySetup != nullptr))
	{
		return;
	}
	
	IJoltPrimitiveComponentInterface* I = Cast<IJoltPrimitiveComponentInterface>(PrimitiveComponent);
	if (!I) return;	
	
	const FVector				Scale = XformSoFar.GetScale3D();
	const JPH::Shape*			JoltShape = nullptr;
	JPH::CompoundShapeSettings* compoundShapeSettings = nullptr;

	if (!ensure(BodySetup != nullptr))
	{
		return;
	}

	const JoltPhysicsMaterial* physicsMaterial = GetJoltPhysicsMaterial(BodySetup->GetPhysMaterial());

	//  if the total makes up more than 1, we have a compound Shape configured in USkeletalMeshComponent
	if (BodySetup->AggGeom.BoxElems.Num() + BodySetup->AggGeom.SphereElems.Num() + BodySetup->AggGeom.SphylElems.Num() > 1)
	{
		compoundShapeSettings = new JPH::StaticCompoundShapeSettings();
	}

	for (const FKBoxElem& ueBox : BodySetup->AggGeom.BoxElems)
	{
		FVector Dimensions = FVector(ueBox.X, ueBox.Y, ueBox.Z) * Scale;
		// We'll re-use based on just the LxWxH, including Actor Scale
		// Rotation and centre will be baked in world space
		const JPH::BoxShape* JoltBox = GetBoxCollisionShape(Dimensions, physicsMaterial);
		JoltShape = JoltBox;

		if (compoundShapeSettings)
		{
			compoundShapeSettings->AddShape(
				JoltHelpers::ToJoltVector3(ueBox.GetTransform().GetLocation()),
				JoltHelpers::ToJoltRotation(ueBox.GetTransform().GetRotation()),
				JoltShape);
			continue;
		}
		
		ShapeDescriptor.Shapes.Last().ShapeRadius = Dimensions.X;
		ShapeDescriptor.Shapes.Last().ShapeWidth = Dimensions.Y;
		ShapeDescriptor.Shapes.Last().ShapeHeight = Dimensions.Z;
		FTransform ShapeXform(ueBox.Rotation, ueBox.Center);
		// Shape transform adds to any relative transform already here
		FTransform XForm = ShapeXform * XformSoFar;
		CB(JoltShape, XForm, I->GetJoltPhysicsBodySettings());
	}
	for (const FKSphereElem& ueSphere : BodySetup->AggGeom.SphereElems)
	{
		// Only support uniform Scale so use X
		const JPH::SphereShape* joltSphere = GetSphereCollisionShape(ueSphere.Radius * Scale.X, physicsMaterial);
		JoltShape = joltSphere;
		if (compoundShapeSettings)
		{
			compoundShapeSettings->AddShape(
				JoltHelpers::ToJoltVector3(ueSphere.GetTransform().GetLocation()),
				JoltHelpers::ToJoltRotation(ueSphere.GetTransform().GetRotation()),
				JoltShape);
			continue;
		}

		ShapeDescriptor.Shapes.Last().ShapeRadius = ueSphere.Radius * Scale.X;
		FTransform ShapeXform(FRotator::ZeroRotator, ueSphere.Center);
		// Shape transform adds to any relative transform already here
		FTransform XForm = ShapeXform * XformSoFar;
		CB(JoltShape, XForm, I->GetJoltPhysicsBodySettings());
	}
	// Sphyl == Capsule (??)
	for (const FKSphylElem& Capsule : BodySetup->AggGeom.SphylElems)
	{
		// X Scales Radius, Z Scales Height
		const JPH::CapsuleShape* capsule = GetCapsuleCollisionShape(Capsule.Radius * Scale.X, Capsule.Length * Scale.Z, physicsMaterial);
		JoltShape = capsule;
		if (compoundShapeSettings)
		{
			compoundShapeSettings->AddShape(
				JoltHelpers::ToJoltVector3(Capsule.GetTransform().GetLocation()),
				JoltHelpers::ToJoltRotation(Capsule.GetTransform().GetRotation()),
				JoltShape);
			continue;
		}

		FTransform ShapeXform(Capsule.GetTransform().GetRotation(), Capsule.Center);
		// Shape transform adds to any relative transform already here
		FTransform XForm = ShapeXform * XformSoFar;
		ShapeDescriptor.Shapes.Last().ShapeRadius = Capsule.Radius * Scale.X;
		ShapeDescriptor.Shapes.Last().ShapeHeight = Capsule.Length * Scale.Z;
		CB(JoltShape, XForm, I->GetJoltPhysicsBodySettings());
	}
	
	for (const FKTaperedCapsuleElem& Capsule : BodySetup->AggGeom.TaperedCapsuleElems)
	{
		// TODO:@GreggoryAddison::CodeCompletion || Tapered capsules are used in the skeletal Mesh physics object. Will need to support this for ragdolls
		// X Scales Radius, Z Scales Height
		/*const JPH::CapsuleShape* capsule = GetCapsuleCollisionShape(Capsule.Radius * Scale.X, Capsule.Length * Scale.Z, physicsMaterial);
		JoltShape = capsule;
		if (compoundShapeSettings)
		{
			compoundShapeSettings->AddShape(
				JoltHelpers::ToJoltVector3(Capsule.GetTransform().GetLocation()),
				JoltHelpers::ToJoltRotation(Capsule.GetTransform().GetRotation()),
				JoltShape);
			continue;
		}*/

		//FTransform ShapeXform(Capsule.GetTransform().GetRotation(), Capsule.Center);
		// Shape transform adds to any relative transform already here
		/*FTransform XForm = ShapeXform * XformSoFar;
		ShapeDescriptor.Shapes.Last().ShapeRadius = Capsule.Radius * Scale.X;
		ShapeDescriptor.Shapes.Last().ShapeHeight = Capsule.Length * Scale.Z;
		CB(JoltShape, XForm, I->GetShapeOptions());*/
	}

	// Convex hull
	for (uint16 i = 0; const FKConvexElem& ConVexElem : BodySetup->AggGeom.ConvexElems)
	{
		const JPH::ConvexHullShape* convexHull = GetConvexHullCollisionShape(BodySetup, i, Scale);
		JoltShape = convexHull;
		i++;
		if (compoundShapeSettings)
		{
			compoundShapeSettings->AddShape(
				JoltHelpers::ToJoltVector3(ConVexElem.GetTransform().GetLocation()),
				JoltHelpers::ToJoltRotation(ConVexElem.GetTransform().GetRotation()),
				JoltShape);
			continue;
		}

		// TODO@GreggoryAddison::CodeCompletion || Use the bounding box??
		CB(JoltShape, XformSoFar, I->GetJoltPhysicsBodySettings());
	}

	if (compoundShapeSettings)
	{
		// TODO@GreggoryAddison::CodeCompletion || Use the bounding box??
		JoltShape = compoundShapeSettings->Create().Get();
		CB(JoltShape, XformSoFar, I->GetJoltPhysicsBodySettings());
		delete compoundShapeSettings;
	}
	
}


const JPH::Shape* UJoltPhysicsWorldSubsystem::ProcessShapeElement(const UShapeComponent* ShapeComponent)
{
	if (!ShapeComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid Shape Component"));
		return nullptr;
	}

	if (const USphereComponent* SphereComponent = Cast<const USphereComponent>(ShapeComponent))
	{
		return GetSphereCollisionShape(SphereComponent->GetScaledSphereRadius());
	}
	else if (const UBoxComponent* BoxComponent = Cast<const UBoxComponent>(ShapeComponent))
	{
		const FVector BoxElem = BoxComponent->GetScaledBoxExtent();
		return GetBoxCollisionShape(FVector(BoxElem.X, BoxElem.Y, BoxElem.Z));
	}
	else if (const UCapsuleComponent* CapsuleComponent = Cast<const UCapsuleComponent>(ShapeComponent))
	{
		return GetCapsuleCollisionShape(CapsuleComponent->GetScaledCapsuleRadius(), CapsuleComponent->GetScaledCapsuleHalfHeight());
	}

	UE_LOG(LogTemp, Warning, TEXT("Unknown or unsupported UShapeComponent type"));
	return nullptr;
}

const JPH::Shape* UJoltPhysicsWorldSubsystem::ProcessShapeElement(const FCollisionShape& ShapeComponent)
{

	if (ShapeComponent.IsSphere())
	{
		return GetSphereCollisionShape(ShapeComponent.GetSphereRadius());
	}
	else if (ShapeComponent.IsBox())
	{
		const FVector BoxElem = ShapeComponent.GetBox();
		return GetBoxCollisionShape(FVector(BoxElem.X, BoxElem.Y, BoxElem.Z));
	}
	
	return GetCapsuleCollisionShape(ShapeComponent.GetCapsuleRadius(), ShapeComponent.GetCapsuleHalfHeight());
}

const JoltPhysicsMaterial* UJoltPhysicsWorldSubsystem::GetJoltPhysicsMaterial(const UPhysicalMaterial* UEPhysicsMat)
{

	if (const JoltPhysicsMaterial** FoundPhysicsMaterial = SurfaceJoltMaterialMap.Find(UEPhysicsMat->SurfaceType.GetValue()))
	{
		return *FoundPhysicsMaterial;
	}

	const JoltPhysicsMaterial* NewPhysicsMaterial = JoltHelpers::ToJoltPhysicsMaterial(UEPhysicsMat);
	SurfaceJoltMaterialMap.Add(UEPhysicsMat->SurfaceType.GetValue(), NewPhysicsMaterial);
	SurfaceUEMaterialMap.Add(UEPhysicsMat->SurfaceType.GetValue(), TWeakObjectPtr<const UPhysicalMaterial>(UEPhysicsMat));
	return NewPhysicsMaterial;
}

const UPhysicalMaterial* UJoltPhysicsWorldSubsystem::GetUEPhysicsMaterial(const JoltPhysicsMaterial* JoltPhysicsMat) const
{
	if (JoltPhysicsMat == nullptr)
	{
		return nullptr;
	}

	const TWeakObjectPtr<const UPhysicalMaterial>* FoundPhysicsMaterial = SurfaceUEMaterialMap.Find(JoltPhysicsMat->SurfaceType);
	if (FoundPhysicsMaterial == nullptr)
	{
		return nullptr;
	}
	return FoundPhysicsMaterial->Get();
}


#pragma region SNAPSHOT HISTORY
static constexpr int32 MinSnapshotCapacity = 8;

int32 UJoltPhysicsWorldSubsystem::RoundUpToPowerOfTwo(int32 Value)
{
	if (Value <= 1) return 1;
	// Round up to next power of two
	Value--;
	Value |= Value >> 1;
	Value |= Value >> 2;
	Value |= Value >> 4;
	Value |= Value >> 8;
	Value |= Value >> 16;
	Value++;
	return Value;
}

void UJoltPhysicsWorldSubsystem::InitializeSnapshotHistory()
{
	int32 Desired = JoltSettings->SnapshotHistoryCapacity;
	Desired = FMath::Max(Desired, MinSnapshotCapacity);

	if (JoltSettings->bForcePowerOfTwoSnapshotCapacity)
	{
		Desired = RoundUpToPowerOfTwo(Desired);
	}

	;

	SnapshotHistory.SetNum(Desired);
	for (FJoltPhysicsSnapshotSlot& Slot : SnapshotHistory)
	{
		Slot.Reset();
	}

	UE_LOG(LogTemp, Log, TEXT("UJoltPhysicsWorldSubsystem: Snapshot history initialized. Capacity=%d"), Desired);
}

void UJoltPhysicsWorldSubsystem::EnsureSnapshotHistoryReady()
{
	if (SnapshotHistory.Num() <= 0)
	{
		InitializeSnapshotHistory();
	}
}



bool UJoltPhysicsWorldSubsystem::GetLastPhysicsState(TArray<uint8>& OutBytes) const
{
	if (SnapshotHistory.Num() == 0) return false;
	
	OutBytes = SnapshotHistory.Last().Bytes;
	return true;
}

int32 UJoltPhysicsWorldSubsystem::FrameToSlotIndex(const int32 CommandFrame) const
{
	// NOTE: CommandFrame should be >= 0. If you use negative sentinel frames, handle them outside.
	const int32 Capacity = SnapshotHistory.Num();
	check(Capacity > 0);

	// Fast path if power-of-two
	if (JoltSettings->bForcePowerOfTwoSnapshotCapacity && FMath::IsPowerOfTwo(Capacity))
	{
		return CommandFrame & (Capacity - 1);
	}

	// General modulo
	// (CommandFrame assumed non-negative)
	return CommandFrame % Capacity;
}

void UJoltPhysicsWorldSubsystem::SaveStateForFrame(const int32 CommandFrame, const JPH::StateRecorderFilter* SaveFilter)
{
	if (!JoltSettings->bStoreSnapshotsOnServer && (GetWorld()->GetNetMode() == NM_DedicatedServer))
	{
		return;
	}
	
	EnsureSnapshotHistoryReady();

	check(CommandFrame != INDEX_NONE);
	check(MainPhysicsSystem != nullptr);

	const int32 SlotIdx = FrameToSlotIndex(CommandFrame);
	FJoltPhysicsSnapshotSlot& Slot = SnapshotHistory[SlotIdx];

	// Create a recorder on the stack (no heap alloc needed).
	JPH::StateRecorderImpl Recorder;

	// Save only "Bodies" state per your earlier approach; adjust if you need more.
	// If you later decide to include constraints, broaden EStateRecorderState accordingly.
	MainPhysicsSystem->SaveState(Recorder, JPH::EStateRecorderState::Bodies, SaveFilter);

	for (const TTuple<unsigned, JPH::CharacterVirtual*>& C : VirtualCharacterMap)
	{
		C.Value->SaveState(Recorder);
	}

	const std::string Data = Recorder.GetData();

	// Overwrite (do NOT append). This keeps memory bounded.
	Slot.Frame = CommandFrame;
	Slot.Bytes.SetNumUninitialized(static_cast<int32>(Data.size()));
	if (!Slot.Bytes.IsEmpty())
	{
		FMemory::Memcpy(Slot.Bytes.GetData(), Data.data(), Data.size());
	}
}

bool UJoltPhysicsWorldSubsystem::RestoreStateForFrame(const int32 CommandFrame)
{
	EnsureSnapshotHistoryReady();

	check(CommandFrame != INDEX_NONE);
	check(MainPhysicsSystem != nullptr);

	const int32 SlotIdx = FrameToSlotIndex(CommandFrame);
	const FJoltPhysicsSnapshotSlot& Slot = SnapshotHistory[SlotIdx];

	if (ContactListener)
	{	
		ContactListener->ClearContactCache();
	}
	
	

	// Validate that this slot still represents the requested frame
	if (Slot.Frame != CommandFrame)
	{
		// Slot was overwritten or never written; history window not large enough or frame mismatch.
		return false;
	}

	if (Slot.Bytes.Num() <= 0)
	{
		return false;
	}

	JPH::StateRecorderImpl Recorder;
	Recorder.WriteBytes(Slot.Bytes.GetData(), Slot.Bytes.Num());

	MainPhysicsSystem->RestoreState(Recorder);
	for (const TTuple<unsigned, JPH::CharacterVirtual*>& C : VirtualCharacterMap)
	{
		C.Value->RestoreState(Recorder);
	}
	
	return true;
}

bool UJoltPhysicsWorldSubsystem::RestoreStateFromBytes(TArrayView<const uint8> SnapshotBytes, const JPH::StateRecorderFilter* RestoreFilter)
{
	check(MainPhysicsSystem);

	JPH::StateRecorderImpl Reader;
	Reader.WriteBytes(SnapshotBytes.GetData(), SnapshotBytes.Num());

	// Must match what you saved: Bodies (and any other categories you saved)
	MainPhysicsSystem->RestoreState(Reader, RestoreFilter);

	// Restore your virtual characters too (must match SaveState)
	for (const TTuple<unsigned, JPH::CharacterVirtual*>& C : VirtualCharacterMap)
	{
		C.Value->RestoreState(Reader);
	}
	
	return !Reader.IsFailed();
}

bool UJoltPhysicsWorldSubsystem::HasStateForFrame(int32 CommandFrame) const
{
	if (SnapshotHistory.Num() <= 0 || CommandFrame == INDEX_NONE)
	{
		return false;
	}

	const int32 SlotIdx = FrameToSlotIndex(CommandFrame);
	const FJoltPhysicsSnapshotSlot& Slot = SnapshotHistory[SlotIdx];
	return Slot.Frame == CommandFrame && Slot.Bytes.Num() > 0;
}
#pragma endregion


