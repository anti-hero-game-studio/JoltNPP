// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/Libraries/JoltBridgeLibrary.h"
#include "JoltBridgeMain.h"
#include "JoltCharacter.h"
#include <functional>
#include "Core/CollisionFilters/JoltFilters.h"
#include "Core/DataTypes/JoltBridgeTypes.h"
#include "GameFramework/Actor.h"
#include "JoltPhysicsWorldSubsystem.generated.h"

class FUnrealGroupFilter;

namespace JPH
{
	class CharacterVirtual;
	class PhysicsSystem;
	class StateRecorderFilter;
}

USTRUCT()
struct FJoltPhysicsSnapshotSlot
{
	GENERATED_BODY()

	// The command frame this slot currently represents. If not equal to the requested frame,
	// the slot is stale/overwritten/invalid for that frame.
	UPROPERTY()
	int32 Frame = INDEX_NONE;
	
	UPROPERTY()
	FString SnapshotDataAsString = TEXT("");

	// Raw snapshot bytes for Jolt::SaveState.
	UPROPERTY()
	TArray<uint8> Bytes;

	void Reset()
	{
		Frame = INDEX_NONE;
		Bytes.Reset();
		SnapshotDataAsString = "";
		
	}
};

class FJoltDebugRenderer;
class FRaycastCollector_AllHits;
class FSweepCastCollector_AllHits;
class FClosestShapeCastHitCollector;
class FRaycastCollector_FirstHit;
class UJoltSettings;
struct FJoltWorkerOptions;
class FJoltWorker;
class FJoltCallBackContactListener;
class UShapeComponent;
class FUnrealCollisionDispatcher;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPhysicsStep, const float&, DeltaTime);
DECLARE_MULTICAST_DELEGATE(FOnModifyContacts);

/**
 * 
 */
UCLASS()
class JOLTBRIDGE_API UJoltPhysicsWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
	
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	void InitPhysicsSystem( int cMaxBodies, int cNumBodyMutexes, int cMaxBodyPairs, int cMaxContactConstraints);
	
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	
	
protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JoltBridge Physics|Objects")
	bool DebugEnabled=true;

	// TODO:@GreggoryAddison::CodeLinking | Replace this with the gravity you would set in the simulation comp
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JoltBridge Physics|Objects")
	FVector Gravity=FVector(0, 0, -9.8);

	// Input the fixed frame rate to calculate physics
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JoltBridge Physics|Objects")
	float PhysicsRefreshRate =60.0f;

	// This is independent of the frame rate in UE
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "JoltBridge Physics|Objects")
	float PhysicsDeltaTime;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JoltBridge Physics|Objects")
	int SubSteps=1;
	
	
#pragma region DELEGATES
	UPROPERTY(BlueprintAssignable, Category="JoltBridge Physics|Delegates")
	FOnPhysicsStep OnPrePhysicsStep;
	
	UPROPERTY(BlueprintAssignable, Category="JoltBridge Physics|Delegates")
	FOnPhysicsStep OnPostPhysicsStep;
#pragma endregion
	
public:
	/**
	 * Creates a joltBridge physics compatible rigid body shape. Actors tagged "dynamic" will automatically register themselves. Set "bSimulatePhysics" to true if you want the body to start in an active state.
	 * @param Target	The actor with primitive components that will be converted to rigid shapes. ACTOR SCALE MUST BE {1,1,1}
	 * @return	Returns the id to use in collision lookups
	 */
	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Registration", DisplayName="Register Dynamic Rigid Body")
	void RegisterJoltRigidBody(AActor* Target);
	
	
	
	
	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Objects", DisplayName="Set Physics State")
	void K2_SetPhysicsState(const UPrimitiveComponent* Target, const FTransform& Transforms, const FVector& Velocity, const FVector& AngularVelocity);
	
	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Objects")
	void GetPhysicsState(const UPrimitiveComponent* Target, FTransform& Transforms, FVector& Velocity, FVector& AngularVelocity, FVector& Force);

	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Objects")
	void StepPhysics(float FixedTimeStep = 0.016666667f);
	
	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Objects")
	void StepVirtualCharacters(float FixedTimeStep = 0.016666667f);
	
	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Objects")
	void AddImpulse(AActor* Target, const FVector Impulse);

	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Objects")
	void AddForce(AActor* Target, const FVector Force);
	
	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Objects")
	void SetGravityFactor(const UPrimitiveComponent* Target, const float GravityFactor);
	
	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Objects")
	void SetLinearVelocity(const UPrimitiveComponent* Target, const FVector LinearVelocity);
	
	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Objects")
	void RestoreCharacterState(const int32 Id, const FTransform Transform, const FVector LinearVelocity);
	
	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Objects")
	void SetAngularVelocity(const UPrimitiveComponent* Target, const FVector AngularVelocity);
	
	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Objects")
	void ApplyVelocity(const UPrimitiveComponent* Target, const FVector LinearVelocity, const FVector AngularVelocity);
	
	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Objects")
	void WakeBody(const UPrimitiveComponent* Target);
	
	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Objects")
	void SleepBody(const UPrimitiveComponent* Target);
	
	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Objects")
	void ZeroActorVelocity(AActor* Target);		
	
	UFUNCTION(BlueprintCallable, Category = "JoltBridge Physics|Registration", DisplayName="Get All Overlapping Actors", meta=(DevelopementOnly))
	TArray<AActor*> GetOverlappingActors(AActor* Target) const;
	
	UFUNCTION(BlueprintPure, Category = "JoltBridge Physics|Objects")
	float GetGravity(const UPrimitiveComponent* Target) const;
	

	

	
