// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltMoverCVDStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Slate/SlateGameResources.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FJoltMoverCVDStyle::StyleInstance = nullptr;

void FJoltMoverCVDStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FJoltMoverCVDStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FJoltMoverCVDStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("JoltMoverCVDStyle"));
	return StyleSetName;
}

const FVector2D Icon16x16(16.0f, 16.0f);

TSharedRef< FSlateStyleSet > FJoltMoverCVDStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("JoltMoverCVDStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("JoltNPP")->GetBaseDir() / TEXT("Resources"));

	Style->Set("TabIconJoltMoverInfoPanel",  new IMAGE_BRUSH_SVG(TEXT("MoverInfo"), Icon16x16));

	return Style;
}

void FJoltMoverCVDStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FJoltMoverCVDStyle::Get()
{
	return *StyleInstance;
}
