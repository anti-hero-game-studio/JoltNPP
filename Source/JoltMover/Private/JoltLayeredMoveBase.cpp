// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltLayeredMoveBase.h"
#include "JoltMoverLog.h"
#include "JoltMoverComponent.h"
#include "JoltMoverSimulationTypes.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "MoveLibrary/JoltMovementMixer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltLayeredMoveBase)

#define LOCTEXT_NAMESPACE "JoltMover"

//////////////////////////////////////////////////////////////////////////
// FJoltLayeredMoveInstancedData

bool FJoltLayeredMoveInstancedData::operator==(const FJoltLayeredMoveInstancedData& Other) const
{
	//@todo DanH: Compare logic class compatibility?
	if (GetScriptStruct() == Other.GetScriptStruct())
	{
		return Equals(Other);
	}
	return false;
}

bool FJoltLayeredMoveInstancedData::Equals(const FJoltLayeredMoveInstancedData& OtherData) const
{
	return StartSimTimeMs == OtherData.StartSimTimeMs
		&& DurationMs == OtherData.DurationMs;
}

void FJoltLayeredMoveInstancedData::ActivateFromContext(const FJoltLayeredMoveActivationParams* ActivationParams)
{
	
}

void FJoltLayeredMoveInstancedData::NetSerialize(FArchive& Ar)
{
	Ar << StartSimTimeMs;
	Ar << DurationMs;
}

//////////////////////////////////////////////////////////////////////////
// UJoltLayeredMoveLogic

UJoltLayeredMoveLogic::UJoltLayeredMoveLogic()
	: InstancedDataStructType(FJoltLayeredMoveInstancedData::StaticStruct())
{
}

bool UJoltLayeredMoveLogic::IsFinished_Implementation(const FJoltMoverTimeStep& TimeStep, const UJoltMoverBlackboard* SimBlackboard)
{
	const FJoltLayeredMoveInstancedData& ExecData = AccessExecutionMoveData();
	const double DurationMs = ExecData.DurationMs;
	if (DurationMs < 0.f)
	{
		return false;
	}
	
	const double StartSimTimeMs = ExecData.GetStartSimTimeMs();
	
	const bool bHasStarted = StartSimTimeMs >= 0.f;
	const bool bTimeExpired = StartSimTimeMs + DurationMs <= TimeStep.BaseSimTimeMs;
	return bHasStarted && bTimeExpired;	
}

bool UJoltLayeredMoveLogic::K2_GetActiveMoveData(UJoltLayeredMoveLogic* MoveLogic, FJoltLayeredMoveInstancedData& OutMoveData)
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
	return false;
}


DEFINE_FUNCTION(UJoltLayeredMoveLogic::execK2_GetActiveMoveData)
{
	P_GET_OBJECT(UJoltLayeredMoveLogic, MoveLogic);
	
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);

	const FStructProperty* MoveDataProperty = CastField<FStructProperty>(Stack.MostRecentProperty);
	uint8* OutMoveDataPtr = Stack.MostRecentPropertyAddress;
	
	P_FINISH

	const bool bHasValidMoveData = ValidateMoveDataGetSet(P_THIS, MoveLogic, MoveDataProperty, OutMoveDataPtr, Stack);
	if (bHasValidMoveData)
	{
		// Write the active move data to the function output
		P_NATIVE_BEGIN;
		MoveDataProperty->Struct->CopyScriptStruct(OutMoveDataPtr, MoveLogic->CurrentInstancedData.Get());
		P_NATIVE_END;
	}
	
	*(bool*)RESULT_PARAM = bHasValidMoveData;
}

void UJoltLayeredMoveLogic::K2_SetActiveMoveData(UJoltLayeredMoveLogic* MoveLogic, const FJoltLayeredMoveInstancedData& OutMoveData)
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
}