#pragma region SCENE QUERY
	
	UFUNCTION(BlueprintCallable, Category="JoltBridge Physics|Scene Queries", meta=( AutoCreateRefTerm = "ActorsToIgnore"))
	FHitResult LineTraceSingleByChannel(const FVector Start, const FVector End, const TEnumAsByte<ECollisionChannel> Channel, const TArray<AActor*>& ActorsToIgnore, int32& HitBodyId);
	
	UFUNCTION(BlueprintCallable, Category="JoltBridge Physics|Scene Queries", meta=( AutoCreateRefTerm = "ActorsToIgnore"))
	UPARAM(DisplayName=Hits) TArray<FHitResult> LineTraceMultiByChannel(const FVector Start, const FVector End, const TEnumAsByte<ECollisionChannel> Channel, 
		const TArray<AActor*>& ActorsToIgnore, TArray<int32>& HitBodyIds);
	
	UFUNCTION(BlueprintCallable, Category="JoltBridge Physics|Scene Queries", meta=( AutoCreateRefTerm = "ActorsToIgnore"))
	FHitResult SweepSphereSingleByChannel(const float Radius, const FVector Start, const FVector End, const TEnumAsByte<ECollisionChannel> Channel, const TArray<AActor*>& ActorsToIgnore, int32& HitBodyId);
	
	UFUNCTION(BlueprintCallable, Category="JoltBridge Physics|Scene Queries", meta=( AutoCreateRefTerm = "ActorsToIgnore"))
	UPARAM(DisplayName=Hits) TArray<FHitResult> SweepSphereMultiByChannel(const float Radius, const FVector Start, const FVector End, 
		const TEnumAsByte<ECollisionChannel> Channel, const TArray<AActor*>& ActorsToIgnore, TArray<int32>& HitBodyIds);
	
	UFUNCTION(BlueprintCallable, Category="JoltBridge Physics|Scene Queries", meta=( AutoCreateRefTerm = "ActorsToIgnore"))
	FHitResult SweepCapsuleSingleByChannel(const float Radius, const float HalfHeight, const FVector Start, const FVector End, const FRotator Rotation, const TEnumAsByte<ECollisionChannel> Channel, const TArray<AActor*>& ActorsToIgnore, int32& HitBodyId);
	
	UFUNCTION(BlueprintCallable, Category="JoltBridge Physics|Scene Queries", meta=( AutoCreateRefTerm = "ActorsToIgnore"))
	UPARAM(DisplayName=Hits) TArray<FHitResult> SweepCapsuleMultiByChannel(const float Radius, const float HalfHeight, const FVector Start, const FVector End, 
		const FRotator Rotation, const TEnumAsByte<ECollisionChannel> Channel, const TArray<AActor*>& ActorsToIgnore, TArray<int32>& HitBodyIds);
	
	UFUNCTION(BlueprintCallable, Category="JoltBridge Physics|Scene Queries", meta=( AutoCreateRefTerm = "ActorsToIgnore"))
	FHitResult SweepBoxSingleByChannel(const FVector BoxExtents, const FVector Start, const FVector End, const FRotator Rotation, const TEnumAsByte<ECollisionChannel> Channel, const TArray<AActor*>& ActorsToIgnore, int32& HitBodyId);
	
	UFUNCTION(BlueprintCallable, Category="JoltBridge Physics|Scene Queries", meta=( AutoCreateRefTerm = "ActorsToIgnore"))
	UPARAM(DisplayName=Hits) TArray<FHitResult> SweepBoxMultiByChannel(const FVector BoxExtents, const FVector Start, const FVector End, 
		const FRotator Rotation, const TEnumAsByte<ECollisionChannel> Channel, const TArray<AActor*>& ActorsToIgnore, TArray<int32>& HitBodyIds);
	
	int32 LineTraceSingle(const FVector& Start, const FVector& End, const TEnumAsByte<ECollisionChannel> Channel, const TArray<AActor*>& ActorsToIgnore, FHitResult& OutHit);
	TArray<int32> LineTraceMulti(const FVector& Start, const FVector& End, const TEnumAsByte<ECollisionChannel> Channel, const TArray<AActor*>& ActorsToIgnore, TArray<FHitResult>& OutHits);
	
	int32 SweepTraceSingle(const FCollisionShape& Shape, const FVector& Start, const FVector& End, const FQuat& Rotation, const TEnumAsByte<ECollisionChannel>& Channel, const TArray<AActor*>& ActorsToIgnore, FHitResult& OutHit);
	void DebugTraces(const FCollisionShape& Shape, const FVector& Start, const FVector& End, const FQuat& Rotation) const;
	TArray<int32> SweepTraceMulti(const FCollisionShape& Shape, const FVector& Start, const FVector& End, const FQuat& Rotation, const TEnumAsByte<ECollisionChannel>& Channel, const TArray<AActor*>& ActorsToIgnore, TArray<FHitResult>& OutHits);
	FVector GetVelocity(const JPH::BodyID& ID) const;
	JPH::PhysicsSystem* GetPhysicsSystem() const {return MainPhysicsSystem;}

