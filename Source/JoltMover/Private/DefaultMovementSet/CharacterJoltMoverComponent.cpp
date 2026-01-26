// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultMovementSet/CharacterJoltMoverComponent.h"

#include "JoltBridgeCoreSettings.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/Singletons/JoltPhysicsWorldSubsystem.h"
#include "DefaultMovementSet/InstantMovementEffects/JoltBasicInstantMovementEffects.h"
#include "DefaultMovementSet/Modes/JoltKinematicFallingMode.h"
#include "DefaultMovementSet/Modes/JoltKinematicFlyingMode.h"
#include "DefaultMovementSet/Modes/JoltKinematicWalkingMode.h"
#include "DefaultMovementSet/Settings/JoltCommonLegacyMovementSettings.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "MoveLibrary/JoltFloorQueryUtils.h"
#include "MoveLibrary/JoltMovementUtils.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(CharacterJoltMoverComponent)

#if !UE_BUILD_SHIPPING
FAutoConsoleVariable CVarLogSimProxyMontageReplication(
	TEXT("jolt.mover.debug.LogSimProxyMontageReplication"),
	false,
	TEXT("Whether to log detailed information about montage replication on a sim proxy using the Character-focused MoverComponent. 0: Disable, 1: Enable"),
	ECVF_Cheat);
#endif	// !UE_BUILD_SHIPPING

UCharacterJoltMoverComponent::UCharacterJoltMoverComponent()
{
	// Default movement modes
	MovementModes.Add(DefaultModeNames::Walking, CreateDefaultSubobject<UJoltKinematicWalkingMode>(TEXT("DefaultWalkingMode")));
	MovementModes.Add(DefaultModeNames::Falling, CreateDefaultSubobject<UJoltKinematicFallingMode>(TEXT("DefaultFallingMode")));
	MovementModes.Add(DefaultModeNames::Flying,  CreateDefaultSubobject<UJoltKinematicFlyingMode>(TEXT("DefaultFlyingMode")));

	StartingMovementMode = DefaultModeNames::Falling;
}

void UCharacterJoltMoverComponent::BeginPlay()
{
	Super::BeginPlay();

	OnHandlerSettingChanged();

	//OnPostFinalize.AddDynamic(this, &UCharacterJoltMoverComponent::OnMoverPostFinalize);
}

bool UCharacterJoltMoverComponent::GetHandleJump() const
{
	return bHandleJump;
}

void UCharacterJoltMoverComponent::SetHandleJump(bool bInHandleJump)
{
	if (bHandleJump != bInHandleJump)
	{
		bHandleJump = bInHandleJump;
		OnHandlerSettingChanged();
	}
}

bool UCharacterJoltMoverComponent::GetHandleStanceChanges() const
{
	return bHandleStanceChanges;
}

void UCharacterJoltMoverComponent::SetHandleStanceChanges(bool bInHandleStanceChanges)
{
	if (bHandleStanceChanges != bInHandleStanceChanges)
	{
		bHandleStanceChanges = bInHandleStanceChanges;
		OnHandlerSettingChanged();
	}
}

bool UCharacterJoltMoverComponent::IsCrouching() const
{
	return HasGameplayTag(JoltMover_IsCrouching, true);
}

bool UCharacterJoltMoverComponent::IsFlying() const
{
	return HasGameplayTag(JoltMover_IsFlying, true);
}

bool UCharacterJoltMoverComponent::IsFalling() const
{
	return HasGameplayTag(JoltMover_IsFalling, true);
}

bool UCharacterJoltMoverComponent::IsAirborne() const
{
	return HasGameplayTag(JoltMover_IsInAir, true);
}

bool UCharacterJoltMoverComponent::IsOnGround() const
{
	return HasGameplayTag(JoltMover_IsOnGround, true);
}

bool UCharacterJoltMoverComponent::IsSwimming() const
{
	return HasGameplayTag(JoltMover_IsSwimming, true);
}

bool UCharacterJoltMoverComponent::IsSlopeSliding() const
{
	if (IsAirborne())
	{
		FJoltFloorCheckResult HitResult;
		const UJoltMoverBlackboard* MoverBlackboard = GetSimBlackboard();
		if (MoverBlackboard && MoverBlackboard->TryGet(CommonBlackboard::LastFloorResult, HitResult))
		{
			return HitResult.bBlockingHit && !HitResult.bWalkableFloor;
		}
	}

	return false;
}

