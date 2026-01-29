// Copyright Epic Games, Inc. All Rights Reserved.


#include "JoltMoverDataModelTypes.h"
#include "Components/PrimitiveComponent.h"
#include "MoveLibrary/JoltBasedMovementUtils.h"
#include "JoltMoverLog.h"



// FJoltCharacterDefaultInputs //////////////////////////////////////////////////////////////

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMoverDataModelTypes)

// FJoltCharacterDefaultInputs //////////////////////////////////////////////////////////////

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMoverDataModelTypes)

void FJoltCharacterDefaultInputs::SetMoveInput(EJoltMoveInputType InMoveInputType, const FVector& InMoveInput)
{
	MoveInputType = InMoveInputType;

	// Limit the precision that we store, so that it matches what is NetSerialized (2 decimal place of precision).
	// This ensures the authoring client, server, and any networking peers are all simulating with the same move input.
	// Note: any change to desired precision must be made here and in NetSerialize
	MoveInput.X = FMath::RoundToFloat(InMoveInput.X * 100.0) / 100.0;
	MoveInput.Y = FMath::RoundToFloat(InMoveInput.Y * 100.0) / 100.0;
	MoveInput.Z = FMath::RoundToFloat(InMoveInput.Z * 100.0) / 100.0;
}


FVector FJoltCharacterDefaultInputs::GetMoveInput_WorldSpace() const
{
	if (bUsingMovementBase && MovementBase)
	{
		FVector MoveInputWorldSpace;
		UJoltBasedMovementUtils::TransformBasedDirectionToWorld(MovementBase, MovementBaseBoneName, MoveInput, MoveInputWorldSpace);
		return MoveInputWorldSpace;
	}

	return MoveInput;	// already in world space
}


FVector FJoltCharacterDefaultInputs::GetOrientationIntentDir_WorldSpace() const
{
	if (bUsingMovementBase && MovementBase)
	{
		FVector OrientIntentDirWorldSpace;
		UJoltBasedMovementUtils::TransformBasedDirectionToWorld(MovementBase, MovementBaseBoneName, OrientationIntent, OrientIntentDirWorldSpace);
		return OrientIntentDirWorldSpace;
	}

	return OrientationIntent;	// already in world space
}


FJoltMoverDataStructBase* FJoltCharacterDefaultInputs::Clone() const
{
	// TODO: ensure that this memory allocation jives with deletion method
	FJoltCharacterDefaultInputs* CopyPtr = new FJoltCharacterDefaultInputs(*this);
	return CopyPtr;
}

bool FJoltCharacterDefaultInputs::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Super::NetSerialize(Ar, Map, bOutSuccess);

	Ar << MoveInputType;

	SerializePackedVector<100, 30>(MoveInput, Ar);	// Note: if you change this serialization, also change in SetMoveInput
	SerializeFixedVector<1, 16>(OrientationIntent, Ar);
	ControlRotation.SerializeCompressedShort(Ar);

	Ar << SuggestedMovementMode;

	Ar.SerializeBits(&bUsingMovementBase, 1);

	if (bUsingMovementBase)
	{
		Ar << MovementBase;
		Ar << MovementBaseBoneName;
	}
	else if (Ar.IsLoading())
	{
		// skip attempts to load movement base properties if flagged as not using a movement base
		MovementBase = nullptr;
		MovementBaseBoneName = NAME_None;
	}

	Ar.SerializeBits(&bIsJumpJustPressed, 1);
	Ar.SerializeBits(&bIsJumpPressed, 1);

	bOutSuccess = true;
	return true;
}