private:
	
	
	void ConstructHitResult(const FRaycastCollector_FirstHit& Result, FHitResult& OutHit) const;
	void ConstructHitResult(const FClosestShapeCastHitCollector& Result, FHitResult& OutHit) const;
	
	void ConstructHitResult(const FSweepCastCollector_AllHits& Result, TArray<FHitResult>& OutHits) const;
	void ConstructHitResult(const FRaycastCollector_AllHits& Result, TArray<FHitResult>& OutHits) const;

	
	
#pragma endregion 

	UPROPERTY()
	const UJoltSettings* JoltSettings = nullptr;

	FJoltWorkerOptions* WorkerOptions = nullptr;

	FJoltWorker* JoltWorker = nullptr;

	FJoltCallBackContactListener* ContactListener = nullptr;

	JPH::PhysicsSystem* MainPhysicsSystem = nullptr;

	JPH::BodyInterface* BodyInterface = nullptr;

	uint32 StaticBodyIDX;

	uint32 DynamicBodyIDX;

	FBroadPhaseLayerInterfaceImpl* BroadPhaseLayerInterface = nullptr;

	// Create class that filters object vs broadphase layers
	// Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
	ObjectVsBroadPhaseLayerFilterImpl* ObjectVsBroadphaseLayerFilter = nullptr;

	// Create class that filters object vs object layers
	// Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
	ObjectLayerPairFilterImpl* ObjectVsObjectLayerFilter = nullptr;

	TArray<const JPH::BoxShape*> BoxShapes;

	TArray<const JPH::SphereShape*> SphereShapes;

	TArray<const JPH::CapsuleShape*> CapsuleShapes;

	TArray<const JPH::HeightFieldShapeSettings*> HeightFieldShapes;

	TArray<JPH::Body*> SavedBodies;

	TMap<uint32, JPH::Body*> BodyIDBodyMap;
	TMap<uint32, JPH::CharacterVirtual*> VirtualCharacterMap;

	// JPH::Array<const JPH::Body*> HeightMapArray;

	// JPH::Array<const JPH::Body*> LandscapeSplines;

	TMap<EPhysicalSurface, const JoltPhysicsMaterial*> SurfaceJoltMaterialMap;

	TMap<EPhysicalSurface, TWeakObjectPtr<const UPhysicalMaterial>> SurfaceUEMaterialMap;

	TMap<const JPH::BodyID*, FTransform> SkeletalMeshBodyIDLocalTransformMap;

	struct ConvexHullShapeHolder
	{
		const UBodySetup*			BodySetup;
		int							HullIndex;
		FVector						Scale;
		const JPH::ConvexHullShape* Shape;
	};

	TArray<ConvexHullShapeHolder> ConvexShapes;

