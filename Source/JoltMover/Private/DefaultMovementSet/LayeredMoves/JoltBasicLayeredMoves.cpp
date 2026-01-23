// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultMovementSet/LayeredMoves/JoltBasicLayeredMoves.h"
#include "JoltMoverSimulationTypes.h"
#include "JoltMoverComponent.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveFloat.h"
#include "JoltMoverLog.h"
#include "DefaultMovementSet/Settings/JoltCommonLegacyMovementSettings.h"
#include "MoveLibrary/JoltMovementUtils.h"


// -------------------------------------------------------------------
// FJoltLayeredMove_LinearVelocity
// -------------------------------------------------------------------

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltBasicLayeredMoves)

FJoltLayeredMove_LinearVelocity::FJoltLayeredMove_LinearVelocity()
	: Velocity(FVector::ZeroVector)
	, MagnitudeOverTime(nullptr)
	, SettingsFlags(0)
{
}

bool FJoltLayeredMove_LinearVelocity::GenerateMove(const FJoltMoverTickStartData& SimState, const FJoltMoverTimeStep& TimeStep, const UJoltMoverComponent* MoverComp, UJoltMoverBlackboard* SimBlackboard, FJoltProposedMove& OutProposedMove)
{ 
	const FJoltUpdatedMotionState* SyncState = SimState.SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>();
	check(SyncState);

	const float DeltaSeconds = TimeStep.StepMs * 0.001f;

	// Convert starting velocity based on starting orientation, if settings call for it
	if (SettingsFlags & (uint8)EJoltLayeredMove_ConstantVelocitySettingsFlags::VelocityStartRelative &&
		StartSimTimeMs == TimeStep.BaseSimTimeMs)
	{
		SettingsFlags &= ~(uint8)EJoltLayeredMove_ConstantVelocitySettingsFlags::VelocityStartRelative;
		Velocity = SyncState->GetOrientation_WorldSpace().RotateVector(Velocity);
	}

	FVector VelocityThisFrame = Velocity;

	// Put velocity into worldspace
	if (SettingsFlags & (uint8)EJoltLayeredMove_ConstantVelocitySettingsFlags::VelocityAlwaysRelative)
	{
		VelocityThisFrame = SyncState->GetOrientation_WorldSpace().RotateVector(Velocity);
	}

	if (MagnitudeOverTime && DurationMs > 0)
	{
		const double TimeValue = FMath::Clamp((TimeStep.BaseSimTimeMs - StartSimTimeMs) / DurationMs, 0.0, 1.0);
		const float TimeFactor = MagnitudeOverTime->GetFloatValue(TimeValue);
		VelocityThisFrame *= TimeFactor;
	}
	
	OutProposedMove.LinearVelocity = VelocityThisFrame;

	return true;
}

FJoltLayeredMoveBase* FJoltLayeredMove_LinearVelocity::Clone() const
{
	FJoltLayeredMove_LinearVelocity* CopyPtr = new FJoltLayeredMove_LinearVelocity(*this);
	return CopyPtr;
}

void FJoltLayeredMove_LinearVelocity::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	SerializePackedVector<10, 16>(Velocity, Ar);
	Ar << SettingsFlags;
	Ar << MagnitudeOverTime;
}

UScriptStruct* FJoltLayeredMove_LinearVelocity::GetScriptStruct() const
{
	return FJoltLayeredMove_LinearVelocity::StaticStruct();
}

FString FJoltLayeredMove_LinearVelocity::ToSimpleString() const
{
	return FString::Printf(TEXT("LinearVelocity"));
}

void FJoltLayeredMove_LinearVelocity::AddReferencedObjects(class FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}




// -------------------------------------------------------------------
// FJoltLayeredMove_JumpImpulseOverDuration
// -------------------------------------------------------------------

FJoltLayeredMove_JumpImpulseOverDuration::FJoltLayeredMove_JumpImpulseOverDuration() 
	: UpwardsSpeed(0.f)
{
	DurationMs = 100.f;
	MixMode = EJoltMoveMixMode::OverrideVelocity;
}