void FJoltCharacterDefaultInputs::ToString(FAnsiStringBuilderBase& Out) const
{
	Super::ToString(Out);

	Out.Appendf("MoveInput: %s (Type %i)\n", TCHAR_TO_ANSI(*MoveInput.ToCompactString()), MoveInputType);
	Out.Appendf("OrientationIntent: X=%.2f Y=%.2f Z=%.2f\n", OrientationIntent.X, OrientationIntent.Y, OrientationIntent.Z);
	Out.Appendf("ControlRotation: P=%.2f Y=%.2f R=%.2f\n", ControlRotation.Pitch, ControlRotation.Yaw, ControlRotation.Roll);
	Out.Appendf("SuggestedMovementMode: %s\n", TCHAR_TO_ANSI(*SuggestedMovementMode.ToString()));

	if (MovementBase)
	{
		Out.Appendf("MovementBase: %s (bone %s)\n", TCHAR_TO_ANSI(*GetNameSafe(MovementBase->GetOwner())), TCHAR_TO_ANSI(*MovementBaseBoneName.ToString()));
	}
	else
	{
		Out.Appendf("MovementBase: none\n");
	}

	Out.Appendf("bIsJumpPressed: %i\tbIsJumpJustPressed: %i\n", bIsJumpPressed, bIsJumpJustPressed);
}

bool FJoltCharacterDefaultInputs::ShouldReconcile(const FJoltMoverDataStructBase& AuthorityState) const
{
	const FJoltCharacterDefaultInputs& TypedAuthority = static_cast<const FJoltCharacterDefaultInputs&>(AuthorityState);
	return *this != TypedAuthority;
}

void FJoltCharacterDefaultInputs::Interpolate(const FJoltMoverDataStructBase& From, const FJoltMoverDataStructBase& To, float Pct)
{
	const FJoltCharacterDefaultInputs& TypedFrom = static_cast<const FJoltCharacterDefaultInputs&>(From);
	const FJoltCharacterDefaultInputs& TypedTo = static_cast<const FJoltCharacterDefaultInputs&>(To);
	
	// Note, this ignores movement base as this is not used by the physics mover
	const FJoltCharacterDefaultInputs* ClosestInputs = Pct < 0.5f ? &TypedFrom : &TypedTo;
	bIsJumpJustPressed = ClosestInputs->bIsJumpJustPressed;
	bIsJumpPressed = ClosestInputs->bIsJumpPressed;
	SuggestedMovementMode = ClosestInputs->SuggestedMovementMode;

	SetMoveInput(ClosestInputs->GetMoveInputType(), FMath::Lerp(TypedFrom.GetMoveInput(), TypedTo.GetMoveInput(), Pct));
	OrientationIntent = FMath::Lerp(TypedFrom.OrientationIntent, TypedTo.OrientationIntent, Pct);
	ControlRotation = FMath::Lerp(TypedFrom.ControlRotation, TypedTo.ControlRotation, Pct);
}

void FJoltCharacterDefaultInputs::Merge(const FJoltMoverDataStructBase& From)
{
	const FJoltCharacterDefaultInputs& TypedFrom = static_cast<const FJoltCharacterDefaultInputs&>(From);
	bIsJumpJustPressed |= TypedFrom.bIsJumpJustPressed;
	bIsJumpPressed |= TypedFrom.bIsJumpPressed;
}

static float CharacterDefaultInputsDecayAmountMultiplier = 1.f;
FAutoConsoleVariableRef CVarCharacterDefaultInputsDecayAmountMultiplier(
	TEXT("JoltMover.Input.CharacterDefaultInputsDecayAmountMultiplier"),
	CharacterDefaultInputsDecayAmountMultiplier,
	TEXT("Multiplier to use when decaying CharacterDefaultInputs."));

void FJoltCharacterDefaultInputs::Decay(float DecayAmount)
{
	DecayAmount *= CharacterDefaultInputsDecayAmountMultiplier;

	MoveInput *= (1.0f - DecayAmount);

	// Single use inputs
	bIsJumpJustPressed = FMath::IsNearlyZero(DecayAmount) ? bIsJumpJustPressed : false;
}

// FJoltUpdatedMotionState //////////////////////////////////////////////////////////////

FJoltMoverDataStructBase* FJoltUpdatedMotionState::Clone() const
{
	// TODO: ensure that this memory allocation jives with deletion method
	FJoltUpdatedMotionState* CopyPtr = new FJoltUpdatedMotionState(*this);
	return CopyPtr;
}

