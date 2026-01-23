// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Build.h"

#ifndef JNP_ENSURES_ALWAYS
#define JNP_ENSURES_ALWAYS 0 
#endif

#define JNP_CHECKS_AND_ENSURES 1
#if JNP_CHECKS_AND_ENSURES
	#define jnpCheck(Condition) check(Condition)
	#define jnpCheckf(Condition, ...) checkf(Condition, ##__VA_ARGS__)
	#if JNP_ENSURES_ALWAYS
		#define jnpEnsure(Condition) ensureAlways(Condition)
		#define jnpEnsureMsgf(Condition, ...) ensureAlwaysMsgf(Condition, ##__VA_ARGS__)
	#else
		#define jnpEnsure(Condition) ensure(Condition)
		#define jnpEnsureMsgf(Condition, ...) ensureMsgf(Condition, ##__VA_ARGS__)
	#endif
#else
	#define jnpCheck(...)
	#define jnpCheckf(...)
	#define jnpEnsure(Condition) (!!(Condition))
	#define jnpEnsureMsgf(Condition, ...) (!!(Condition))
#endif

#define JNP_CHECKS_AND_ENSURES_SLOW !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#if JNP_CHECKS_AND_ENSURES_SLOW
	#define jnpCheckSlow(Condition) check(Condition)
	#define jnpCheckfSlow(Condition, ...) checkf(Condition, ##__VA_ARGS__)
	#if JNP_ENSURES_ALWAYS
		#define jnpEnsureSlow(Condition) ensureAlways(Condition)
		#define jnpEnsureMsgfSlow(Condition, ...) ensureAlwaysMsgf(Condition, ##__VA_ARGS__)
	#else
		#define jnpEnsureSlow(Condition) ensure(Condition)
		#define jnpEnsureMsgfSlow(Condition, ...) ensureMsgf(Condition, ##__VA_ARGS__)
	#endif
#else
	#define jnpCheckSlow(Condition)
	#define jnpCheckfSlow(...)
	#define jnpEnsureSlow(Condition) (!!(Condition))
	#define jnpEnsureMsgfSlow(Condition, ...) (!!(Condition))
#endif