DEFINE_FUNCTION(UJoltLayeredMoveLogic::execK2_SetActiveMoveData)
{
	P_GET_OBJECT(UJoltLayeredMoveLogic, MoveLogic);
	
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);

	const FStructProperty* MoveDataProperty = CastField<FStructProperty>(Stack.MostRecentProperty);
	const uint8* MoveDataPtr = Stack.MostRecentPropertyAddress;

	P_FINISH

	if (ValidateMoveDataGetSet(P_THIS, MoveLogic, MoveDataProperty, MoveDataPtr, Stack))
	{
		// Overwrite the contents of the current move data with that provided
		P_NATIVE_BEGIN;
		MoveDataProperty->Struct->CopyScriptStruct(MoveLogic->CurrentInstancedData.Get(), MoveDataPtr);
		P_NATIVE_END;
	}
}

FJoltLayeredMoveInstance::FJoltLayeredMoveInstance()
	: InstanceMoveData(nullptr)
	, MoveLogic(nullptr)
{
}

FJoltLayeredMoveInstance::FJoltLayeredMoveInstance(const FJoltLayeredMoveInstance& OtherActiveLayeredMove)
	: InstanceMoveData(OtherActiveLayeredMove.InstanceMoveData)
	, MoveLogic(OtherActiveLayeredMove.MoveLogic)
{
}

FJoltLayeredMoveInstance::FJoltLayeredMoveInstance(const TSharedRef<FJoltLayeredMoveInstancedData>& InMoveData, UJoltLayeredMoveLogic* InMoveLogic)
	: InstanceMoveData(InMoveData)
	, MoveLogic(InMoveLogic)
{
}

bool UJoltLayeredMoveLogic::ValidateMoveDataGetSet(const UObject* ObjectValidatingData, const UJoltLayeredMoveLogic* MoveLogic, const FStructProperty* MoveDataProperty, const uint8* MoveDataPtr, FFrame& StackFrame)
{
	FText BlueprintExceptionText;
	bool bIsValid = true;
	if (!MoveLogic)
	{
		BlueprintExceptionText = LOCTEXT("ValidateMoveDataNoMoveLogic", "No MoveLogic was present.");
		bIsValid = false;
	}
	else if (!MoveLogic->CurrentInstancedData)
	{
		BlueprintExceptionText = LOCTEXT("ValidateMoveDataNoCurrentActiveMoveData", "No CurrentActiveMoveData on the MoveLogic was present.");
		bIsValid = false;
	}
	else if (!MoveDataProperty)
	{
		BlueprintExceptionText = LOCTEXT("ValidateMoveDataNoMoveData", "No MoveData was present.");
		bIsValid = false;
	}
	else if (!MoveDataPtr)
	{
		BlueprintExceptionText = LOCTEXT("ValidateMoveDataNoMoveData", "No MoveData was present.");
		bIsValid = false;
	}
	else if (MoveDataProperty->Struct != MoveLogic->InstancedDataStructType.Get())
	{
		BlueprintExceptionText = FText::Format(LOCTEXT("ValidateMoveDataMisMatchData", "MoveData passed in did not match MoveLogic Active mode data. Expected: {0}. Found: {1}."),
			FText::FromString(MoveDataProperty->Struct.GetName()),
			FText::FromString(MoveLogic->InstancedDataStructType.GetName())
		);
		bIsValid = false;
	}

	if (!bIsValid)
	{
		const FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::NonFatalError,
			BlueprintExceptionText
		);
		
		FBlueprintCoreDelegates::ThrowScriptException(ObjectValidatingData, StackFrame, ExceptionInfo);
	}
	
	return bIsValid;
}

//////////////////////////////////////////////////////////////////////////
// FJoltLayeredMoveInstance

/**
 * Scoped wrapper that is the only means of calling the virtual functions in UJoltLayeredMoveLogic
 * that depend on/expect access to valid active move data.
 */
class FScopedMoveLogicExecContext
{
public:	
	FScopedMoveLogicExecContext(UJoltLayeredMoveLogic& MoveLogic, const TSharedRef<FJoltLayeredMoveInstancedData>& MoveData)
		: LogicObj(MoveLogic)
	{
		MoveLogic.CurrentInstancedData = MoveData.ToSharedPtr();
	}

	template <typename... ArgsT>
    void OnStart(ArgsT&&... Args)
    {
    	LogicObj.OnStart(Args...);
    }
	
	template <typename... ArgsT>
	void OnEnd(ArgsT&&... Args)
	{
		LogicObj.OnEnd(Args...);
	}
	
