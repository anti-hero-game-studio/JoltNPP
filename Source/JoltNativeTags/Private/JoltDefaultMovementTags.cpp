// Fill out your copyright notice in the Description page of Project Settings.


#include "JoltDefaultMovementTags.h"


#define LOCTEXT_NAMESPACE "JoltData"

UE_DEFINE_GAMEPLAY_TAG_COMMENT(Jolt_IsOnGround, "Jolt.IsOnGround", "Default Jolt state flag indicating character is on the ground.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Jolt_IsInAir, "Jolt.IsInAir", "Default Jolt state flag indicating character is in the air.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Jolt_IsFalling, "Jolt.IsFalling", "Default Jolt state flag indicating character is falling.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Jolt_IsFlying, "Jolt.IsFlying", "Default Jolt state flag indicating character is flying.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Jolt_IsSwimming, "Jolt.IsSwimming", "Default Jolt state flag indicating character is swimming.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Jolt_IsCrouching, "Jolt.Stance.IsCrouching", "Default Jolt state flag indicating character is crouching.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Jolt_IsNavWalking, "Jolt.IsNavWalking", "Default Jolt state flag indicating character is NavWalking.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Jolt_SkipAnimRootMotion, "Jolt.SkipAnimRootMotion", "Default Jolt state flag indicating Animation Root Motion proposed movement should be skipped.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Jolt_SkipVerticalAnimRootMotion, "Jolt.SkipVerticalAnimRootMotion", "Default Jolt state flag indicating Animation Root Motion proposed movements should not include a vertical velocity component (along the up/down axis).");
