// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "MoveLibrary/JoltMovementUtilsTypes.h"
#include "JoltLayeredMoveGroup.generated.h"

#define UE_API JOLTMOVER_API

struct FJoltLayeredMoveInstance;
struct FJoltLayeredMoveInstancedData;
struct FJoltProposedMove;
class UJoltMovementMixer;
struct FJoltMoverTickStartData;
struct FJoltMoverTimeStep;
class UJoltMoverBlackboard;
class UJoltLayeredMoveLogic;

/**
 * The group of information about currently active and queued moves.
 * This replicates info for FJoltLayeredMoveInstancedData only - it is expected that the corresponding UJoltLayeredMoveLogic is 
 * already registered with the mover component.
 */
USTRUCT(BlueprintType)
struct FJoltLayeredMoveInstanceGroup
{
	GENERATED_BODY()

	UE_API FJoltLayeredMoveInstanceGroup();
	UE_API FJoltLayeredMoveInstanceGroup& operator=(const FJoltLayeredMoveInstanceGroup& Other);
	UE_API bool operator==(const FJoltLayeredMoveInstanceGroup& Other) const;
	bool operator!=(const FJoltLayeredMoveInstanceGroup& Other) const { return !operator==(Other); }

	/** Checks only whether there are matching LayeredMoves, but NOT necessarily identical states of each move */
	UE_API bool HasSameContents(const FJoltLayeredMoveInstanceGroup& Other) const;
	
	UE_API bool GenerateMixedMove(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, UJoltMovementMixer& MovementMixer, UJoltMoverBlackboard* SimBlackboard, FJoltProposedMove& OutMixedMove);
	UE_API void ApplyResidualVelocity(FJoltProposedMove& InOutProposedMove);
	UE_API void NetSerialize(FArchive& Ar, uint8 MaxNumMovesToSerialize = MAX_uint8);
	UE_API void AddStructReferencedObjects(FReferenceCollector& Collector) const;
	UE_API void ResetResidualVelocity();
	UE_API void Reset();

	/**
	 * Loops through all Queued and Active moves and populates any missing MoveLogic using FJoltLayeredMoveInstance::PopulateMissingActiveMoveLogic.
	 * See FJoltLayeredMoveInstance::PopulateMissingActiveMoveLogic function for more details.
	 */
	UE_API void PopulateMissingActiveMoveLogic(const TArray<TObjectPtr<UJoltLayeredMoveLogic>>& RegisteredMoves);
	
	/** Adds the active move to the queued array of the move group */
	UE_API void QueueLayeredMove(const TSharedPtr<FJoltLayeredMoveInstance>& Move);
	
	/** @return True if there are any active or queued moves in this group */
	bool HasAnyMoves() const { return (!ActiveMoves.IsEmpty() || !QueuedMoves.IsEmpty()); }
	
	/** @return True if there is at least one layered move that's either active or queued and is associated with the provided logic or data type */
	template <typename MoveElementT UE_REQUIRES(std::is_base_of_v<FJoltLayeredMoveInstancedData, MoveElementT> || std::is_base_of_v<UJoltLayeredMoveLogic, MoveElementT>)>
	bool HasMove() const
	{
		return FindActiveMove<MoveElementT>() || FindQueuedMove<MoveElementT>();
	}
	
	/** Get a simplified string representation of this group. Typically for debugging. */
	UE_API FString ToSimpleString() const;
	
	/** Returns the first active layered move associated with logic of the specified type, if one exists */
	template <typename MoveLogicT = UJoltLayeredMoveLogic UE_REQUIRES(std::is_base_of_v<UJoltLayeredMoveLogic, MoveLogicT>)>
	const FJoltLayeredMoveInstance* FindActiveMove(TSubclassOf<UJoltLayeredMoveLogic> MoveLogicClass = MoveLogicT::StaticClass()) const
	{
		return PrivateFindActiveMove(MoveLogicClass);
	}
	
	/** Returns the first active layered move using data of the specified type, if one exists */
	template <typename MoveDataT = FJoltLayeredMoveInstancedData UE_REQUIRES(std::is_base_of_v<FJoltLayeredMoveInstancedData, MoveDataT>)>
	const FJoltLayeredMoveInstance* FindActiveMove(const UScriptStruct* MoveDataType = MoveDataT::StaticStruct()) const
	{
		return PrivateFindActiveMove(MoveDataType);
	}
	
