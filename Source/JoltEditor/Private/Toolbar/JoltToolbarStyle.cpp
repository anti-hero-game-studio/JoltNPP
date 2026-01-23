// Fill out your copyright notice in the Description page of Project Settings.


#include "Toolbar/JoltToolbarStyle.h"

#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FJoltToolbarStyle::StyleInstance = nullptr;

void FJoltToolbarStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FJoltToolbarStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FJoltToolbarStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("JoltEditor"));
	return StyleSetName;
}


const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);

TSharedRef< FSlateStyleSet > FJoltToolbarStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("JoltEditor"));
	Style->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate/"));

	Style->Set("Jolt.ConvertActorsAction", new IMAGE_BRUSH_SVG( "Starship/Common/Apply", Icon20x20));
	return Style;
}

void FJoltToolbarStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FJoltToolbarStyle::Get()
{
	return *StyleInstance;
}