bool FJoltLayeredMove_JumpImpulseOverDuration::GenerateMove(const FJoltMoverTickStartData& SimState, const FJoltMoverTimeStep& TimeStep, const UJoltMoverComponent* MoverComp, UJoltMoverBlackboard* SimBlackboard, FJoltProposedMove& OutProposedMove)
{	
	const FJoltUpdatedMotionState* SyncState = SimState.SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>();
	check(SyncState);

	const FVector UpDir = MoverComp->GetUpDirection();

	const FVector ImpulseVelocity = UpDir * UpwardsSpeed;

	// Jump impulse overrides vertical velocity while maintaining the rest
	if (MixMode == EJoltMoveMixMode::OverrideVelocity)
	{
		const FVector PriorVelocityWS = SyncState->GetVelocity_WorldSpace();
		const FVector StartingNonUpwardsVelocity = PriorVelocityWS - PriorVelocityWS.ProjectOnToNormal(UpDir);

		OutProposedMove.LinearVelocity = StartingNonUpwardsVelocity + ImpulseVelocity;
	}
	else
	{
		ensureMsgf(false, TEXT("JumpImpulse layered move only supports Override Velocity mix mode and was queued with a different mix mode. Layered move will do nothing."));
		return false;
	}

	return true;
}

FJoltLayeredMoveBase* FJoltLayeredMove_JumpImpulseOverDuration::Clone() const
{
	FJoltLayeredMove_JumpImpulseOverDuration* CopyPtr = new FJoltLayeredMove_JumpImpulseOverDuration(*this);
	return CopyPtr;
}

void FJoltLayeredMove_JumpImpulseOverDuration::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	Ar << UpwardsSpeed;
}

UScriptStruct* FJoltLayeredMove_JumpImpulseOverDuration::GetScriptStruct() const
{
	return FJoltLayeredMove_JumpImpulseOverDuration::StaticStruct();
}

FString FJoltLayeredMove_JumpImpulseOverDuration::ToSimpleString() const
{
	return FString::Printf(TEXT("JumpImpulseOverDuration"));
}

void FJoltLayeredMove_JumpImpulseOverDuration::AddReferencedObjects(class FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}

// -------------------------------------------------------------------
// FJoltLayeredMove_JumpTo
// -------------------------------------------------------------------

FJoltLayeredMove_JumpTo::FJoltLayeredMove_JumpTo()
	: JumpDistance(-1.0f)
	, JumpHeight(-1.0f)
	, bUseActorRotation(true)
	, JumpRotation(ForceInitToZero)
	, PathOffsetCurve(nullptr)
	, TimeMappingCurve(nullptr)
{
	DurationMs = 1.f;
	MixMode = EJoltMoveMixMode::OverrideVelocity;
}

