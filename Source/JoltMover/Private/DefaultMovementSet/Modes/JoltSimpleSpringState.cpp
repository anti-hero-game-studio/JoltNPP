// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltSimpleSpringState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltSimpleSpringState)

UScriptStruct* FJoltSimpleSpringState::GetScriptStruct() const
{ 
	return StaticStruct(); 
}

FJoltMoverDataStructBase* FJoltSimpleSpringState::Clone() const
{ 
	return new FJoltSimpleSpringState(*this); 
}

bool FJoltSimpleSpringState::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bool bSuccess = Super::NetSerialize(Ar, Map, bOutSuccess);

	// Could be quantized to save bandwidth
	Ar << CurrentAccel;

	return bSuccess;
}

void FJoltSimpleSpringState::ToString(FAnsiStringBuilderBase& Out) const
{
	Super::ToString(Out);

	Out.Appendf("Accel=%s\n", *CurrentAccel.ToCompactString());
}

bool FJoltSimpleSpringState::ShouldReconcile(const FJoltMoverDataStructBase& AuthorityState) const
{
	const FJoltSimpleSpringState* AuthoritySpringState = static_cast<const FJoltSimpleSpringState*>(&AuthorityState);

	return !(CurrentAccel - AuthoritySpringState->CurrentAccel).IsNearlyZero();
		
}


void FJoltSimpleSpringState::Interpolate(const FJoltMoverDataStructBase& From, const FJoltMoverDataStructBase& To, float Pct)
{
	const FJoltSimpleSpringState* FromState = static_cast<const FJoltSimpleSpringState*>(&From);
	const FJoltSimpleSpringState* ToState   = static_cast<const FJoltSimpleSpringState*>(&To);

	CurrentAccel           = FMath::Lerp(FromState->CurrentAccel, ToState->CurrentAccel, Pct);
}
