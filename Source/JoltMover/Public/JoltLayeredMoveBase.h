// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "MoveLibrary/JoltMovementUtilsTypes.h"
#include "Templates/SubclassOf.h"
#include "JoltLayeredMove.h"
#include "JoltMoverLog.h"

#include "JoltLayeredMoveBase.generated.h"

//////////////////////////////////////////////////////////////////////////
// TODO: Consider relocating this class and functionality, a lot of what's here belongs in the original LayeredMove.h/.cpp as replacements for FJoltLayeredMoveBase and such
//		 but we wanted to explore this implementation of Blueprintable layered moves without disrupting the old system so it lives here for now.
//////////////////////////////////////////////////////////////////////////

#define UE_API JOLTMOVER_API
#define USING_JOLT_PARAMS(ActivationParamsClass) using ActivationParamsType = ActivationParamsClass;
#define USING_JOLT_MOVE_DATA(MoveDataClass) using MoveDataType = MoveDataClass;

class UJoltMovementMixer;
struct FJoltMoverTickStartData;
struct FJoltMoverTimeStep;
class UJoltMoverBlackboard;
class UJoltMoverComponent;
class UJoltLayeredMoveLogic;

/**
 * Packaged params struct for initializing a corresponding FJoltLayeredMoveInstancedData
 * Allows BP to do "templated" move data creation. Optional in C++, where params can be forwarded to the FJoltLayeredMoveInstancedData ctor directly
 * The base class can also be used on any activation to just use default values
 */
USTRUCT(Blueprintable)
struct FJoltLayeredMoveActivationParams
{
	GENERATED_BODY()
	
	/**
 	 * This move will expire after a set amount of time if > 0. If 0, it will be ticked only once, regardless of time step. It will need to be manually ended if < 0.
 	 * Note: If changed after starting to a value beneath the current lifetime of the move, it will immediately finish (so if your move finishes early, setting this to 0 is equivalent to returning true from IsFinished())
 	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
	double DurationMs = 0.f;
};

/** Instanced data created and replicated for each activation of a layered move */
USTRUCT(Blueprintable)
struct FJoltLayeredMoveInstancedData
{
	GENERATED_BODY()

	//@todo DanH: If we're already using a macro, might as well put the boilerplate virtuals in here
	USING_JOLT_PARAMS(FJoltLayeredMoveActivationParams);
	
	FJoltLayeredMoveInstancedData() {}
	virtual ~FJoltLayeredMoveInstancedData() {}
	// Checks if 
	UE_API bool operator==(const FJoltLayeredMoveInstancedData& Other) const;
	bool operator!=(const FJoltLayeredMoveInstancedData& Other) const { return !operator==(Other); }

	/** @return A newly allocated copy of this FJoltLayeredMoveInstancedData. Must be overridden by child classes */
	virtual FJoltLayeredMoveInstancedData* Clone() const { return new FJoltLayeredMoveInstancedData(*this); }

	/** @return The UScriptStruct describing this struct. Must be overridden by child classes */
	virtual UScriptStruct* GetScriptStruct() const { return StaticStruct(); }
	
	/** @return True if this move data is identical to OtherData. OtherData is guaranteed to be safe to cast to the implementing type. */
	UE_API virtual bool Equals(const FJoltLayeredMoveInstancedData& OtherData) const;
	
	/** Called when a queued layered move is activated. Provides an opportunity to initialize Layered MoveData */
	UE_API virtual void ActivateFromContext(const FJoltLayeredMoveActivationParams* ActivationParams);
	
	UE_API virtual void NetSerialize(FArchive& Ar);

	virtual void AddReferencedObjects(FReferenceCollector& Collector) {}

	/** Is this move considered to "have" a given gameplay tag? */
	virtual bool HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const { return false; }
	
	double GetStartSimTimeMs() const { return StartSimTimeMs; }