	template <typename... ArgsT>
	bool GenerateMove(ArgsT&&... Args)
	{
		return LogicObj.GenerateMove(Args...);
	}

	template <typename... ArgsT>
	bool IsFinished(ArgsT&&... Args)
	{
		return LogicObj.IsFinished(Args...);
	}
	
	~FScopedMoveLogicExecContext()
	{
		LogicObj.CurrentInstancedData.Reset();
	}

private:
	UJoltLayeredMoveLogic& LogicObj;
};

void FJoltLayeredMoveInstance::StartMove(const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard) const
{
	if (ensure(MoveLogic))
	{
		FScopedMoveLogicExecContext LogicExecContext(*MoveLogic, InstanceMoveData.ToSharedRef());
		LogicExecContext.OnStart(TimeStep, SimBlackboard);
		
		InstanceMoveData->StartSimTimeMs = TimeStep.BaseSimTimeMs;
	}
}

bool FJoltLayeredMoveInstance::GenerateMove(const FJoltMoverTickStartData& StartState, const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard, FJoltProposedMove& OutProposedMove) const
{
	if (ensure(MoveLogic))
	{
		FScopedMoveLogicExecContext LogicExecContext(*MoveLogic, InstanceMoveData.ToSharedRef());
		if (LogicExecContext.GenerateMove(TimeStep, SimBlackboard, StartState, OutProposedMove))
		{
			//@todo DanH: Feels like the preferred mode from moves should come from OnStart if that's the only time we listen to it
			// Wipe the preferred mode if this wasn't the first tick of the mode
			if (InstanceMoveData->GetStartSimTimeMs() < TimeStep.BaseSimTimeMs)
			{
				OutProposedMove.PreferredMode = NAME_None;
			}
			return true;
		}
	}
	
	return false;
}

void FJoltLayeredMoveInstance::EndMove(const FJoltMoverTimeStep& TimeStep, UJoltMoverBlackboard* SimBlackboard) const
{
	if (ensure(MoveLogic))
	{
		FScopedMoveLogicExecContext LogicExecContext(*MoveLogic, InstanceMoveData.ToSharedRef());
		LogicExecContext.OnEnd(TimeStep, SimBlackboard);
	}
}

bool FJoltLayeredMoveInstance::IsFinished(const FJoltMoverTimeStep& TimeStep, const UJoltMoverBlackboard* SimBlackboard) const
{
	if (ensure(MoveLogic))
	{
		FScopedMoveLogicExecContext LogicExecContext(*MoveLogic, InstanceMoveData.ToSharedRef());
		return LogicExecContext.IsFinished(TimeStep, SimBlackboard);
	}
	return true;
}

const FJoltLayeredMoveFinishVelocitySettings& FJoltLayeredMoveInstance::GetFinishVelocitySettings() const
{
	if (MoveLogic)
	{
		return MoveLogic->GetFinishVelocitySettings();
	}
	
	static const FJoltLayeredMoveFinishVelocitySettings Defaults;
	return Defaults;
}

EJoltMoveMixMode FJoltLayeredMoveInstance::GetMixMode() const
{
	if (MoveLogic)
	{
		return MoveLogic->GetMixMode();
	}
	return EJoltMoveMixMode::AdditiveVelocity;
}

FJoltLayeredMoveInstance FJoltLayeredMoveInstance::Clone() const
{
	return FJoltLayeredMoveInstance(TSharedRef<FJoltLayeredMoveInstancedData>(InstanceMoveData->Clone()), MoveLogic.Get());
}

