// Copyright Epic Games, Inc. All Rights Reserved.


#include "JoltMoverTypes.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "JoltMoverLog.h"
#include "JoltMoverModule.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/ObjectKey.h"
#include "JoltUserDefinedStructSupport.h"
#include "HAL/IConsoleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMoverTypes)

#define LOCTEXT_NAMESPACE "JoltMoverData"

UE_DEFINE_GAMEPLAY_TAG_COMMENT(JoltMover_IsOnGround, "JoltMover.IsOnGround", "Default Mover state flag indicating character is on the ground.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(JoltMover_IsInAir, "JoltMover.IsInAir", "Default Mover state flag indicating character is in the air.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(JoltMover_IsFalling, "JoltMover.IsFalling", "Default Mover state flag indicating character is falling.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(JoltMover_IsFlying, "JoltMover.IsFlying", "Default Mover state flag indicating character is flying.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(JoltMover_IsSwimming, "JoltMover.IsSwimming", "Default Mover state flag indicating character is swimming.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(JoltMover_IsCrouching, "JoltMover.Stance.IsCrouching", "Default Mover state flag indicating character is crouching.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(JoltMover_IsNavWalking, "JoltMover.IsNavWalking", "Default Mover state flag indicating character is NavWalking.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(JoltMover_SkipAnimRootMotion, "JoltMover.SkipAnimRootMotion", "Default Mover state flag indicating Animation Root Motion proposed movement should be skipped.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(JoltMover_SkipVerticalAnimRootMotion, "JoltMover.SkipVerticalAnimRootMotion", "Default Mover state flag indicating Animation Root Motion proposed movements should not include a vertical velocity component (along the up/down axis).");

FJoltMoverOnImpactParams::FJoltMoverOnImpactParams() 
	: AttemptedMoveDelta(0) 
{
}

FJoltMoverOnImpactParams::FJoltMoverOnImpactParams(const FName& ModeName, const FHitResult& Hit, const FVector& Delta)
	: MovementModeName(ModeName)
	, HitResult(Hit)
	, AttemptedMoveDelta(Delta)
{
}

FJoltMoverDataStructBase::FJoltMoverDataStructBase()
{
}

FJoltMoverDataStructBase* FJoltMoverDataStructBase::Clone() const
{
	// If child classes don't override this, collections will not work
	checkf(false, TEXT("%hs is being called erroneously on [%s]. This must be overridden in derived types!"), __FUNCTION__, *GetScriptStruct()->GetName());
	return nullptr;
}

UScriptStruct* FJoltMoverDataStructBase::GetScriptStruct() const
{
	checkf(false, TEXT("%hs is being called erroneously. This must be overridden in derived types!"), __FUNCTION__);
	return FJoltMoverDataStructBase::StaticStruct();
}

bool FJoltMoverDataStructBase::ShouldReconcile(const FJoltMoverDataStructBase& AuthorityState) const
{
	checkf(false, TEXT("%hs is being called erroneously on [%s]. This must be overridden in derived types that comprise STATE data (sync/aux) "
					"or INPUT data for use with physics-based movement"), __FUNCTION__, *GetScriptStruct()->GetName());
	return false;
}

void FJoltMoverDataStructBase::Interpolate(const FJoltMoverDataStructBase& From, const FJoltMoverDataStructBase& To, float Pct)
{
	checkf(false, TEXT("%hs is being called erroneously on [%s]. This must be overridden in derived types that comprise STATE data (sync/aux) "
					"or INPUT data for use with physics-based movement"), __FUNCTION__, *GetScriptStruct()->GetName());
}

void FJoltMoverDataStructBase::Merge(const FJoltMoverDataStructBase& From)
{
	checkf(false, TEXT("%hs is being called erroneously on [%s]. This must be overridden in derived types that comprise INPUT data for use with physics-based movement"),
		__FUNCTION__, *GetScriptStruct()->GetName());
}

const UScriptStruct* FJoltMoverDataStructBase::GetDataScriptStruct() const
{ 
	return GetScriptStruct(); 
}


FJoltMoverDataCollection::FJoltMoverDataCollection()
{
}

bool FJoltMoverDataCollection::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	NetSerializeDataArray(Ar, Map, DataArray);

	if (Ar.IsError())
	{
		bOutSuccess = false;
		return false;
	}

	bOutSuccess = true;
	return true;
}

struct FJoltMoverDataDeleter
{
	FORCEINLINE void operator()(FJoltMoverDataStructBase* Object) const
	{
		check(Object);
		UScriptStruct* ScriptStruct = Object->GetScriptStruct();
		check(ScriptStruct);
		ScriptStruct->DestroyStruct(Object);
		FMemory::Free(Object);
	}
};

bool FJoltMoverDataCollection::SerializeDebugData(FArchive& Ar)
{
	// DISCLAIMER: This serialization is not version independent, so it might not be good enough to be used for the Chaos Visual Debugger in the long run

	// First serialize the number of structs in the collection
	int32 NumDataStructs;
	if (Ar.IsLoading())
	{
		Ar << NumDataStructs;
		DataArray.SetNumZeroed(NumDataStructs);
	}
	else
	{
		NumDataStructs = DataArray.Num();
		Ar << NumDataStructs;
	}

	if (Ar.IsLoading())
	{
		DataArray.Empty();
		for (int32 i = 0; i < NumDataStructs && !Ar.IsError(); ++i)
		{
			FString StructName;
			Ar << StructName;
			if (UScriptStruct* MoveDataStruct = Cast<UScriptStruct>(FindObject<UStruct>(nullptr, *StructName)))
			{
				FJoltMoverDataStructBase* NewMoverData = AddDataByType(MoveDataStruct);
				MoveDataStruct->SerializeBin(Ar, NewMoverData);
			}
		}
	}
	else
	{
		for (int32 i = 0; i < DataArray.Num() && !Ar.IsError(); ++i)
		{
			FJoltMoverDataStructBase* MoveDataStruct = DataArray[i].Get();
			if (MoveDataStruct)
			{
				// The FullName of the script struct will be something like "ScriptStruct /Script/JoltMover.FJoltCharacterDefaultInputs"
				FString FullStructName = MoveDataStruct->GetScriptStruct()->GetFullName(nullptr);
				// We don't need to save the first part since we only ever save UScriptStructs (C++ structs)
				FString StructName = FullStructName.RightChop(13); // So we chop the "ScriptStruct " part (hence 13 characters)
				Ar << StructName;
				MoveDataStruct->GetScriptStruct()->SerializeBin(Ar, MoveDataStruct);
			}
		}
	}

	return true;
}

FJoltMoverDataCollection& FJoltMoverDataCollection::operator=(const FJoltMoverDataCollection& Other)
{
	// Perform deep copy of this Group
	if (this != &Other)
	{
		bool bCanCopyInPlace = (UE::JoltMover::DisableDataCopyInPlace == 0 && DataArray.Num() == Other.DataArray.Num());
		if (bCanCopyInPlace)
		{
			// If copy in place is enabled and the arrays are the same size, copy by index
			for (int32 i = 0; i < DataArray.Num(); ++i)
			{
				if (FJoltMoverDataStructBase* SrcData = Other.DataArray[i].Get())
				{
					FJoltMoverDataStructBase* DestData = DataArray[i].Get();
					UScriptStruct* SourceStruct = SrcData->GetScriptStruct();

					if (DestData && SourceStruct == DestData->GetScriptStruct())
					{
						// Same type so copy in place
						SourceStruct->CopyScriptStruct(DestData, SrcData, 1);
					}
					else
					{
						// Different type so replace the shared ptr with a clone
						DataArray[i] = TSharedPtr<FJoltMoverDataStructBase>(SrcData->Clone());
					}
				}
				else
				{
					// Found invalid source, fall back to full copy
					bCanCopyInPlace = false;
					break;
				}
			}
		}
		
		if (!bCanCopyInPlace)
		{
			// Deep copy active data blocks
			DataArray.Empty(Other.DataArray.Num());
			for (int i = 0; i < Other.DataArray.Num(); ++i)
			{
				if (Other.DataArray[i].IsValid())
				{
					FJoltMoverDataStructBase* CopyOfSourcePtr = Other.DataArray[i]->Clone();
					DataArray.Add(TSharedPtr<FJoltMoverDataStructBase>(CopyOfSourcePtr));
				}
				else
				{
					UE_LOG(LogJoltMover, Warning, TEXT("FJoltMoverDataCollection::operator= trying to copy invalid Other DataArray element"));
				}
			}
		}
	}

	return *this;
}

bool FJoltMoverDataCollection::operator==(const FJoltMoverDataCollection& Other) const
{
	// Deep move-by-move comparison
	if (DataArray.Num() != Other.DataArray.Num())
	{
		return false;
	}

	for (int32 i = 0; i < DataArray.Num(); ++i)
	{
		if (DataArray[i].IsValid() == Other.DataArray[i].IsValid())
		{
			if (DataArray[i].IsValid())
			{
				// TODO: Implement deep equality checks
				// 				if (!DataArray[i]->MatchesAndHasSameState(Other.DataArray[i].Get()))
				// 				{
				// 					return false; // They're valid and don't match/have same state
				// 				}
			}
		}
		else
		{
			return false; // Mismatch in validity
		}
	}

	return true;
}

bool FJoltMoverDataCollection::operator!=(const FJoltMoverDataCollection& Other) const
{
	return !(FJoltMoverDataCollection::operator==(Other));
}


bool FJoltMoverDataCollection::ShouldReconcile(const FJoltMoverDataCollection& Other) const
{
	// Collections must have matching elements, and those elements are piece-wise tested for needing reconciliation
	if (DataArray.Num() != Other.DataArray.Num())
	{
		return true;
	}

	for (int32 i = 0; i < DataArray.Num(); ++i)
	{
		const FJoltMoverDataStructBase* DataElement = DataArray[i].Get();
		const FJoltMoverDataStructBase* OtherDataElement = Other.FindDataByType(DataElement->GetDataScriptStruct());

		// Reconciliation is needed if there's no matching types, or if the element pair needs reconciliation
		if (OtherDataElement == nullptr ||
			DataElement->ShouldReconcile(*OtherDataElement))
		{
			return true;
		}
	}

	return false;
}

void FJoltMoverDataCollection::Interpolate(const FJoltMoverDataCollection& From, const FJoltMoverDataCollection& To, float Pct)
{
	// TODO: Consider an inline allocator to avoid dynamic memory allocations
	TSet<TObjectKey<UScriptStruct>> AddedDataTypes;

	// Piece-wise interpolation of matching data blocks
	for (const TSharedPtr<FJoltMoverDataStructBase>& FromElement : From.DataArray)
	{
		AddedDataTypes.Add(FromElement->GetDataScriptStruct());

		if (const FJoltMoverDataStructBase* ToElement = To.FindDataByType(FromElement->GetDataScriptStruct()))
		{
			FJoltMoverDataStructBase* InterpElement = FindOrAddDataByType(FromElement->GetDataScriptStruct());
			InterpElement->Interpolate(*FromElement, *ToElement, Pct);
		}
		else
		{
			// If only present in From, add the block directly to this collection
			AddDataByCopy(FromElement.Get());
		}
	}

	// Add any types present only in To as well
	for (const TSharedPtr<FJoltMoverDataStructBase>& ToElement : To.DataArray)
	{
		if (!AddedDataTypes.Contains(ToElement->GetDataScriptStruct()))
		{
			AddDataByCopy(ToElement.Get());
		}
	}
}

void FJoltMoverDataCollection::Merge(const FJoltMoverDataCollection& From)
{
	for (const TSharedPtr<FJoltMoverDataStructBase>& FromElement : From.DataArray)
	{
		if (FJoltMoverDataStructBase* ExistingElement = FindDataByType(FromElement->GetDataScriptStruct()))
		{
			ExistingElement->Merge(*FromElement);
		}
		else
		{
			// If only present in the previous block, copy it into this block
			AddDataByCopy(FromElement.Get());
		}
	}
}

void FJoltMoverDataCollection::Decay(float DecayAmount)
{
	for (const TSharedPtr<FJoltMoverDataStructBase>& Element : DataArray)
	{
		Element->Decay(DecayAmount);
	}
}


bool FJoltMoverDataCollection::HasSameContents(const FJoltMoverDataCollection& Other) const
{
	if (DataArray.Num() != Other.DataArray.Num())
	{
		return false;
	}

	for (int32 i = 0; i < DataArray.Num(); ++i)
	{
		if (DataArray[i]->GetDataScriptStruct() != Other.DataArray[i]->GetDataScriptStruct())
		{
			return false;
		}
	}

	return true;
}

void FJoltMoverDataCollection::AddStructReferencedObjects(FReferenceCollector& Collector) const
{
	for (const TSharedPtr<FJoltMoverDataStructBase>& Data : DataArray)
	{
		if (Data.IsValid())
		{
			Data->AddReferencedObjects(Collector);
		}
	}
}

void FJoltMoverDataCollection::ToString(FAnsiStringBuilderBase& Out) const
{
	for (const TSharedPtr<FJoltMoverDataStructBase>& Data : DataArray)
	{
		if (Data.IsValid())
		{
			UScriptStruct* Struct = Data->GetScriptStruct();
			Out.Appendf("\n[%s]\n", TCHAR_TO_ANSI(*Struct->GetName()));
			Data->ToString(Out);
		}
	}
}

TArray<TSharedPtr<FJoltMoverDataStructBase>>::TConstIterator FJoltMoverDataCollection::GetCollectionDataIterator() const
{
	return DataArray.CreateConstIterator();
}

//static 
TSharedPtr<FJoltMoverDataStructBase> FJoltMoverDataCollection::CreateDataByType(const UScriptStruct* DataStructType)
{
	check(DataStructType->IsChildOf(FJoltMoverDataStructBase::StaticStruct()));

	FJoltMoverDataStructBase* NewDataBlock = (FJoltMoverDataStructBase*)FMemory::Malloc(DataStructType->GetCppStructOps()->GetSize());
	DataStructType->InitializeStruct(NewDataBlock);

	return TSharedPtr<FJoltMoverDataStructBase>(NewDataBlock, FJoltMoverDataDeleter());
}


FJoltMoverDataStructBase* FJoltMoverDataCollection::AddDataByType(const UScriptStruct* DataStructType)
{
	if (ensure(!FindDataByType(DataStructType)))
	{
		TSharedPtr<FJoltMoverDataStructBase> NewDataInstance;

		if (DataStructType->IsA<UUserDefinedStruct>())
		{
			NewDataInstance = CreateDataByType(FJoltMoverUserDefinedDataStruct::StaticStruct());
			static_cast<FJoltMoverUserDefinedDataStruct*>(NewDataInstance.Get())->StructInstance.InitializeAs(DataStructType);
		}
		else
		{
			NewDataInstance = CreateDataByType(DataStructType);
		}

		DataArray.Add(NewDataInstance);
		return NewDataInstance.Get();
	}
	
	return nullptr;
}


void FJoltMoverDataCollection::AddOrOverwriteData(const TSharedPtr<FJoltMoverDataStructBase> DataInstance)
{
	RemoveDataByType(DataInstance->GetDataScriptStruct());
	DataArray.Add(DataInstance);
}


void FJoltMoverDataCollection::AddDataByCopy(const FJoltMoverDataStructBase* DataInstanceToCopy)
{
	check(DataInstanceToCopy);

	const UScriptStruct* TypeToMatch = DataInstanceToCopy->GetDataScriptStruct();

	if (FJoltMoverDataStructBase* ExistingMatchingData = FindDataByType(TypeToMatch))
	{
		// Note that we've matched based on the "data" type but we're copying the top-level type (a FJoltMoverDataStructBase subtype)
		const UScriptStruct* MoverDataTypeToCopy = DataInstanceToCopy->GetScriptStruct();
		MoverDataTypeToCopy->CopyScriptStruct(ExistingMatchingData, DataInstanceToCopy, 1);
	}
	else
	{
		DataArray.Add(TSharedPtr<FJoltMoverDataStructBase>(DataInstanceToCopy->Clone()));
	}
}


FJoltMoverDataStructBase* FJoltMoverDataCollection::FindDataByType(const UScriptStruct* DataStructType) const
{
	for (const TSharedPtr<FJoltMoverDataStructBase>& Data : DataArray)
	{
		const UStruct* CandidateStruct = Data->GetDataScriptStruct();
		while (CandidateStruct)
		{
			if (DataStructType == CandidateStruct)
			{
				return Data.Get();
			}

			CandidateStruct = CandidateStruct->GetSuperStruct();
		}
	}

	return nullptr;
}


FJoltMoverDataStructBase* FJoltMoverDataCollection::FindOrAddDataByType(const UScriptStruct* DataStructType)
{
	if (FJoltMoverDataStructBase* ExistingData = FindDataByType(DataStructType))
	{
		return ExistingData;
	}

	return AddDataByType(DataStructType);
}


bool FJoltMoverDataCollection::RemoveDataByType(const UScriptStruct* DataStructType)
{
	int32 IndexToRemove = -1;

	for (int32 i=0; i < DataArray.Num() && IndexToRemove < 0; ++i)
	{
		const UStruct* CandidateStruct = DataArray[i]->GetDataScriptStruct();
		while (CandidateStruct)
		{
			if (DataStructType == CandidateStruct)
			{
				IndexToRemove = i;
				break;
			}

			CandidateStruct = CandidateStruct->GetSuperStruct();
		}
	}

	if (IndexToRemove >= 0)
	{
		DataArray.RemoveAt(IndexToRemove);
		return true;
	}

	return false;
}

/*static*/
void FJoltMoverDataCollection::NetSerializeDataArray(FArchive& Ar, UPackageMap* Map, TArray<TSharedPtr<FJoltMoverDataStructBase>>& DataArray)
{
	uint8 NumDataStructsToSerialize;
	if (Ar.IsSaving())
	{
		NumDataStructsToSerialize = DataArray.Num();
	}

	Ar << NumDataStructsToSerialize;

	if (Ar.IsLoading())
	{
		DataArray.SetNumZeroed(NumDataStructsToSerialize);
	}

	for (int32 i = 0; i < NumDataStructsToSerialize && !Ar.IsError(); ++i)
	{
		TCheckedObjPtr<UScriptStruct> ScriptStruct = DataArray[i].IsValid() ? DataArray[i]->GetScriptStruct() : nullptr;
		UScriptStruct* ScriptStructLocal = ScriptStruct.Get();

		Ar << ScriptStruct;

		if (ScriptStruct.IsValid())
		{
			// Restrict replication to derived classes of FJoltMoverDataStructBase for security reasons:
			// If FJoltMoverDataCollection is replicated through a Server RPC, we need to prevent clients from sending us
			// arbitrary ScriptStructs due to the allocation/reliance on GetCppStructOps below which could trigger a server crash
			// for invalid structs. All provided sources are direct children of FJoltMoverDataStructBase and we never expect to have deep hierarchies
			// so this should not be too costly
			bool bIsDerivedFromBase = false;
			UStruct* CurrentSuperStruct = ScriptStruct->GetSuperStruct();
			while (CurrentSuperStruct)
			{
				if (CurrentSuperStruct == FJoltMoverDataStructBase::StaticStruct())
				{
					bIsDerivedFromBase = true;
					break;
				}
				CurrentSuperStruct = CurrentSuperStruct->GetSuperStruct();
			}

			if (bIsDerivedFromBase)
			{
				if (Ar.IsLoading())
				{
					if (DataArray[i].IsValid() && ScriptStructLocal == ScriptStruct.Get())
					{
						// What we have locally is the same type as we're being serialized into, so we don't need to
						// reallocate - just use existing structure
					}
					else
					{
						// For now, just reset/reallocate the data when loading.
						// Longer term if we want to generalize this and use it for property replication, we should support
						// only reallocating when necessary
						FJoltMoverDataStructBase* NewDataBlock = (FJoltMoverDataStructBase*)FMemory::Malloc(ScriptStruct->GetCppStructOps()->GetSize());
						ScriptStruct->InitializeStruct(NewDataBlock);

						DataArray[i] = TSharedPtr<FJoltMoverDataStructBase>(NewDataBlock, FJoltMoverDataDeleter());
					}
				}

				bool bArrayElementSuccess = false;
				DataArray[i]->NetSerialize(Ar, Map, bArrayElementSuccess);

				if (!bArrayElementSuccess)
				{
					UE_LOG(LogJoltMover, Error, TEXT("FJoltMoverDataCollection::NetSerialize: Failed to serialize ScriptStruct %s"), *ScriptStruct->GetName());
					Ar.SetError();
					break;
				}
			}
			else
			{
				UE_LOG(LogJoltMover, Error, TEXT("FJoltMoverDataCollection::NetSerialize: ScriptStruct not derived from FJoltMoverDataStructBase attempted to serialize."));
				Ar.SetError();
				break;
			}
		}
		else if (ScriptStruct.IsError())
		{
			UE_LOG(LogJoltMover, Error, TEXT("FJoltMoverDataCollection::NetSerialize: Invalid ScriptStruct serialized."));
			Ar.SetError();
			break;
		}
	}

}



void UJoltMoverDataCollectionLibrary::K2_AddDataToCollection(FJoltMoverDataCollection& Collection, const int32& SourceAsRawBytes)
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
}

// static
DEFINE_FUNCTION(UJoltMoverDataCollectionLibrary::execK2_AddDataToCollection)
{
	P_GET_STRUCT_REF(FJoltMoverDataCollection, TargetCollection);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);

	void* SourceDataAsRawPtr = Stack.MostRecentPropertyAddress;
	FStructProperty* SourceStructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	if (!SourceDataAsRawPtr || !SourceStructProp)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("JoltMoverDataCollection_AddDataToCollection", "Failed to resolve the SourceAsRawBytes for AddDataToCollection")
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}
	else
	{
		P_NATIVE_BEGIN;

		if (ensure(SourceStructProp->Struct))
		{
			// User-defined struct type support: we wrap an instance inside a FJoltMoverUserDefinedDataStruct
			if (SourceStructProp->Struct->IsA<UUserDefinedStruct>())
			{
				FJoltMoverUserDefinedDataStruct UserDefinedDataWrapper;
				UserDefinedDataWrapper.StructInstance.InitializeAs(SourceStructProp->Struct, (uint8*)SourceDataAsRawPtr);

				TargetCollection.AddDataByCopy(&UserDefinedDataWrapper);
			}
			else if (SourceStructProp->Struct->IsChildOf(FJoltMoverDataStructBase::StaticStruct()))
			{
				FJoltMoverDataStructBase* SourceDataAsBasePtr = reinterpret_cast<FJoltMoverDataStructBase*>(SourceDataAsRawPtr);
				TargetCollection.AddDataByCopy(SourceDataAsBasePtr);
			}
			else
			{
				UE_LOG(LogJoltMover, Warning, TEXT("AddDataToCollection: invalid struct type submitted: %s"), *SourceStructProp->Struct->GetName());
			}
		}

		P_NATIVE_END;
	}
}