bool FJoltLayeredMove_JumpTo::GenerateMove(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, const UJoltMoverComponent* MoverComp, UJoltMoverBlackboard* SimBlackboard, FJoltProposedMove& OutProposedMove)
{
	const FJoltUpdatedMotionState* SyncState = StartState.SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>();
	check(SyncState);

	if (DurationMs == 0)
	{
		ensureMsgf(false, TEXT("JumpTo expected a non-zero duration."));
		return false;
	}
	
	const float DeltaSeconds = TimeStep.StepMs / 1000.f;
	float CurrentTimeFraction = (TimeStep.BaseSimTimeMs - StartSimTimeMs) / DurationMs;
	float TargetTimeFraction = CurrentTimeFraction + DeltaSeconds;
	
	// If we're beyond specified duration, we need to re-map times so that
	// we continue our desired ending velocity
	if (TargetTimeFraction > 1.f)
	{
		float TimeFractionPastAllowable = TargetTimeFraction - 1.0f;
		TargetTimeFraction -= TimeFractionPastAllowable;
		CurrentTimeFraction -= TimeFractionPastAllowable;
	}

	float CurrentMoveFraction = CurrentTimeFraction;
	float TargetMoveFraction = TargetTimeFraction;

	if (TimeMappingCurve)
	{
		CurrentMoveFraction = EvaluateFloatCurveAtFraction(*TimeMappingCurve, CurrentMoveFraction);
		TargetMoveFraction  = EvaluateFloatCurveAtFraction(*TimeMappingCurve, TargetMoveFraction);
	}

	const FRotator Rotation = bUseActorRotation ? SyncState->GetOrientation_WorldSpace() : JumpRotation;
	const FVector CurrentRelativeLocation = GetRelativeLocation(CurrentMoveFraction, Rotation);
	const FVector TargetRelativeLocation = GetRelativeLocation(TargetMoveFraction, Rotation);

	OutProposedMove.LinearVelocity = (TargetRelativeLocation - CurrentRelativeLocation) / DeltaSeconds;;
	if (const TObjectPtr<const UJoltCommonLegacyMovementSettings> CommonLegacySettings = MoverComp->FindSharedSettings<UJoltCommonLegacyMovementSettings>())
	{
		OutProposedMove.PreferredMode = CommonLegacySettings->AirMovementModeName;
	}
	
	return true;
}

FJoltLayeredMoveBase* FJoltLayeredMove_JumpTo::Clone() const
{
	FJoltLayeredMove_JumpTo* CopyPtr = new FJoltLayeredMove_JumpTo(*this);
	return CopyPtr;
}

void FJoltLayeredMove_JumpTo::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	Ar << JumpDistance;
	Ar << JumpHeight;
	Ar.SerializeBits(&bUseActorRotation, 1);

	if (!bUseActorRotation)
	{
		Ar << JumpRotation;
	}

	Ar << PathOffsetCurve;
	Ar << TimeMappingCurve;
}

UScriptStruct* FJoltLayeredMove_JumpTo::GetScriptStruct() const
{
	return FJoltLayeredMove_JumpTo::StaticStruct();
}

FString FJoltLayeredMove_JumpTo::ToSimpleString() const
{
	return FString::Printf(TEXT("JumpTo"));
}

void FJoltLayeredMove_JumpTo::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}

FVector FJoltLayeredMove_JumpTo::GetPathOffset(const float MoveFraction) const
{
	FVector PathOffset(FVector::ZeroVector);
	if (PathOffsetCurve)
	{
		// Calculate path offset
		float MinCurveTime(0.f);
		float MaxCurveTime(1.f);

		PathOffsetCurve->GetTimeRange(MinCurveTime, MaxCurveTime);
		PathOffset =  PathOffsetCurve->GetVectorValue(FMath::GetRangeValue(FVector2f(MinCurveTime, MaxCurveTime), MoveFraction));
	}
	else
	{
		// Default to "jump parabola", a simple x^2 shifted to be upside-down and shifted
		// to get [0,1] X (MoveFraction/Distance) mapping to [0,1] Y (height)
		// Height = -(2x-1)^2 + 1
		const float Phi = 2.f*MoveFraction - 1;
		const float Z = -(Phi*Phi) + 1;
		PathOffset.Z = Z;
	}

	// Scale Z offset to height. If Height < 0, we use direct path offset values
	if (JumpHeight >= 0.f)
	{
		PathOffset.Z *= JumpHeight;
	}

	return PathOffset;
}

float FJoltLayeredMove_JumpTo::EvaluateFloatCurveAtFraction(const UCurveFloat& Curve, const float Fraction) const
{
	float MinCurveTime(0.f);
	float MaxCurveTime(1.f);

	Curve.GetTimeRange(MinCurveTime, MaxCurveTime);
	return Curve.GetFloatValue(FMath::GetRangeValue(FVector2f(MinCurveTime, MaxCurveTime), Fraction));
}

