// Copyright Epic Games, Inc. All Rights Reserved.


#include "MoveLibrary/JoltMoverBlackboard.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMoverBlackboard)



void UJoltMoverBlackboard::Invalidate(FName ObjName)
{
	UE::TWriteScopeLock Lock(ObjectsMapLock);
	ObjectsByName.Remove(ObjName);
}

void UJoltMoverBlackboard::Invalidate(EJoltInvalidationReason Reason)
{
	switch (Reason)
	{
		default:
		case EJoltInvalidationReason::FullReset:
		{
			UE::TWriteScopeLock Lock(ObjectsMapLock);
			ObjectsByName.Empty();
		}
		break;

		// TODO: Support other reasons
	}
}

void UJoltMoverBlackboard::BeginDestroy()
{
	InvalidateAll();
	Super::BeginDestroy();
}