	/** Returns the first queued layered move associated with logic of the specified type, if one exists */
	template <typename MoveLogicT = UJoltLayeredMoveLogic UE_REQUIRES(std::is_base_of_v<UJoltLayeredMoveLogic, MoveLogicT>)>
	const FJoltLayeredMoveInstance* FindQueuedMove(TSubclassOf<UJoltLayeredMoveLogic> MoveLogicClass = MoveLogicT::StaticClass()) const
	{
		return PrivateFindQueuedMove(MoveLogicClass);
	}

	/** Returns the first queued layered move using data of the specified type, if one exists */
	template <typename MoveDataT = FJoltLayeredMoveInstancedData UE_REQUIRES(std::is_base_of_v<FJoltLayeredMoveInstancedData, MoveDataT>)>
	const FJoltLayeredMoveInstance* FindQueuedMove(const UScriptStruct* MoveDataType = MoveDataT::StaticStruct()) const
	{
		return PrivateFindQueuedMove(MoveDataType);
	}

	/** Cancel any active or queued moves with a matching tag */
	void CancelMovesByTag(FGameplayTag Tag, bool bRequireExactMatch); 

	// Clears out any finished or invalid active moves and adds any queued moves to the active moves
	UE_API void FlushMoveArrays(const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard);
	
protected:
	// Helper function for gathering any residual velocity settings from layered moves that just ended
	void ProcessFinishedMove(const FJoltLayeredMoveInstance& Move, bool& bResidualVelocityOverriden, bool& bClampVelocityOverriden);
	
	/** Moves that are currently active in this group */
	TArray<TSharedPtr<FJoltLayeredMoveInstance>> ActiveMoves;

	/** Moves that are queued to become active next sim frame */
	TArray<TSharedPtr<FJoltLayeredMoveInstance>> QueuedMoves;

private:
	const FJoltLayeredMoveInstance* PrivateFindActiveMove(const TSubclassOf<UJoltLayeredMoveLogic>& MoveLogicClass) const;
	const FJoltLayeredMoveInstance* PrivateFindActiveMove(const UScriptStruct* MoveDataType) const;
	const FJoltLayeredMoveInstance* PrivateFindQueuedMove(const TSubclassOf<UJoltLayeredMoveLogic>& MoveLogicClass) const;
	const FJoltLayeredMoveInstance* PrivateFindQueuedMove(const UScriptStruct* MoveDataType) const;
	
	//@todo DanH: Maybe these should be grouped in a struct?
	/**
	 * Clamps an actors velocity to this value when a layered move ends. This expects Value >= 0.
	 * Note: This is set automatically when a layered move has ended based off of end velocity settings - it is not meant to be set by the user see @FJoltLayeredMoveFinishVelocitySettings
	 */
	UPROPERTY(NotReplicated, VisibleAnywhere, BlueprintReadOnly, Category = Mover, meta = (AllowPrivateAccess))
	float ResidualClamping;

	/**
	 * If true ResidualVelocity will be the next velocity used for this actor
	 * Note: This is set automatically when a layered move has ended based off of end velocity settings - it is not meant to be set by the user see @FJoltLayeredMoveFinishVelocitySettings
	 */
	UPROPERTY(NotReplicated, VisibleAnywhere, BlueprintReadOnly, Category = Mover, meta = (AllowPrivateAccess))
	bool bApplyResidualVelocity;

	/**
	 * If bApplyResidualVelocity is true this actors velocity will be set to this.
	 * Note: This is set automatically when a layered move has ended based off of end velocity settings - it is not meant to be set by the user see @FJoltLayeredMoveFinishVelocitySettings
	 */
	UPROPERTY(NotReplicated, VisibleAnywhere, BlueprintReadOnly, Category = Mover, meta = (AllowPrivateAccess))
	FVector ResidualVelocity;

	/** Used during simulation to cancel any moves that match a tag */
	TArray<TPair<FGameplayTag, bool>> TagCancellationRequests;
};

template<>
struct TStructOpsTypeTraits<FJoltLayeredMoveInstanceGroup> : public TStructOpsTypeTraitsBase2<FJoltLayeredMoveInstanceGroup>
{
	enum
	{
		WithCopy = true,
		//WithNetSerializer = true,
		WithIdenticalViaEquality = true,
		WithAddStructReferencedObjects = true,
	};
};

#undef UE_API