FVector FJoltLayeredMove_JumpTo::GetRelativeLocation(float MoveFraction, const FRotator& Rotator) const
{
	// Given MoveFraction, what relative location should a character be at?
	FRotator FacingRotation(Rotator);
	FacingRotation.Pitch = 0.f; // By default we don't include pitch, but an option could be added if necessary

	const FVector RelativeLocationFacingSpace = FVector(MoveFraction * JumpDistance, 0.f, 0.f) + GetPathOffset(MoveFraction);

	return FacingRotation.RotateVector(RelativeLocationFacingSpace);
}

// -------------------------------------------------------------------
// FJoltLayeredMove_MoveTo
// -------------------------------------------------------------------

FJoltLayeredMove_MoveTo::FJoltLayeredMove_MoveTo()
	: StartLocation(ForceInitToZero)
	, TargetLocation(ForceInitToZero)
	, bRestrictSpeedToExpected(false)
	, PathOffsetCurve(nullptr)
	, TimeMappingCurve(nullptr)
{
	DurationMs = 1000.f;
	MixMode = EJoltMoveMixMode::OverrideVelocity;
}

FVector FJoltLayeredMove_MoveTo::GetPathOffsetInWorldSpace(const float MoveFraction) const
{
	if (PathOffsetCurve)
	{
		float MinCurveTime(0.f);
		float MaxCurveTime(1.f);

		PathOffsetCurve->GetTimeRange(MinCurveTime, MaxCurveTime);
		
		// Calculate path offset
		const FVector PathOffsetInFacingSpace = PathOffsetCurve->GetVectorValue(FMath::GetRangeValue(FVector2f(MinCurveTime, MaxCurveTime), MoveFraction));;
		FRotator FacingRotation((TargetLocation-StartLocation).Rotation());
		FacingRotation.Pitch = 0.f; // By default we don't include pitch in the offset, but an option could be added if necessary
		return FacingRotation.RotateVector(PathOffsetInFacingSpace);
	}

	return FVector::ZeroVector;
}

float FJoltLayeredMove_MoveTo::EvaluateFloatCurveAtFraction(const UCurveFloat& Curve, const float Fraction) const
{
	float MinCurveTime(0.f);
	float MaxCurveTime(1.f);

	Curve.GetTimeRange(MinCurveTime, MaxCurveTime);
	return Curve.GetFloatValue(FMath::GetRangeValue(FVector2f(MinCurveTime, MaxCurveTime), Fraction));
}

bool FJoltLayeredMove_MoveTo::GenerateMove(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, const UJoltMoverComponent* MoverComp, UJoltMoverBlackboard* SimBlackboard, FJoltProposedMove& OutProposedMove)
{
	OutProposedMove.MixMode = MixMode;

	const float DeltaSeconds = TimeStep.StepMs / 1000.f;
	
	float MoveFraction = (TimeStep.BaseSimTimeMs - StartSimTimeMs) / DurationMs;

	if (TimeMappingCurve)
	{
		MoveFraction = EvaluateFloatCurveAtFraction(*TimeMappingCurve, MoveFraction);
	}
	
	const AActor* MoverActor = MoverComp->GetOwner();
	
	FVector CurrentTargetLocation = FMath::Lerp<FVector, float>(StartLocation, TargetLocation, MoveFraction);
	FVector PathOffset = GetPathOffsetInWorldSpace(MoveFraction);
	CurrentTargetLocation += PathOffset;

	const FVector CurrentLocation = MoverActor->GetActorLocation();

	FVector Velocity = (CurrentTargetLocation - CurrentLocation) / DeltaSeconds;

	if (bRestrictSpeedToExpected && !Velocity.IsNearlyZero(UE_KINDA_SMALL_NUMBER))
	{
		// Calculate expected current location (if we didn't have collision and moved exactly where our velocity should have taken us)
		const float PreviousMoveFraction = (TimeStep.BaseSimTimeMs - StartSimTimeMs - TimeStep.StepMs) / DurationMs;
		FVector CurrentExpectedLocation = FMath::Lerp<FVector, float>(StartLocation, TargetLocation, PreviousMoveFraction);
		CurrentExpectedLocation += GetPathOffsetInWorldSpace(PreviousMoveFraction);

		// Restrict speed to the expected speed, allowing some small amount of error
		const FVector ExpectedForce = (CurrentTargetLocation - CurrentExpectedLocation) / DeltaSeconds;
		const float ExpectedSpeed = ExpectedForce.Size();
		const float CurrentSpeedSqr = Velocity.SizeSquared();

		const float ErrorAllowance = 0.5f; // in cm/s
		if (CurrentSpeedSqr > FMath::Square(ExpectedSpeed + ErrorAllowance))
		{
			Velocity.Normalize();
			Velocity *= ExpectedSpeed;
		}
	}
	
	OutProposedMove.LinearVelocity = Velocity;
	
	return true;
}