void FJoltLayeredMoveInstance::NetSerialize(FArchive& Ar)
{
	// Step carefully! When writing from an archive, this move may have been created via TArray::AddZeroed, which does not call the constructor
	// Therefore, this is the one spot where we are actually ok with MoveData being null, but that violates the rules of TSharedRef.
	// So the ref is converted first to a TSharedPtr so we can safely, albeit cheekily, check it for validity.
	TSharedPtr<FJoltLayeredMoveInstancedData> PossiblyNullData = InstanceMoveData;
	TCheckedObjPtr<UScriptStruct> DataStructType = PossiblyNullData ? PossiblyNullData->GetScriptStruct() : nullptr;
	UScriptStruct* ExistingDataType = DataStructType.Get();
	Ar << DataStructType;

	UClass* CurrentMoveLogicClassType = MoveLogic ? MoveLogic->GetClass() : nullptr;
	
	Ar << CurrentMoveLogicClassType;
	if (!MoveLogic)
	{
		MoveLogicClassType = CurrentMoveLogicClassType;
	}
	
	if (DataStructType.IsValid())
	{
		//@todo DanH: Fwiw FJoltLayeredMoveInstanceGroup is not actually ever expected to be sent in a server RPC right?
		// Restrict replication to derived classes of FJoltLayeredMoveInstancedData for security reasons:
		// If FJoltLayeredMoveInstanceGroup is replicated through a Server RPC, we need to prevent clients from sending us
		// arbitrary ScriptStructs due to the allocation/reliance on GetCppStructOps below which could trigger a server crash
		// for invalid structs.
		if (DataStructType->IsChildOf(FJoltLayeredMoveInstancedData::StaticStruct()))
		{
			// If the struct type at this index should be different from the one that's already here, we need to change it
			if (DataStructType.Get() != ExistingDataType && ensure(Ar.IsLoading()))
			{
				//@todo DanH: There was a comment here about doing it this way "for now", but it was copy-pasted from FRootMotionSourceGroup::NetSerializeRMSArray. Is it valid?
				FJoltLayeredMoveInstancedData* NewMove = (FJoltLayeredMoveInstancedData*)FMemory::Malloc(DataStructType->GetCppStructOps()->GetSize());
				DataStructType->InitializeStruct(NewMove);

				struct FSerializationAllocatedLayeredMoveDataDeleter
				{
					FORCEINLINE void operator()(FJoltLayeredMoveInstancedData* MoveData) const
					{
						check(MoveData);
						UScriptStruct* ScriptStruct = MoveData->GetScriptStruct();
						check(ScriptStruct);
						ScriptStruct->DestroyStruct(MoveData);
						FMemory::Free(MoveData);
					}
				};
				InstanceMoveData = TSharedRef<FJoltLayeredMoveInstancedData>(NewMove, FSerializationAllocatedLayeredMoveDataDeleter());
			}

			InstanceMoveData->NetSerialize(Ar);
		}
		else
		{
			UE_LOG(LogJoltMover, Error, TEXT("FJoltLayeredMoveInstanceGroup::NetSerialize: ScriptStruct [%s] not derived from FJoltLayeredMoveInstancedData attempted to serialize."),
				*DataStructType->GetName());
			Ar.SetError();
		}
	}
	else if (DataStructType.IsError())
	{
		UE_LOG(LogJoltMover, Error, TEXT("FJoltLayeredMoveInstanceGroup::NetSerialize: Invalid ScriptStruct serialized."));
		Ar.SetError();
	}
}

const UClass* FJoltLayeredMoveInstance::GetSerializedMoveLogicClass() const
{
	return MoveLogicClassType;
}

bool FJoltLayeredMoveInstance::PopulateMissingActiveMoveLogic(const TArray<TObjectPtr<UJoltLayeredMoveLogic>>& RegisteredMoves)
{
	if (!HasLogic())
	{
		bool bFoundMissingLogic = false;
		
		if (MoveLogicClassType)
		{
			for (const TObjectPtr<UJoltLayeredMoveLogic>& RegisteredMoveLogic : RegisteredMoves)
			{
				if (RegisteredMoveLogic->GetClass() == MoveLogicClassType)
				{
					MoveLogic = RegisteredMoveLogic;
					
					bFoundMissingLogic = true;
					break;
				}
			}

			if (!bFoundMissingLogic)
			{
				UE_LOG(LogJoltMover, Warning, TEXT("Active Layered Move couldn't find it's serialized MoveLogicClass (%s) among registered MoveLogic"), *MoveLogicClassType->GetName());
			}
		}
		else
		{
			UE_LOG(LogJoltMover, Warning, TEXT("Active Layered Move didn't have a valid logic class or class type to search for"));
		}

		return bFoundMissingLogic;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