bool FJoltUpdatedMotionState::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Super::NetSerialize(Ar, Map, bOutSuccess);

	SerializePackedVector<100, 30>(Location, Ar);
	SerializeFixedVector<2, 8>(MoveDirectionIntent, Ar);
	SerializePackedVector<10, 16>(Velocity, Ar);
	SerializePackedVector<10, 16>(AngularVelocityDegrees, Ar);
	Orientation.SerializeCompressedShort(Ar);

	// Optional movement base
	bool bIsUsingMovementBase = (Ar.IsSaving() ? MovementBase.IsValid() : false);
	Ar.SerializeBits(&bIsUsingMovementBase, 1);

	if (bIsUsingMovementBase)
	{
		Ar << MovementBase;
		Ar << MovementBaseBoneName;

		SerializePackedVector<100, 30>(MovementBasePos, Ar);
		MovementBaseQuat.NetSerialize(Ar, Map, bOutSuccess);
	}
	else if (Ar.IsLoading())
	{
		MovementBase = nullptr;
	}

	bOutSuccess = true;
	return true;
}

void FJoltUpdatedMotionState::ToString(FAnsiStringBuilderBase& Out) const
{
	Super::ToString(Out);

	Out.Appendf("Loc: X=%.2f Y=%.2f Z=%.2f\n", Location.X, Location.Y, Location.Z);
	Out.Appendf("Intent: X=%.2f Y=%.2f Z=%.2f\n", MoveDirectionIntent.X, MoveDirectionIntent.Y, MoveDirectionIntent.Z);
	Out.Appendf("Vel: X=%.2f Y=%.2f Z=%.2f\n", Velocity.X, Velocity.Y, Velocity.Z);
	Out.Appendf("Ang Vel: X=%.2f Y=%.2f Z=%.2f\n", AngularVelocityDegrees.X, AngularVelocityDegrees.Y, AngularVelocityDegrees.Z);
	Out.Appendf("Orient: P=%.2f Y=%.2f R=%.2f\n", Orientation.Pitch, Orientation.Yaw, Orientation.Roll);

	if (const UPrimitiveComponent* MovementBasePtr = MovementBase.Get())
	{
		Out.Appendf("MovementBase: %s (bone %s)\n", TCHAR_TO_ANSI(*GetNameSafe(MovementBasePtr->GetOwner())), TCHAR_TO_ANSI(*MovementBaseBoneName.ToString()));
		Out.Appendf("    BasePos: %s   BaseRot: %s\n", TCHAR_TO_ANSI(*MovementBasePos.ToCompactString()), TCHAR_TO_ANSI(*MovementBaseQuat.Rotator().ToCompactString()));
	}
	else
	{
		Out.Appendf("MovementBase: none\n");
	}

}


bool FJoltUpdatedMotionState::ShouldReconcile(const FJoltMoverDataStructBase& AuthorityState) const
{
	const FJoltUpdatedMotionState* AuthoritySyncState = static_cast<const FJoltUpdatedMotionState*>(&AuthorityState);
	const float DistErrorTolerance = 5.f;	// JAH TODO: define these elsewhere as CVars or data asset settings

	const bool bAreInDifferentSpaces = !((MovementBase.HasSameIndexAndSerialNumber(AuthoritySyncState->MovementBase)) && (MovementBaseBoneName == AuthoritySyncState->MovementBaseBoneName));

	bool bIsNearEnough = false;
	bool bIsFastEnough = false;
	
	bIsFastEnough = GetVelocity_WorldSpace().Equals(AuthoritySyncState->GetVelocity_WorldSpace(), DistErrorTolerance);

	if (!bAreInDifferentSpaces)
	{
		if (MovementBase.IsValid())
		{
			bIsNearEnough = GetLocation_BaseSpace().Equals(AuthoritySyncState->GetLocation_BaseSpace(), DistErrorTolerance);
		}
		else
		{
			bIsNearEnough = GetLocation_WorldSpace().Equals(AuthoritySyncState->GetLocation_WorldSpace(), DistErrorTolerance);
		}
	}

	if (!bIsNearEnough)
	{
		/*UE_LOG(LogJoltMover, Error, TEXT("Client And Server Locations Are Out Of Sync"))
		UE_LOG(LogJoltMover, Error, TEXT("Client Velocity : %s"), *GetLocation_WorldSpace().ToCompactString())
		UE_LOG(LogJoltMover, Error, TEXT("Server Velocity : %s"), *AuthoritySyncState->GetLocation_WorldSpace().ToCompactString())*/
	}
	
	if (!bIsFastEnough)
	{
		/*UE_LOG(LogJoltMover, Error, TEXT("Client And Server Velocity Are Out Of Sync"))
		UE_LOG(LogJoltMover, Error, TEXT("Client Velocity : %s"), *GetVelocity_WorldSpace().ToCompactString())
		UE_LOG(LogJoltMover, Error, TEXT("Server Velocity : %s"), *AuthoritySyncState->GetVelocity_WorldSpace().ToCompactString())*/
	}

	return /*bAreInDifferentSpaces || */!bIsNearEnough || !bIsFastEnough;
}