	/**
	 * This move will expire after a set amount of time if > 0. If 0, it will be ticked only once, regardless of time step. It will need to be manually ended if < 0.
	 * Note: If changed after starting to a value beneath the current lifetime of the move, it will immediately finish (so if your move finishes early, setting this to 0 is equivalent to returning true from IsFinished())
	 */
	UPROPERTY(BlueprintReadWrite, Category = Mover)
	double DurationMs = -1.f;
	
	/** The simulation time this move first ticked (< 0 means it hasn't started yet) */
    UPROPERTY(BlueprintReadOnly, Category = Mover)
    double StartSimTimeMs = -UE_BIG_NUMBER;

	friend class UJoltLayeredMoveLogic;
};

template<>
struct TStructOpsTypeTraits<FJoltLayeredMoveInstancedData> : public TStructOpsTypeTraitsBase2<FJoltLayeredMoveInstancedData>
{
	enum
	{
		//WithNetSerializer = true,
		WithCopy = true
	};
};

/**
 * Base class for all layered move logic that operates in tandem with instanced FJoltLayeredMoveInstancedData.
 * The logic object itself is not meant to ever be replicated, and a maximum of one instance of each logic class
 * need ever exist on a given MoverComponent. Repeated and/or simultaneous activations of the same move on a component
 * are represented, tracked, and replicated through instances of the FJoltLayeredMoveInstancedData struct type
 * that the logic class indicates in the ActiveMoveDataStructType property.
 *
 * The virtual methods on this class are invoked in a special and strict pattern that guarantees AccessExecutionMoveData
 * will return the valid data instance relevant to that function execution.
 *
 * Refer to [Examples when they exist] for implementation examples
 */
UCLASS(Abstract, BlueprintType, Blueprintable)
class UJoltLayeredMoveLogic : public UObject
{
	GENERATED_BODY()

public:
	USING_JOLT_MOVE_DATA(FJoltLayeredMoveInstancedData)
	
	UE_API UJoltLayeredMoveLogic();

	UScriptStruct* GetInstancedDataType() const { return InstancedDataStructType; }
	
	const FJoltLayeredMoveFinishVelocitySettings& GetFinishVelocitySettings() const { return FinishVelocitySettings; }
	EJoltMoveMixMode GetMixMode() const { return MixMode; }
	uint8 GetPriority() const { return Priority; }

	// Helper function for validating move data when passing data and logic to/from BP
	static bool ValidateMoveDataGetSet(const UObject* ObjectValidatingData, const UJoltLayeredMoveLogic* MoveLogic, const FStructProperty* MoveDataProperty, const uint8* MoveDataPtr, FFrame& StackFrame);
	
private:
	// These functions all assume validity of CurrentActiveMoveData to perform logic from the perspective of that chunk of properties
	// To that end, the only external entity capable of invoking them is the FScopedMoveLogicExecContext, which guarantees the active move data has been set
	friend class FScopedMoveLogicExecContext;
	
protected:
	/** Called when this move is initially activated  */
	UFUNCTION(BlueprintNativeEvent)
	UE_API void OnStart(const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard);

	/** Called when this move has ended  */
	UFUNCTION(BlueprintNativeEvent)
	UE_API void OnEnd(const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard);

	/** Generate a movement that will be combined with other sources */
	UFUNCTION(BlueprintNativeEvent)
	UE_API bool GenerateMove(const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard, UPARAM(ref) const FJoltMoverTickStartData& StartState, UPARAM(ref) FJoltProposedMove& OutProposedMove);
	
	//@todo: Will need to cache whether to treat the instance data as const (i.e. whether to disregard & complain if someone tries to set the data in BP during that window) 
	UFUNCTION(BlueprintNativeEvent)
	UE_API bool IsFinished(const FJoltMoverTimeStep& TimeStep, const UJoltMoverBlackboard* SimBlackboard);
	
	virtual void OnStart_Implementation(const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard) {}
	virtual void OnEnd_Implementation(const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard) {}
	virtual bool GenerateMove_Implementation(const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard, const FJoltMoverTickStartData& StartState, FJoltProposedMove& OutProposedMove) { return false; }
	UE_API virtual bool IsFinished_Implementation(const FJoltMoverTimeStep& TimeStep, const UJoltMoverBlackboard* SimBlackboard);
	