FJoltLayeredMoveBase* FJoltLayeredMove_MoveTo::Clone() const
{
	FJoltLayeredMove_MoveTo* CopyPtr = new FJoltLayeredMove_MoveTo(*this);
	return CopyPtr;
}

void FJoltLayeredMove_MoveTo::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	Ar << StartLocation;
	Ar << TargetLocation;
	Ar << bRestrictSpeedToExpected;
	Ar << PathOffsetCurve;
	Ar << TimeMappingCurve;
}

UScriptStruct* FJoltLayeredMove_MoveTo::GetScriptStruct() const
{
	return FJoltLayeredMove_MoveTo::StaticStruct();
}

FString FJoltLayeredMove_MoveTo::ToSimpleString() const
{
	return FString::Printf(TEXT("Move To"));
}

void FJoltLayeredMove_MoveTo::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}

// -------------------------------------------------------------------
// FJoltLayeredMove_MoveToDynamic
// -------------------------------------------------------------------

FJoltLayeredMove_MoveToDynamic::FJoltLayeredMove_MoveToDynamic()
	: LocationActor(nullptr)
{
	StartLocation = FVector::ZeroVector;
	TargetLocation = FVector::ZeroVector;
	bRestrictSpeedToExpected = false;
	PathOffsetCurve = nullptr;
	TimeMappingCurve = nullptr;
	DurationMs = 1000.f;
	MixMode = EJoltMoveMixMode::OverrideVelocity;
}

bool FJoltLayeredMove_MoveToDynamic::GenerateMove(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, const UJoltMoverComponent* MoverComp, UJoltMoverBlackboard* SimBlackboard, FJoltProposedMove& OutProposedMove)
{
	if (LocationActor)
	{
		TargetLocation = LocationActor->GetActorLocation();
	}
	
	return Super::GenerateMove(StartState, TimeStep, MoverComp, SimBlackboard, OutProposedMove);
}

FJoltLayeredMoveBase* FJoltLayeredMove_MoveToDynamic::Clone() const
{
	FJoltLayeredMove_MoveToDynamic* CopyPtr = new FJoltLayeredMove_MoveToDynamic(*this);
	return CopyPtr;
}

void FJoltLayeredMove_MoveToDynamic::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	Ar << LocationActor;
}

UScriptStruct* FJoltLayeredMove_MoveToDynamic::GetScriptStruct() const
{
	return FJoltLayeredMove_MoveToDynamic::StaticStruct();
}

FString FJoltLayeredMove_MoveToDynamic::ToSimpleString() const
{
	return FString::Printf(TEXT("Move To Dynamic"));
}

void FJoltLayeredMove_MoveToDynamic::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}

// -------------------------------------------------------------------
// FJoltLayeredMove_RadialImpulse
// -------------------------------------------------------------------

