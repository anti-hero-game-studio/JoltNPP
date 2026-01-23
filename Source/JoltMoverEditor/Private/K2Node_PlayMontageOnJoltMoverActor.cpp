// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_PlayMontageOnJoltMoverActor.h"

#include "Containers/UnrealString.h"
#include "EdGraph/EdGraphPin.h"
#include "MoveLibrary/PlayJoltMoverMontageCallbackProxy.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(K2Node_PlayMontageOnJoltMoverActor)

#define LOCTEXT_NAMESPACE "JoltMover_K2Nodes"

UK2Node_PlayMontageOnJoltMoverActor::UK2Node_PlayMontageOnJoltMoverActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ProxyFactoryFunctionName = GET_FUNCTION_NAME_CHECKED(UPlayJoltMoverMontageCallbackProxy, CreateProxyObjectForPlayMoverMontage);
	ProxyFactoryClass = UPlayJoltMoverMontageCallbackProxy::StaticClass();
	ProxyClass = UPlayJoltMoverMontageCallbackProxy::StaticClass();
}

FText UK2Node_PlayMontageOnJoltMoverActor::GetTooltipText() const
{
	return LOCTEXT("K2Node_PlayMontageOnJoltMoverActor_Tooltip", "Plays a Montage on an actor with JoltMover and SkeletalMesh components. Used for networked animation root motion.");
}

FText UK2Node_PlayMontageOnJoltMoverActor::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("PlayMontageOnJoltMoverActor", "Play Montage (JoltMover Actor)");
}

FText UK2Node_PlayMontageOnJoltMoverActor::GetMenuCategory() const
{
	return LOCTEXT("PlayMontageCategory", "Animation|Montage");
}

static const FName NAME_OnNotifyBegin = "OnNotifyBegin";
static const FName NAME_OnNotifyEnd = "OnNotifyEnd";

void UK2Node_PlayMontageOnJoltMoverActor::GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const
{
	Super::GetPinHoverText(Pin, HoverTextOut);

	if (Pin.PinName == NAME_OnNotifyBegin)
	{
		FText ToolTipText = LOCTEXT("K2Node_PlayMontageOnJoltMoverActor_OnNotifyBegin_Tooltip", "Event called when using a PlayMontageNotify or PlayMontageNotifyWindow Notify in a Montage.");
		HoverTextOut = FString::Printf(TEXT("%s\n%s"), *ToolTipText.ToString(), *HoverTextOut);
	}
	else if (Pin.PinName == NAME_OnNotifyEnd)
	{
		FText ToolTipText = LOCTEXT("K2Node_PlayMontageOnJoltMoverActor_OnNotifyEnd_Tooltip", "Event called when using a PlayMontageNotifyWindow Notify in a Montage.");
		HoverTextOut = FString::Printf(TEXT("%s\n%s"), *ToolTipText.ToString(), *HoverTextOut);
	}
}

#undef LOCTEXT_NAMESPACE