void FJoltUpdatedMotionState::Interpolate(const FJoltMoverDataStructBase& From, const FJoltMoverDataStructBase& To, float Pct)
{
	const FJoltUpdatedMotionState* FromState = static_cast<const FJoltUpdatedMotionState*>(&From);
	const FJoltUpdatedMotionState* ToState = static_cast<const FJoltUpdatedMotionState*>(&To);

	// TODO: investigate replacing this threshold with a flag indicating that the state (or parts thereof) isn't intended to be interpolated
	static constexpr float TeleportThreshold = 500.f * 500.f;
	if (FVector::DistSquared(FromState->GetLocation_WorldSpace(), ToState->GetLocation_WorldSpace()) > TeleportThreshold)
	{
		*this = *ToState;
	}
	else
	{

		// No matter what base we started from, we always interpolate into the "To" movement base's space
		MovementBase         = ToState->MovementBase;
		MovementBaseBoneName = ToState->MovementBaseBoneName;
		MovementBasePos      = ToState->MovementBasePos;
		MovementBaseQuat     = ToState->MovementBaseQuat;

		FVector FromLocation_ToSpace, FromIntent_ToSpace, FromVelocity_ToSpace, FromAngularVelocity_ToSpace;
		FRotator FromOrientation_ToSpace;

	
		// Bases match (or not using based movement at all)
		if (FromState->MovementBase.HasSameIndexAndSerialNumber(ToState->MovementBase) && FromState->MovementBaseBoneName == ToState->MovementBaseBoneName)
		{
			if (FromState->MovementBase.IsValid())
			{
				MovementBasePos = FMath::Lerp(FromState->MovementBasePos, ToState->MovementBasePos, Pct);
				MovementBaseQuat = FQuat::Slerp(FromState->MovementBaseQuat, ToState->MovementBaseQuat, Pct);
			}

			FromLocation_ToSpace    = FromState->Location;
			FromIntent_ToSpace      = FromState->MoveDirectionIntent;
			FromVelocity_ToSpace    = FromState->Velocity;
			FromAngularVelocity_ToSpace    = FromState->AngularVelocityDegrees;
			FromOrientation_ToSpace = FromState->Orientation;
		}
		else if (ToState->MovementBase.IsValid())	// if moving onto a different base, regardless of coming from a prior base or not
		{
			UJoltBasedMovementUtils::TransformLocationToLocal(ToState->MovementBasePos, ToState->MovementBaseQuat, FromState->GetLocation_WorldSpace(), OUT FromLocation_ToSpace);
			UJoltBasedMovementUtils::TransformDirectionToLocal(ToState->MovementBaseQuat, FromState->GetIntent_WorldSpace(), OUT FromIntent_ToSpace);
			UJoltBasedMovementUtils::TransformDirectionToLocal(ToState->MovementBaseQuat, FromState->GetVelocity_WorldSpace(), OUT FromVelocity_ToSpace);
			UJoltBasedMovementUtils::TransformDirectionToLocal(ToState->MovementBaseQuat, FromState->GetAngularVelocityDegrees_WorldSpace(), OUT FromAngularVelocity_ToSpace);
			UJoltBasedMovementUtils::TransformRotatorToLocal(ToState->MovementBaseQuat, FromState->GetOrientation_WorldSpace(), OUT FromOrientation_ToSpace);
		}
		else if (FromState->MovementBase.IsValid())	// if leaving a base
		{
			FromLocation_ToSpace	= FromState->GetLocation_WorldSpace();
			FromIntent_ToSpace		= FromState->GetIntent_WorldSpace();
			FromVelocity_ToSpace	= FromState->GetVelocity_WorldSpace();
			FromAngularVelocity_ToSpace	= FromState->GetAngularVelocityDegrees_WorldSpace();
			FromOrientation_ToSpace = FromState->GetOrientation_WorldSpace();
		}

		Location			= FMath::Lerp(FromLocation_ToSpace,		ToState->Location, Pct);
		MoveDirectionIntent = FMath::Lerp(FromIntent_ToSpace,		ToState->MoveDirectionIntent, Pct);
		Velocity			= FMath::Lerp(FromVelocity_ToSpace,		ToState->Velocity, Pct);
		AngularVelocityDegrees = FMath::Lerp(FromAngularVelocity_ToSpace,ToState->AngularVelocityDegrees, Pct);
		Orientation			= FMath::Lerp(FromOrientation_ToSpace,	ToState->Orientation, Pct);

	}
}