bool UCharacterJoltMoverComponent::CanActorJump() const
{
	return IsOnGround();
}

bool UCharacterJoltMoverComponent::Jump()
{
	if (const UJoltCommonLegacyMovementSettings* CommonSettings = FindSharedSettings<UJoltCommonLegacyMovementSettings>())
	{
		TSharedPtr<FJumpImpulseEffect> JumpMove = MakeShared<FJumpImpulseEffect>();
		JumpMove->UpwardsSpeed = CommonSettings->JumpUpwardsSpeed;
		
		QueueInstantMovementEffect(JumpMove);

		return true;
	}

	return false;
}

bool UCharacterJoltMoverComponent::CanCrouch()
{
	return true;
}

void UCharacterJoltMoverComponent::Crouch()
{
	if (CanCrouch())
	{
		bWantsToCrouch = true;
	}
}

void UCharacterJoltMoverComponent::UnCrouch()
{
	bWantsToCrouch = false;
}

void UCharacterJoltMoverComponent::OnMoverPreSimulationTick(const FJoltMoverTimeStep& TimeStep, const FJoltMoverInputCmdContext& InputCmd)
{
	if (bHandleJump)
	{
		const FJoltCharacterDefaultInputs* CharacterInputs = InputCmd.Collection.FindDataByType<FJoltCharacterDefaultInputs>();
		if (CharacterInputs && CharacterInputs->bIsJumpJustPressed && CanActorJump())
		{
			Jump();
		}
	}
	
	if (bHandleStanceChanges)
	{
		const FStanceModifier* StanceModifier = static_cast<const FStanceModifier*>(FindMovementModifier(StanceModifierHandle));
		// This is a fail safe in case our handle was bad - try finding the modifier by type if we can
		if (!StanceModifier)
		{
			StanceModifier = FindMovementModifierByType<FStanceModifier>();
		}
	
		EStanceMode OldActiveStance = EStanceMode::Invalid;
		if (StanceModifier)
		{
			OldActiveStance = StanceModifier->ActiveStance;
		}
	
		const bool bIsCrouching = HasGameplayTag(JoltMover_IsCrouching, true);
		if (bIsCrouching && (!bWantsToCrouch || !CanCrouch()))
		{	
			if (StanceModifier && StanceModifier->CanExpand(this))
			{
				CancelModifierFromHandle(StanceModifier->GetHandle());
				StanceModifierHandle.Invalidate();

				StanceModifier = nullptr;
			}
		}
		else if (!bIsCrouching && bWantsToCrouch && CanCrouch())
		{
			TSharedPtr<FStanceModifier> NewStanceModifier = MakeShared<FStanceModifier>();
			StanceModifierHandle = QueueMovementModifier(NewStanceModifier);

			StanceModifier = NewStanceModifier.Get();
		}
	
		EStanceMode NewActiveStance = EStanceMode::Invalid;
		if (StanceModifier)
		{
			NewActiveStance = StanceModifier->ActiveStance;
		}

		if (OldActiveStance != NewActiveStance)
		{
			OnStanceChanged.Broadcast(OldActiveStance, NewActiveStance);
		}
	}
}

void UCharacterJoltMoverComponent::OnMoverPostFinalize(const FJoltMoverSyncState& SyncState, const FJoltMoverAuxStateContext& AuxState)
{
	UpdateSyncedMontageState(GetLastTimeStep(), SyncState, AuxState);
}

void UCharacterJoltMoverComponent::OnHandlerSettingChanged()
{
	const bool bIsHandlingAnySettings = bHandleJump || bHandleStanceChanges;

	if (bIsHandlingAnySettings)
	{
		OnPreSimulationTick.AddUniqueDynamic(this, &UCharacterJoltMoverComponent::OnMoverPreSimulationTick);
	}
	else
	{
		OnPreSimulationTick.RemoveDynamic(this, &UCharacterJoltMoverComponent::OnMoverPreSimulationTick);
	}
}

