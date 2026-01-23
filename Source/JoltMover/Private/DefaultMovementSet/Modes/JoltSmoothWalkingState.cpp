// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltSmoothWalkingState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltSmoothWalkingState)

namespace SmoothWalkingStateErrorTolerance
{
	constexpr float VelocityErrorTolerance = 10.f;
	constexpr float AngularVelocityErrorTolerance = 10.f;
	constexpr float AccelerationErrorTolerance = 50.f;
	constexpr float FacingDegreeErrorTolerance = 10.0f;
}

UScriptStruct* FJoltSmoothWalkingState::GetScriptStruct() const
{ 
	return StaticStruct(); 
}

FJoltMoverDataStructBase* FJoltSmoothWalkingState::Clone() const
{ 
	return new FJoltSmoothWalkingState(*this); 
}

bool FJoltSmoothWalkingState::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bool bSuccess = Super::NetSerialize(Ar, Map, bOutSuccess);

	// Could be quantized to save bandwidth
	Ar << SpringVelocity;
	Ar << SpringAcceleration;
	Ar << IntermediateVelocity;
	Ar << IntermediateFacing;
	Ar << IntermediateAngularVelocity;

	return bSuccess;
}

void FJoltSmoothWalkingState::ToString(FAnsiStringBuilderBase& Out) const
{
	Super::ToString(Out);

	Out.Appendf("SpringVelocity=%s SpringAcceleration=%s IntVel=%s IntFac=%s IntAng=%s\n",
		*SpringVelocity.ToCompactString(),
		*SpringAcceleration.ToCompactString(),
		*IntermediateVelocity.ToCompactString(),
		*IntermediateFacing.ToString(), 
		*IntermediateAngularVelocity.ToString());
}

bool FJoltSmoothWalkingState::ShouldReconcile(const FJoltMoverDataStructBase& AuthorityState) const
{
	const FJoltSmoothWalkingState* AuthoritySpringState = static_cast<const FJoltSmoothWalkingState*>(&AuthorityState);

	return (!(SpringVelocity - AuthoritySpringState->SpringVelocity).IsNearlyZero(SmoothWalkingStateErrorTolerance::VelocityErrorTolerance) ||
			!(SpringAcceleration - AuthoritySpringState->SpringAcceleration).IsNearlyZero(SmoothWalkingStateErrorTolerance::AccelerationErrorTolerance) ||
			!(IntermediateVelocity - AuthoritySpringState->IntermediateVelocity).IsNearlyZero(SmoothWalkingStateErrorTolerance::VelocityErrorTolerance) ||
			 (IntermediateFacing.AngularDistance(AuthoritySpringState->IntermediateFacing) > SmoothWalkingStateErrorTolerance::FacingDegreeErrorTolerance ||
			!(IntermediateAngularVelocity - AuthoritySpringState->IntermediateAngularVelocity).IsNearlyZero(SmoothWalkingStateErrorTolerance::AngularVelocityErrorTolerance)));
}


void FJoltSmoothWalkingState::Interpolate(const FJoltMoverDataStructBase& From, const FJoltMoverDataStructBase& To, float Pct)
{
	const FJoltSmoothWalkingState* FromState = static_cast<const FJoltSmoothWalkingState*>(&From);
	const FJoltSmoothWalkingState* ToState   = static_cast<const FJoltSmoothWalkingState*>(&To);

	SpringVelocity = FMath::Lerp(FromState->SpringVelocity, ToState->SpringVelocity, Pct);
	SpringAcceleration = FMath::Lerp(FromState->SpringAcceleration, ToState->SpringAcceleration, Pct);
	IntermediateVelocity = FMath::Lerp(FromState->IntermediateVelocity, ToState->IntermediateVelocity, Pct);
	IntermediateFacing = FQuat::Slerp(FromState->IntermediateFacing, ToState->IntermediateFacing, Pct);
	IntermediateAngularVelocity = FMath::Lerp(FromState->IntermediateAngularVelocity, ToState->IntermediateAngularVelocity, Pct);
}