void FJoltUpdatedMotionState::SetTransforms_WorldSpace(const FVector& WorldLocation, const FRotator& WorldOrient, const FVector& WorldVelocity, const FVector& WorldAngularVelocityDegrees, UPrimitiveComponent* Base, const FName& BaseBone)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FJoltUpdatedMotionState::SetTransforms_WorldSpace);
	if (Base && SetMovementBase(Base, BaseBone))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FJoltUpdatedMotionState::TransformToLocal);
		UJoltBasedMovementUtils::TransformLocationToLocal(  MovementBasePos,  MovementBaseQuat, WorldLocation, OUT Location);
		UJoltBasedMovementUtils::TransformRotatorToLocal(   MovementBaseQuat, WorldOrient, OUT Orientation);
		UJoltBasedMovementUtils::TransformDirectionToLocal( MovementBaseQuat, WorldVelocity, OUT Velocity);
		UJoltBasedMovementUtils::TransformDirectionToLocal( MovementBaseQuat, WorldAngularVelocityDegrees, OUT AngularVelocityDegrees);
	}
	else
	{
		
		TRACE_CPUPROFILER_EVENT_SCOPE(FJoltUpdatedMotionState::SetDirectly);
		if (Base)
		{
			UE_LOG(LogJoltMover, Warning, TEXT("Failed to set base as %s. Falling back to world space movement"), *GetNameSafe(Base->GetOwner()));
		}

		Location = UE::JoltNetQuant::QuantizePackedVector<100>(WorldLocation);
		Orientation = UE::JoltNetQuant::QuantizeRotatorCompressedShort(WorldOrient);
		Velocity = UE::JoltNetQuant::QuantizePackedVector<10>(WorldVelocity);
		AngularVelocityDegrees = UE::JoltNetQuant::QuantizePackedVector<10>(WorldAngularVelocityDegrees);
	}
}

void FJoltUpdatedMotionState::SetLinearVelocity_WorldSpace(const FVector& LinearVelocity)
{
	Velocity = UE::JoltNetQuant::QuantizePackedVector<10>(LinearVelocity);
}

void FJoltUpdatedMotionState::SetAngularVelocity_WorldSpace(const FVector& LinearVelocity)
{
	AngularVelocityDegrees = UE::JoltNetQuant::QuantizePackedVector<10>(LinearVelocity);
}

void FJoltUpdatedMotionState::SetLinearAndAngularVelocity_WorldSpace(const FVector& Linear, const FVector& Angular)
{
	Velocity = UE::JoltNetQuant::QuantizePackedVector<10>(Linear);
	AngularVelocityDegrees = UE::JoltNetQuant::QuantizePackedVector<10>(Angular);
}


bool FJoltUpdatedMotionState::SetMovementBase(UPrimitiveComponent* Base, FName BaseBone)
{
	MovementBase = Base;
	MovementBaseBoneName = BaseBone;

	const bool bDidCaptureBaseTransform = UpdateCurrentMovementBase();
	return !Base || bDidCaptureBaseTransform;
}