FJoltLayeredMove_RadialImpulse::FJoltLayeredMove_RadialImpulse()
	: Location(ForceInitToZero)
	, LocationActor(nullptr)
	, Radius(1.f)
	, Magnitude(0.f)
	, bIsPush(true)
	, bNoVerticalVelocity(false)
	, DistanceFalloff(nullptr)
	, MagnitudeOverTime(nullptr)
	, bUseFixedWorldDirection(false)
	, FixedWorldDirection(ForceInitToZero)
{
	DurationMs = 0.f;
	MixMode = EJoltMoveMixMode::AdditiveVelocity;
}

bool FJoltLayeredMove_RadialImpulse::GenerateMove(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, const UJoltMoverComponent* MoverComp, UJoltMoverBlackboard* SimBlackboard, FJoltProposedMove& OutProposedMove)
{
	const FJoltUpdatedMotionState* SyncState = StartState.SyncState.Collection.FindDataByType<FJoltUpdatedMotionState>();
	check(SyncState);
	
	const FVector CharacterLocation = SyncState->GetLocation_WorldSpace();
	FVector Velocity = FVector::ZeroVector;
	const FVector VelocityLocation = LocationActor ? LocationActor->GetActorLocation() : Location;
	float Distance = FVector::Dist(VelocityLocation, CharacterLocation);
	
	if (Distance < Radius)
	{
		// Calculate magnitude
		float CurrentMagnitude = Magnitude;
		{
			float AdditiveMagnitudeFactor = 1.f;
			if (DistanceFalloff)
			{
				const float DistanceFactor = DistanceFalloff->GetFloatValue(FMath::Clamp(Distance / Radius, 0.f, 1.f));
				AdditiveMagnitudeFactor -= (1.f - DistanceFactor);
			}

			if (MagnitudeOverTime && DurationMs > 0)
			{
				const double TimeValue = FMath::Clamp((TimeStep.BaseSimTimeMs - StartSimTimeMs) / DurationMs, 0.0, 1.0);
				const float TimeFactor = MagnitudeOverTime->GetFloatValue(TimeValue);
				AdditiveMagnitudeFactor -= (1.f - TimeFactor);
			}

			CurrentMagnitude = Magnitude * FMath::Clamp(AdditiveMagnitudeFactor, 0.f, 1.f);
		}

		if (bUseFixedWorldDirection)
		{
			Velocity = FixedWorldDirection.Vector() * CurrentMagnitude;
		}
		else
		{
			Velocity = (VelocityLocation - CharacterLocation).GetSafeNormal() * CurrentMagnitude;
			
			if (bIsPush)
			{
				Velocity *= -1.f;
			}
		}
		
		if (bNoVerticalVelocity)
        {
       		const FPlane MovementPlane(FVector::ZeroVector, MoverComp->GetUpDirection());
       		Velocity = UJoltMovementUtils::ConstrainToPlane(Velocity, MovementPlane);
       	}
	}
	else 
	{
	     return false;
	}

	OutProposedMove.LinearVelocity = Velocity;

	return true;
}

FJoltLayeredMoveBase* FJoltLayeredMove_RadialImpulse::Clone() const
{
	FJoltLayeredMove_RadialImpulse* CopyPtr = new FJoltLayeredMove_RadialImpulse(*this);
    return CopyPtr;
}

void FJoltLayeredMove_RadialImpulse::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);
	
	Ar << Location;
	Ar << LocationActor;
	Ar << Radius;
	Ar << Magnitude;
	Ar << bIsPush;
	Ar << bNoVerticalVelocity;
	Ar << DistanceFalloff;
	Ar << MagnitudeOverTime;
	Ar << bUseFixedWorldDirection;
	if (bUseFixedWorldDirection)
	{
		Ar << FixedWorldDirection;
	}
}

UScriptStruct* FJoltLayeredMove_RadialImpulse::GetScriptStruct() const
{
	return FJoltLayeredMove_RadialImpulse::StaticStruct();
}

FString FJoltLayeredMove_RadialImpulse::ToSimpleString() const
{
	return FString::Printf(TEXT("Radial Impulse"));
}

void FJoltLayeredMove_RadialImpulse::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}