void UCharacterJoltMoverComponent::UpdateSyncedMontageState(const FJoltMoverTimeStep& TimeStep, const FJoltMoverSyncState& SyncState, const FJoltMoverAuxStateContext& AuxState)
{
	if (GetOwnerRole() == ROLE_SimulatedProxy)
	{
		const FJoltLayeredMove_MontageStateProvider* MontageStateProvider = static_cast<const FJoltLayeredMove_MontageStateProvider*>(SyncState.LayeredMoves.FindActiveMove(FJoltLayeredMove_MontageStateProvider::StaticStruct()));

		bool bShouldStopSyncedMontage = false;
		bool bShouldStartNewMontage = false;
		FJoltMoverAnimMontageState NewMontageState;

		if (SyncedMontageState.Montage)
		{
			if (MontageStateProvider)
			{
				NewMontageState = MontageStateProvider->GetMontageState();

				if (NewMontageState.Montage != SyncedMontageState.Montage)
				{
					bShouldStartNewMontage = true;
					bShouldStopSyncedMontage = true;
				}
			}
			else
			{
				bShouldStopSyncedMontage = true;
			}
		}
		else // We aren't actively syncing a montage state yet
		{
			if (MontageStateProvider)
			{
				// We have just received a montage state to sync against
				NewMontageState = MontageStateProvider->GetMontageState();
				bShouldStartNewMontage = true;
			}
		}

		if (bShouldStopSyncedMontage || bShouldStartNewMontage)
		{
			const USkeletalMeshComponent* MeshComp = Cast<USkeletalMeshComponent>(GetPrimaryVisualComponent());
			UAnimInstance* MeshAnimInstance = MeshComp ? MeshComp->GetAnimInstance() : nullptr;

			if (bShouldStopSyncedMontage)
			{
				#if !UE_BUILD_SHIPPING
				UE_CLOG(CVarLogSimProxyMontageReplication->GetBool(), LogJoltMover, Log, TEXT("JoltMover SP montage repl (SimF %i SimT: %.3f): STOP %s"),
					TimeStep.ServerFrame, TimeStep.BaseSimTimeMs, *SyncedMontageState.Montage->GetName());
				#endif // !UE_BUILD_SHIPPING

				if (MeshAnimInstance)
				{
					MeshAnimInstance->Montage_Stop(SyncedMontageState.Montage->GetDefaultBlendOutTime(), SyncedMontageState.Montage);
				}

				SyncedMontageState.Reset();
			}

			if (bShouldStartNewMontage && NewMontageState.Montage && MeshAnimInstance)
			{
				const float StartPosition = NewMontageState.CurrentPosition;
				const float PlaySeconds = MeshAnimInstance->Montage_Play(NewMontageState.Montage, NewMontageState.PlayRate, EMontagePlayReturnType::MontageLength, StartPosition);

				#if !UE_BUILD_SHIPPING
				UE_CLOG(CVarLogSimProxyMontageReplication->GetBool(), LogJoltMover, Log, TEXT("JoltMover SP montage repl (SimF %i SimT: %.3f): PLAY %s (StartPos: %.3f  Rate: %.3f  PlaySecs: %.3f)"),
					TimeStep.ServerFrame, TimeStep.BaseSimTimeMs, *NewMontageState.Montage->GetName(), StartPosition, NewMontageState.PlayRate, PlaySeconds);
				#endif // !UE_BUILD_SHIPPING

				if (PlaySeconds > 0.0f)
				{
					SyncedMontageState = NewMontageState;	// only consider us sync'd if the montage actually started
				}
			}
		}
	}
}

#pragma region JOLT PHYSICS
void UCharacterJoltMoverComponent::InitializeJoltCharacter()
{
	UJoltPhysicsWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogJoltMover, Error, TEXT("Could not find the Physics World Subsystem"))
		return;
	}
	
	if (UJoltPhysicsWorldSubsystem* S = GetWorld()->GetSubsystem<UJoltPhysicsWorldSubsystem>())
	{
		// TODO:@GreggoryAddison::CodeCompletion || Add support for kinematic mover
		S->RegisterJoltRigidBody(GetOwner());

		if (UpdatedCompAsPrimitive)
		{
			const float GravityFactor = FMath::Abs(S->GetJoltSettings()->WorldGravityAcceleration.Length() / GetGravityAcceleration().Length());
			S->SetGravityFactor(UpdatedCompAsPrimitive, GravityFactor);
		}
		
	}
	
}

void UCharacterJoltMoverComponent::InitializeWithJolt()
{
	Super::InitializeWithJolt();
	InitializeJoltCharacter();
}

#pragma endregion