bool FJoltUpdatedMotionState::UpdateCurrentMovementBase()
{
	bool bDidGetBaseTransform = false;

	if (const UPrimitiveComponent* MovementBasePtr = MovementBase.Get())
	{
		bDidGetBaseTransform = UJoltBasedMovementUtils::GetMovementBaseTransform(MovementBasePtr, MovementBaseBoneName, OUT MovementBasePos, OUT MovementBaseQuat);
	}

	if (!bDidGetBaseTransform)
	{
		MovementBase = nullptr;
		MovementBaseBoneName = NAME_None;
		MovementBasePos = FVector::ZeroVector;
		MovementBaseQuat = FQuat::Identity;
	}

	return bDidGetBaseTransform;
}

bool FJoltUpdatedMotionState::IsNearlyEqual(const FJoltUpdatedMotionState& Other) const
{
	const bool bHasSameBaseBaseInfo = (!MovementBase.IsValid() && !Other.MovementBase.IsValid()) ||
											(MovementBase == Other.MovementBase && 
											 MovementBaseBoneName == Other.MovementBaseBoneName &&
											 (MovementBasePos - Other.MovementBasePos).IsNearlyZero() && 
											 MovementBaseQuat.Equals(Other.MovementBaseQuat));

	return (Location-Other.Location).IsNearlyZero() &&
		(Orientation-Other.Orientation).IsNearlyZero() &&
		(Velocity-Other.Velocity).IsNearlyZero() &&(AngularVelocityDegrees-Other.AngularVelocityDegrees).IsNearlyZero() &&
		(MoveDirectionIntent-Other.MoveDirectionIntent).IsNearlyZero() &&
			bHasSameBaseBaseInfo;
}

FVector FJoltUpdatedMotionState::GetLocation_WorldSpace() const
{
	if (MovementBase.IsValid())
	{
		return FTransform(MovementBaseQuat, MovementBasePos).TransformPositionNoScale(Location);
	}

	return Location; // if no base, assumed to be in world space
}

FVector FJoltUpdatedMotionState::GetLocation_BaseSpace() const
{
	return Location;
}


FVector FJoltUpdatedMotionState::GetIntent_WorldSpace() const
{
	if (MovementBase.IsValid())
	{
		return MovementBaseQuat.RotateVector(MoveDirectionIntent);
	}

	return MoveDirectionIntent; // if no base, assumed to be in world space
}

FVector FJoltUpdatedMotionState::GetIntent_BaseSpace() const
{
	return MoveDirectionIntent;
}

FVector FJoltUpdatedMotionState::GetVelocity_WorldSpace() const
{
	if (MovementBase.IsValid())
	{
		return MovementBaseQuat.RotateVector(Velocity);
	}

	return Velocity; // if no base, assumed to be in world space
}

FVector FJoltUpdatedMotionState::GetVelocity_BaseSpace() const
{
	return Velocity;
}


FRotator FJoltUpdatedMotionState::GetOrientation_WorldSpace() const
{
	if (MovementBase.IsValid())
	{
		return (MovementBaseQuat * FQuat(Orientation)).Rotator();
	}

	return Orientation; // if no base, assumed to be in world space
}


FRotator FJoltUpdatedMotionState::GetOrientation_BaseSpace() const
{
	return Orientation;
}

FTransform FJoltUpdatedMotionState::GetTransform_WorldSpace() const
{
	if (MovementBase.IsValid())
	{
		return FTransform(Orientation, Location) * FTransform(MovementBaseQuat, MovementBasePos);
	}

	return FTransform(Orientation, Location);
}

FTransform FJoltUpdatedMotionState::GetTransform_BaseSpace() const
{
	return FTransform(Orientation, Location);
}

FVector FJoltUpdatedMotionState::GetAngularVelocityDegrees_WorldSpace() const
{
	if (MovementBase.IsValid())
	{
		return MovementBaseQuat.RotateVector(AngularVelocityDegrees);
	}

	return AngularVelocityDegrees; // if no base, assumed to be in world space
}

FVector FJoltUpdatedMotionState::GetAngularVelocityDegrees_BaseSpace() const
{
	return AngularVelocityDegrees;
}

