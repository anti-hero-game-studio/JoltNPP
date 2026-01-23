// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltInstantMovementEffect.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltInstantMovementEffect)

FJoltInstantMovementEffect* FJoltInstantMovementEffect::Clone() const
{
	// If child classes don't override this, saved moves will not work
	checkf(false, TEXT("FJoltInstantMovementEffect::Clone() being called erroneously from %s. A FJoltInstantMovementEffect should never be queued directly and Clone should always be overridden in child structs!"), *GetNameSafe(GetScriptStruct()));
	return nullptr;
}

void FJoltInstantMovementEffect::NetSerialize(FArchive& Ar)
{
	
}

UScriptStruct* FJoltInstantMovementEffect::GetScriptStruct() const
{
	return FJoltInstantMovementEffect::StaticStruct();
}

FString FJoltInstantMovementEffect::ToSimpleString() const
{
	return GetScriptStruct()->GetName();
}
