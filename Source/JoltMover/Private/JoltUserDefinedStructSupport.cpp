// Copyright Epic Games, Inc. All Rights Reserved.


#include "JoltUserDefinedStructSupport.h"
#include "StructUtils/UserDefinedStruct.h"
#include "JoltMoverLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltUserDefinedStructSupport)

#define LOCTEXT_NAMESPACE "JoltMoverUDSInstances"


// TODO: Consider different rules for interpolation/merging/reconciliation checks. 
// This could be accomplished via cvars / Mover settings / per-type metadata , etc.

bool FJoltMoverUserDefinedDataStruct::ShouldReconcile(const FJoltMoverDataStructBase& AuthorityState) const
{
	const FJoltMoverUserDefinedDataStruct& TypedAuthority = static_cast<const FJoltMoverUserDefinedDataStruct&>(AuthorityState);

	check(TypedAuthority.StructInstance.GetScriptStruct() == this->StructInstance.GetScriptStruct());

	return !StructInstance.Identical(&TypedAuthority.StructInstance, EPropertyPortFlags::PPF_DeepComparison);
}

void FJoltMoverUserDefinedDataStruct::Interpolate(const FJoltMoverDataStructBase& From, const FJoltMoverDataStructBase& To, float LerpFactor)
{
	const FJoltMoverUserDefinedDataStruct& PrimarySource = static_cast<const FJoltMoverUserDefinedDataStruct&>((LerpFactor < 0.5f) ? From : To);

	// copy all properties from the heaviest-weighted source rather than interpolate
	StructInstance = PrimarySource.StructInstance;
}

void FJoltMoverUserDefinedDataStruct::Merge(const FJoltMoverDataStructBase& From)
{
	const FJoltMoverUserDefinedDataStruct& TypedFrom = static_cast<const FJoltMoverUserDefinedDataStruct&>(From);

	check(TypedFrom.StructInstance.GetScriptStruct() == this->StructInstance.GetScriptStruct());

	// Merging is typically only done for inputs. Let's make the assumption that boolean inputs should be OR'd so we never miss any digital inputs.

	if (const UScriptStruct* UdsScriptStruct = TypedFrom.StructInstance.GetScriptStruct())
	{
		uint8* ThisInstanceMemory = StructInstance.GetMutableMemory();
		const uint8* FromInstanceMemory = TypedFrom.StructInstance.GetMemory();

		for (TFieldIterator<FBoolProperty> BoolProperty(UdsScriptStruct); BoolProperty; ++BoolProperty)
		{
			bool bMergedBool = BoolProperty->GetPropertyValue(ThisInstanceMemory);

			if (!bMergedBool)
			{
				bMergedBool |= BoolProperty->GetPropertyValue(FromInstanceMemory);

				if (bMergedBool)
				{
					BoolProperty->SetPropertyValue(ThisInstanceMemory, bMergedBool);
				}
			}
		}
	}
}

FJoltMoverDataStructBase* FJoltMoverUserDefinedDataStruct::Clone() const
{
	FJoltMoverUserDefinedDataStruct* CopyPtr = new FJoltMoverUserDefinedDataStruct(*this);
	return CopyPtr;
}

bool FJoltMoverUserDefinedDataStruct::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bool bSuperSuccess, bStructSuccess;

	Super::NetSerialize(Ar, Map, bSuperSuccess);
	StructInstance.NetSerialize(Ar, Map, bStructSuccess);

	bOutSuccess = bSuperSuccess && bStructSuccess;

	return true;
}


void FJoltMoverUserDefinedDataStruct::ToString(FAnsiStringBuilderBase& Out) const
{
	Super::ToString(Out);

	// TODO: add property-wise concatenated string output
}

const UScriptStruct* FJoltMoverUserDefinedDataStruct::GetDataScriptStruct() const
{
	return StructInstance.GetScriptStruct();
}


#undef LOCTEXT_NAMESPACE