	/** Accessor to the FJoltLayeredMoveInstancedData established for the execution of a virtual move logic function */
	template <typename MoveDataT = FJoltLayeredMoveInstancedData UE_REQUIRES(std::is_base_of_v<FJoltLayeredMoveInstancedData, MoveDataT>)>
	MoveDataT& AccessExecutionMoveData() const
	{
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
		if (!ensure(InstancedDataStructType))
        {
			UE_LOG(LogJoltMover, Error, TEXT("Move Logic needs an active data struct type. If no data is needed consider using the default move data type."))
        	static MoveDataT GarbageData;
        	return GarbageData;
        }
#endif
		
		check(InstancedDataStructType);
		return static_cast<MoveDataT&>(*CurrentInstancedData);
	}
	
	/**
	 * Gets active move data that is tied to this active move. Use SetActiveMoveData to write active move data.
	 * Note: Make sure break node is the correct data type specified by the layered move logic.
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "JoltMover | Layered Move", DisplayName = "Get Active Move Data", meta = (BlueprintProtected, BlueprintInternalUseOnly, CustomStructureParam = "OutMoveData", DefaultToSelf = "MoveLogic"))
	static bool K2_GetActiveMoveData(UJoltLayeredMoveLogic* MoveLogic, FJoltLayeredMoveInstancedData& OutMoveData);
	DECLARE_FUNCTION(execK2_GetActiveMoveData);
	
	/**
	 * Sets active move data that is tied to this active move. Use GetActiveMoveData to read current active move data.
	 * Note: Make sure make node is the correct data type specified by the layered move logic.
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "JoltMover | Layered Move", DisplayName = "Set Active Move Data", meta = (BlueprintProtected, BlueprintInternalUseOnly, CustomStructureParam = "OutMoveData", DefaultToSelf = "MoveLogic"))
	static void K2_SetActiveMoveData(UJoltLayeredMoveLogic* MoveLogic, const FJoltLayeredMoveInstancedData& OutMoveData);
	DECLARE_FUNCTION(execK2_SetActiveMoveData);
	
	/**
	 * This move will expire after a set amount of time if > 0. If 0, it will be ticked only once, regardless of time step. It will need to be manually ended if < 0.
	 * Note: If changed after starting to a value beneath the current lifetime of the move, it will immediately finish (so if your move finishes early, setting this to 0 is equivalent to returning true from IsFinished())
	 */
	UPROPERTY(BlueprintReadWrite, Category = Mover)
	double DefaultDurationMs = -1.f;
	
	/** Determines how this object's movement contribution should be mixed with others */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Mover)
	EJoltMoveMixMode MixMode = EJoltMoveMixMode::AdditiveVelocity;

	/** Determines if this layered move should take priority over other layered moves when different moves have conflicting overrides - higher numbers taking precedent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Mover)
	uint8 Priority = 0;

	//@todo: Does this need to be a separate struct?
	/** Settings related to velocity applied to the actor after the move has finished */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Mover)
	FJoltLayeredMoveFinishVelocitySettings FinishVelocitySettings;

	//@todo DanH: Fail validation if this isn't set or isn't an FJoltLayeredMoveInstancedData
	UPROPERTY(EditAnywhere, NoClear, BlueprintReadOnly, Category = Mover, meta=(MetaStruct="/Script/JoltMover.LayeredMoveInstancedData"))
	TObjectPtr<UScriptStruct> InstancedDataStructType;
	
private:
	/**
	 * The FJoltLayeredMoveInstancedData provided to each of the base virtual move functions, valid only for the duration a single virtual function execution.
	 * Direct access is only for internal plumbing - use AccessExecutionMoveData() in virtuals to obtain a typed reference to this.
	 */
	TSharedPtr<FJoltLayeredMoveInstancedData> CurrentInstancedData;
};