#ifdef JPH_DEBUG_RENDERER
	FJoltDebugRenderer* JoltDebugRendererImpl = nullptr;

	JPH::BodyManager::DrawSettings* DrawSettings = nullptr;

	void DrawDebugLines() const;
#endif

	typedef const std::function<void(const JPH::Shape*, const FTransform&, const FJoltPhysicsBodySettings& /*ShapeOptions*/)>& PhysicsGeometryCallback;
	
		
#pragma region JOLTBRIDGE SHAPE CREATION
public:
	 
	const JPH::BoxShape* GetBoxCollisionShape(const FVector& Dimensions, const JoltPhysicsMaterial* material = nullptr);

	const JPH::SphereShape* GetSphereCollisionShape(float Radius, const JoltPhysicsMaterial* material = nullptr);

	const JPH::CapsuleShape* GetCapsuleCollisionShape(float Radius, float Height, const JoltPhysicsMaterial* material = nullptr);

	const JPH::ConvexHullShape* GetConvexHullCollisionShape(UBodySetup* BodySetup, int ConvexIndex, const FVector& Scale, const JoltPhysicsMaterial* material = nullptr);

	JPH::Body* AddRigidBodyCollider(AActor* Actor, const FTransform& FinalTransform, const JPH::Shape* Shape, const FJoltPhysicsBodySettings& Options, const FJoltUserData* UserData);

	JPH::Body* AddRigidBodyCollider(USkeletalMeshComponent* Skel, const FTransform& localTransform, const JPH::Shape* CollisionShape, const FJoltPhysicsBodySettings& Options, const FJoltUserData* UserData);
	
	JPH::Body* AddStaticCollider(const JPH::Shape* Shape, const FTransform& Transform, const FJoltPhysicsBodySettings& Options, const FJoltUserData* UserData);
	
	JPH::Body* AddBodyToSimulation(const JPH::BodyID* BodyID, const JPH::BodyCreationSettings& ShapeSettings, const FJoltPhysicsBodySettings& Options, const FJoltUserData* UserData);

	JPH::BodyCreationSettings MakeBodyCreationSettings(const JPH::Shape* Shape, const FTransform& T, const FJoltPhysicsBodySettings& Options, const FJoltUserData* UserData);
	
private:
	
	/*
	 * Fetch all the actors in UE world and add them to jolt simulation
	 * "jolt-static" tag should be added for static objects (from UE editor)
	 * "jolt-dynamic" tag should be added for dynamic objects (from UE editor)
	 */
	void AddAllJoltActors(const UWorld* World);

	void ExtractPhysicsGeometry(const AActor* Actor, PhysicsGeometryCallback CB, FUnrealShapeDescriptor& ShapeDescriptor);
	
	void ExtractComplexPhysicsGeometry(const FTransform& XformSoFar, UStaticMeshComponent* Mesh, PhysicsGeometryCallback Callback, FUnrealShapeDescriptor& ShapeDescriptor);

	void ExtractPhysicsGeometry(UStaticMeshComponent* SMC, const FTransform& InvActorXform, PhysicsGeometryCallback CB, FUnrealShapeDescriptor& ShapeDescriptor);

	void ExtractPhysicsGeometry(UShapeComponent* Sc, const FTransform& InvActorXform, PhysicsGeometryCallback CB, FUnrealShapeDescriptor& ShapeDescriptor);

	void ExtractPhysicsGeometry(UPrimitiveComponent* PrimitiveComponent, const FTransform& XformSoFar, UBodySetup* BodySetup, PhysicsGeometryCallback CB, FUnrealShapeDescriptor& ShapeDescriptor);
	
	const JPH::Shape* ProcessShapeElement(const UShapeComponent* ShapeComponent);
	const JPH::Shape* ProcessShapeElement(const FCollisionShape& ShapeComponent);
	
	const JoltPhysicsMaterial* GetJoltPhysicsMaterial(const UPhysicalMaterial* UEPhysicsMat);

	const UPhysicalMaterial* GetUEPhysicsMaterial(const JoltPhysicsMaterial* JoltPhysicsMat) const;
	
	void CleanUpJoltBridgeWorld();

	
#pragma endregion
	

#pragma region DATA CACHE
	
protected:
	// Holds an array of collision object id's for a specific actor.
	TMap<TWeakObjectPtr<AActor>, FUnrealShapeDescriptor> GlobalShapeDescriptorDataCache; 
	
	FUnrealShapeDescriptor GetShapeDescriptorData(const AActor* Actor) const;
	
	
public:
	
	
#pragma endregion
	
