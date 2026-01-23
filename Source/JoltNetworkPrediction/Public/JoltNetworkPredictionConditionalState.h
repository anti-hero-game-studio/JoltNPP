// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "JoltNetworkPredictionCheck.h"

template<typename TestType, typename UnderlyingType=TestType>
struct TJoltConditionalState
{
	enum { Valid = true };

	operator UnderlyingType*() { return &State; }

	const UnderlyingType* operator->() const { return &State; }
	UnderlyingType* operator->() { return &State; }

	UnderlyingType* Get() { return &State; }
	const UnderlyingType* Get() const { return &State; }

	void CopyTo(UnderlyingType* Dest) const
	{
		jnpCheckSlow(Dest);
		*Dest = State;
	}

private:
	UnderlyingType State;
};

template<typename UnderlyingType>
struct TJoltConditionalState<void, UnderlyingType>
{
	enum { Valid = false };

	operator void*() const { return nullptr; }

	const void* operator->() const { return nullptr; }
	void* operator->() { return nullptr; }

	void* Get() { return nullptr; }
	const void* Get() const { return nullptr; }

	void CopyTo(void* Dest) const { }
};