/**
 * Wrapper to encapsulate the split implementation of a move between a stateless UJoltLayeredMoveLogic* object and an instance of FJoltLayeredMoveInstancedData
 * Those two pieces, in tandem, represent a "whole" functional Layered Move.
 */
USTRUCT()
struct FJoltLayeredMoveInstance
{
	GENERATED_BODY()
	
	UE_API FJoltLayeredMoveInstance();
	UE_API FJoltLayeredMoveInstance(const FJoltLayeredMoveInstance& OtherLayeredMoveInstance);
	UE_API FJoltLayeredMoveInstance(const TSharedRef<FJoltLayeredMoveInstancedData>& InMoveData, UJoltLayeredMoveLogic* InMoveLogic = nullptr);
	
	/** TArray::FindByKey() enablers */
	bool operator==(const TSubclassOf<UJoltLayeredMoveLogic>& LogicClass) const { return MoveLogic && MoveLogic->IsA(LogicClass); }
	bool operator==(const UScriptStruct* MoveDataType) const { return InstanceMoveData->GetScriptStruct()->IsChildOf(MoveDataType); }

	bool HasLogic() const { return MoveLogic != nullptr; }
	const UClass* GetLogicClass() const { return MoveLogic->GetClass(); }
	
	UE_API void StartMove(const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard) const;
	UE_API bool GenerateMove(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard, FJoltProposedMove& OutProposedMove) const;
	UE_API void EndMove(const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard) const;
	UE_API bool IsFinished(const FJoltMoverTimeStep& TimeStep, const UJoltMoverBlackboard* SimBlackboard) const;

	UE_API const FJoltLayeredMoveFinishVelocitySettings& GetFinishVelocitySettings() const;
	UE_API EJoltMoveMixMode GetMixMode() const;
	uint8 GetPriority() const { return MoveLogic->GetPriority(); }
	double GetStartingTimeMs() const { return InstanceMoveData->GetStartSimTimeMs(); }
	bool HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const { return InstanceMoveData->HasGameplayTag(TagToFind, bExactMatch); }

	UE_API FJoltLayeredMoveInstance Clone() const;
	UScriptStruct* GetDataStructType() const { return InstanceMoveData->GetScriptStruct(); }
	void AddReferencedObjects(FReferenceCollector& Collector) const { InstanceMoveData->AddReferencedObjects(Collector); } 

	UE_API void NetSerialize(FArchive& Ar);

	UE_API const UClass* GetSerializedMoveLogicClass() const;

	/**
	 * This function populates the MoveLogic reference of ActiveMoves that don't have any logic classes. This is necessary as
	 * active move data received from NetSerialize doesn't necessarily have the Logic class it was activated with.
	 * Note: Currently we are NetSerializing the MoveLogic class so that we can then populate Logic from using the registered move array
	 * on the MoverComponent but eventually we should use a ID mapping or something better to avoid serializing the MoveLogic class.
	 */
	bool PopulateMissingActiveMoveLogic(const TArray<TObjectPtr<UJoltLayeredMoveLogic>>& RegisteredMoves);
	
private:
	TSharedPtr<FJoltLayeredMoveInstancedData> InstanceMoveData;
	
	/**
	 * Is used in PopulateMissingActiveMoveLogic to help populate logic classes in active moves that were NetSerialized since MoveLogic isn't NetSerialized
	 * TODO: Eventually replace this with some ID/Mapping system so we can NetSerialize that instead of a full UClass
	 */
	UPROPERTY(Transient)
	TSubclassOf<UJoltLayeredMoveLogic> MoveLogicClassType;
	
	//@todo DanH: This exists on the PT for the chaos mover - can/should this still be a UProperty TObjectPtr?
	UPROPERTY(Transient)
	TObjectPtr<UJoltLayeredMoveLogic> MoveLogic;
};

template<>
struct TStructOpsTypeTraits<FJoltLayeredMoveInstance> : public TStructOpsTypeTraitsBase2<FJoltLayeredMoveInstance>
{
	enum
	{
		//WithNetSerializer = true,
		WithCopy = true
	};
};

#undef UE_API