#pragma region HELPERS
	
	int32 GetActorRootShapeId(const AActor* Actor) const;
	int32 FindShapeId(const UPrimitiveComponent* Target) const;
	bool IsBodyValid(const UPrimitiveComponent* Target) const;
	bool HasRigidBodyBeenCreated(const UPrimitiveComponent* Target) const;
	bool HasSensorBodyBeenCreated(const UPrimitiveComponent* Target) const;
	bool IsCollisionBodyActive(const UPrimitiveComponent* Target) const;
	void SetRigidBodyActiveState(const UPrimitiveComponent* Target, bool Active) const;
	void SetPhysicsState(const UPrimitiveComponent* Target, const FTransform& Transforms, const FVector& Velocity, const FVector& AngularVelocity) const;
	
	const FCollisionResponseContainer& GetCollisionResponseContainer(const UPrimitiveComponent* Target) const;
	
	UPrimitiveComponent* GetPrimitiveComponent(const uint32& Id) const;
	static UPrimitiveComponent* GetPrimitiveComponent(const uint64& UserDataPtr);
	
	JPH::BodyInterface* GetBodyInterface() const { return BodyInterface; };
	JPH::Body* GetBody(const uint32& BodyID) const { return BodyIDBodyMap[BodyID];}
	JPH::Body* GetRigidBody(const FHitResult& Hit) const;
	JPH::Body* GetRigidBody(const UPrimitiveComponent* Target) const;
	const FJoltUserData* GetUserData(const UPrimitiveComponent* Target) const;
	static const FJoltUserData* GetUserData(const uint64& UserDataPtr);
	const UJoltSettings* GetJoltSettings() const {return JoltSettings;};
	
	void RegisterJoltCharacter(const APawn* Target, const JPH::CharacterVirtualSettings& Settings, uint32& CharacterId);
	
	JPH::CharacterVirtual* GetCharacterFromId(const uint32& CharacterId) const;
	
	
	
	FOnModifyContacts OnModifyContacts;
	

private:
	
	bool BroadcastPendingAddedContactEvents();
	bool BroadcastPendingRemovedContactEvents();
	
	FUnrealGroupFilter* UEGroupFilter = nullptr;
	TArray<TUniquePtr<FJoltUserData>> UserDataStore;
	
	FCollisionResponseContainer DefaultCollisionResponseContainer;
	
	FJoltUserData* AllocUserData()
	{
		TUniquePtr<FJoltUserData> Ptr = MakeUnique<FJoltUserData>();
		FJoltUserData* Raw = Ptr.Get();
		UserDataStore.Add(MoveTemp(Ptr));
		return Raw;
	}
	
	
#pragma endregion 
	
	
#pragma region SNAPSHOT HISTORY
public:
	
	// Call once after physics system is created (or on Initialize) to allocate snapshot slots.
	void InitializeSnapshotHistory();

	// Save snapshot for a specific command frame. Filter can be null.
	// This overwrites the ring slot for that frame index.
	void SaveStateForFrame(int32 CommandFrame, const JPH::StateRecorderFilter* SaveFilter = nullptr);

	// Restore snapshot for a specific command frame. Returns false if the snapshot is missing/stale.
	bool RestoreStateForFrame(int32 CommandFrame);
	
	bool RestoreStateFromBytes(TArrayView<const uint8> SnapshotBytes, const JPH::StateRecorderFilter* RestoreFilter);

	// Optional utilities
	bool HasStateForFrame(int32 CommandFrame) const;
	int32 GetSnapshotHistoryCapacity() const { return SnapshotHistory.Num(); }
	
	bool GetDataStreamForCommandFrame(const int32 CommandFrame, FString& DataStream) const;
	
	bool GetLastPhysicsState(const int32& CommandFrame, TArray<uint8>& OutBytes) const;
	
	bool RestorePhysicsStateFromDataStream(const FString& DataStream);

	

private:
	// Convert frame -> slot index
	int32 FrameToSlotIndex(int32 CommandFrame) const;

	// Ensures SnapshotHistory is allocated and capacity sane.
	void EnsureSnapshotHistoryReady();

	// Round up to power-of-two (min 1)
	static int32 RoundUpToPowerOfTwo(int32 Value);

private:
	// Circular buffer of snapshots.
	UPROPERTY(Transient)
	TArray<FJoltPhysicsSnapshotSlot> SnapshotHistory;
	
	UPROPERTY(Transient)
	FJoltPhysicsSnapshotSlot Snapshot;
	
	UPROPERTY(transient)
	int32 SnapshotHistoryCapacity = 256;
	
	
	
#pragma endregion
};