FTransform FJoltUpdatedMotionState::GetTransform_WorldSpace_Quantized() const
{
	const FVector LQuantized = UE::JoltNetQuant::QuantizePackedVector<100>(GetLocation_BaseSpace());
	const FRotator RQuantized = UE::JoltNetQuant::QuantizeRotatorCompressedShort(GetOrientation_BaseSpace());
	const FVector BLQuantized = UE::JoltNetQuant::QuantizePackedVector<100>(MovementBasePos);
	const FRotator BRQuantized = UE::JoltNetQuant::QuantizeRotatorCompressedShort(MovementBaseQuat.Rotator());
	if (MovementBase.IsValid())
	{
		return FTransform(RQuantized, LQuantized) * FTransform(BRQuantized, BLQuantized);
	}

	return FTransform(RQuantized, LQuantized);
}

FVector FJoltUpdatedMotionState::GetLocation_WorldSpace_Quantized() const
{
	const FVector LocalQ = UE::JoltNetQuant::QuantizePackedVector<100>(GetLocation_BaseSpace());
	if (MovementBase.IsValid())
	{
		return FTransform(MovementBaseQuat, MovementBasePos).TransformPositionNoScale(LocalQ);
	}
	return LocalQ;
}

FVector FJoltUpdatedMotionState::GetVelocity_WorldSpace_Quantized() const
{
	const FVector LocalQ = UE::JoltNetQuant::QuantizePackedVector<10>(GetVelocity_BaseSpace());
	if (MovementBase.IsValid())
	{
		return MovementBaseQuat.RotateVector(LocalQ);
	}
	return LocalQ;
}

FVector FJoltUpdatedMotionState::GetAngularVelocityDegrees_WorldSpace_Quantized() const
{
	const FVector LocalQ = UE::JoltNetQuant::QuantizePackedVector<10>(GetAngularVelocityDegrees_BaseSpace());
	if (MovementBase.IsValid())
	{
		return MovementBaseQuat.RotateVector(LocalQ);
	}
	return LocalQ;
}

FRotator FJoltUpdatedMotionState::GetOrientation_WorldSpace_Quantized() const
{
	const FRotator LocalQ = UE::JoltNetQuant::QuantizeRotatorCompressedShort(GetOrientation_BaseSpace());
	if (MovementBase.IsValid())
	{
		return (MovementBaseQuat * FQuat(LocalQ)).Rotator();
	}
	return LocalQ;
}

// UJoltMoverDataModelBlueprintLibrary ///////////////////////////////////////////////////

void UJoltMoverDataModelBlueprintLibrary::SetDirectionalInput(FJoltCharacterDefaultInputs& Inputs, const FVector& DirectionInput)
{
	Inputs.SetMoveInput(EJoltMoveInputType::DirectionalIntent, DirectionInput);
}

void UJoltMoverDataModelBlueprintLibrary::SetVelocityInput(FJoltCharacterDefaultInputs& Inputs, const FVector& VelocityInput)
{
	Inputs.SetMoveInput(EJoltMoveInputType::Velocity, VelocityInput);
}

FVector UJoltMoverDataModelBlueprintLibrary::GetMoveDirectionIntentFromInputs(const FJoltCharacterDefaultInputs& Inputs)
{
	return Inputs.GetMoveInput_WorldSpace();
}

FVector UJoltMoverDataModelBlueprintLibrary::GetLocationFromSyncState(const FJoltUpdatedMotionState& SyncState)
{
	return SyncState.GetLocation_WorldSpace_Quantized();
}

FVector UJoltMoverDataModelBlueprintLibrary::GetMoveDirectionIntentFromSyncState(const FJoltUpdatedMotionState& SyncState)
{
	return SyncState.GetIntent_WorldSpace();
}

FVector UJoltMoverDataModelBlueprintLibrary::GetVelocityFromSyncState(const FJoltUpdatedMotionState& SyncState)
{
	return SyncState.GetVelocity_WorldSpace_Quantized();
}

FVector UJoltMoverDataModelBlueprintLibrary::GetAngularVelocityDegreesFromSyncState(const FJoltUpdatedMotionState& SyncState)
{
	return SyncState.GetAngularVelocityDegrees_WorldSpace_Quantized();
}

FRotator UJoltMoverDataModelBlueprintLibrary::GetOrientationFromSyncState(const FJoltUpdatedMotionState& SyncState)
{
	return SyncState.GetOrientation_WorldSpace_Quantized();
}