void UJoltMoverDataCollectionLibrary::K2_GetDataFromCollection(bool& DidSucceed, const FJoltMoverDataCollection& Collection, int32& TargetAsRawBytes)
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
}

// static
DEFINE_FUNCTION(UJoltMoverDataCollectionLibrary::execK2_GetDataFromCollection)
{
	P_GET_UBOOL_REF(DidSucceed);
	P_GET_STRUCT_REF(FJoltMoverDataCollection, TargetCollection);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);

	void* TargetDataAsRawPtr = Stack.MostRecentPropertyAddress;
	FStructProperty* TargetStructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	DidSucceed = false;

	if (!TargetDataAsRawPtr || !TargetStructProp)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("JoltMoverDataCollection_GetDataFromCollection_UnresolvedTarget", "Failed to resolve the TargetAsRawBytes for GetDataFromCollection")
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}
	else if (!TargetStructProp->Struct || 
				(!TargetStructProp->Struct->IsChildOf(FJoltMoverDataStructBase::StaticStruct()) && !TargetStructProp->Struct->IsA<UUserDefinedStruct>()))
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("JoltMoverDataCollection_GetDataFromCollection_BadType", "TargetAsRawBytes is not a valid type. Must be a child of FJoltMoverDataStructBase or a User-Defined Struct type.")
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}
	else
	{
		P_NATIVE_BEGIN;

		if (TargetStructProp->Struct->IsA<UUserDefinedStruct>())
		{
			if (FJoltMoverDataStructBase* FoundDataInstance = TargetCollection.FindDataByType(TargetStructProp->Struct))
			{
				// User-defined struct instances are wrapped in a FJoltMoverUserDefinedDataStruct, so we need to extract the instance memory from inside it
				FJoltMoverUserDefinedDataStruct* FoundBPDataInstance = static_cast<FJoltMoverUserDefinedDataStruct*>(FoundDataInstance);
				TargetStructProp->Struct->CopyScriptStruct(TargetDataAsRawPtr, FoundBPDataInstance->StructInstance.GetMemory());
				DidSucceed = true;
			}
		}
		else
		{
			if (FJoltMoverDataStructBase* FoundDataInstance = TargetCollection.FindDataByType(TargetStructProp->Struct))
			{
				TargetStructProp->Struct->CopyScriptStruct(TargetDataAsRawPtr, FoundDataInstance);
				DidSucceed = true;
			}
		}

		P_NATIVE_END;
	}
}


void UJoltMoverDataCollectionLibrary::ClearDataFromCollection(FJoltMoverDataCollection& Collection)
{
	Collection.Empty();
}

#undef LOCTEXT_NAMESPACE
