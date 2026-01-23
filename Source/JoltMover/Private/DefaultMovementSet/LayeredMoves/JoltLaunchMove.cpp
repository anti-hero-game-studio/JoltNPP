// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultMovementSet/LayeredMoves/JoltLaunchMove.h"
#include "JoltMoverComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltLaunchMove)

void FJoltLaunchMoveData::ActivateFromContext(const FJoltLayeredMoveActivationParams* ActivationParams)
{
	if (ActivationParams)
	{
		if (const FJoltLaunchMoveActivationParams* LaunchMoveActivationParams = static_cast<const FJoltLaunchMoveActivationParams*>(ActivationParams))
		{
			LaunchVelocity = LaunchMoveActivationParams->LaunchVelocity;
			DurationMs = LaunchMoveActivationParams->DurationMs;
			ForceMovementMode = LaunchMoveActivationParams->ForceMovementMode;
		}
	}
}

void FJoltLaunchMoveData::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	SerializePackedVector<10, 16>(LaunchVelocity, Ar);

	bool bUsingForcedMovementMode = !ForceMovementMode.IsNone();

	Ar.SerializeBits(&bUsingForcedMovementMode, 1);

	if (bUsingForcedMovementMode)
	{
		Ar << ForceMovementMode;
	}
	else
	{
		ForceMovementMode = NAME_None;
	}
}

ULaunchMoveLogic::ULaunchMoveLogic()
{
	DefaultDurationMs = 0.f;
	MixMode = EJoltMoveMixMode::OverrideVelocity;
	InstancedDataStructType = FJoltLaunchMoveData::StaticStruct();
}

bool ULaunchMoveLogic::GenerateMove_Implementation(const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard, const FJoltMoverTickStartData& StartState, FJoltProposedMove& OutProposedMove)
{
	const FJoltLaunchMoveData& LaunchMoveData = AccessExecutionMoveData<FJoltLaunchMoveData>();
	
	OutProposedMove.MixMode = MixMode;
	OutProposedMove.LinearVelocity = LaunchMoveData.LaunchVelocity;
	OutProposedMove.PreferredMode = LaunchMoveData.ForceMovementMode;

	return true;
}

FJoltLayeredMove_Launch::FJoltLayeredMove_Launch()
{
	DurationMs = 0.f;
	MixMode = EJoltMoveMixMode::OverrideVelocity;
}

bool FJoltLayeredMove_Launch::GenerateMove(const FJoltMoverTickStartData& SimState, const FJoltMoverTimeStep& TimeStep, const UJoltMoverComponent* MoverComp, UJoltMoverBlackboard* SimBlackboard, FJoltProposedMove& OutProposedMove)
{
	OutProposedMove.MixMode = MixMode;
	OutProposedMove.LinearVelocity = LaunchVelocity;
	OutProposedMove.PreferredMode = ForceMovementMode;

	return true;
}

FJoltLayeredMoveBase* FJoltLayeredMove_Launch::Clone() const
{
	FJoltLayeredMove_Launch* CopyPtr = new FJoltLayeredMove_Launch(*this);
	return CopyPtr;
}

void FJoltLayeredMove_Launch::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	SerializePackedVector<10, 16>(LaunchVelocity, Ar);

	bool bUsingForcedMovementMode = !ForceMovementMode.IsNone();

	Ar.SerializeBits(&bUsingForcedMovementMode, 1);

	if (bUsingForcedMovementMode)
	{
		Ar << ForceMovementMode;
	}

}

UScriptStruct* FJoltLayeredMove_Launch::GetScriptStruct() const
{
	return FJoltLayeredMove_Launch::StaticStruct();
}

FString FJoltLayeredMove_Launch::ToSimpleString() const
{
	return FString::Printf(TEXT("Launch"));
}

void FJoltLayeredMove_Launch::AddReferencedObjects(class FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}
