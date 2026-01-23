#include "JoltEditor.h"
#include "Selection.h"
#include "Core/BaseClasses/JoltStaticMeshActor.h"
#include "Factories/JoltStaticMeshActorFactory.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Toolbar/JoltToolbarCommands.h"
#include "Toolbar/JoltToolbarStyle.h"

#define LOCTEXT_NAMESPACE "FJoltEditorModule"

void FJoltEditorModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FJoltToolbarStyle::Initialize();
	FJoltToolbarStyle::ReloadTextures();

	FJoltToolbarCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FJoltToolbarCommands::Get().PluginAction,
		FExecuteAction::CreateRaw(this, &FJoltEditorModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FJoltEditorModule::RegisterMenus));
	/*if (GEditor)
	{
		UJoltStaticMeshActorFactory* NewFactory = NewObject<UJoltStaticMeshActorFactory>();
		GEditor->ActorFactories.Insert(NewFactory,0);
	}*/
}

void FJoltEditorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FJoltToolbarStyle::Shutdown();

	FJoltToolbarCommands::Unregister();
	
	
	/*if (GEditor)
	{
		GEditor->ActorFactories.RemoveAll([](UActorFactory* Factory) 
		{
			return Factory && Factory->IsA<UJoltStaticMeshActorFactory>();
		});
	}*/
}

void FJoltEditorModule::PluginButtonClicked()
{
	// Put your "OnButtonClicked" stuff here
	
	TSet<FString> ComponentsToConsider;
	ComponentsToConsider.Add("StaticMeshComponent");

	TArray<TWeakObjectPtr<UObject>> SelectedObjects;
	GEditor->GetSelectedActors()->GetSelectedObjects(SelectedObjects);

	UClass* ReplaceWithClass = AJoltStaticMeshActor::StaticClass();
	
	TArray<AActor*> SelectedActors;
	for (TWeakObjectPtr<UObject> S : SelectedObjects)
	{
		if (!Cast<AStaticMeshActor>(S)) continue;
		
		SelectedActors.Add(Cast<AActor>(S));
	}

	UEditorActorSubsystem::ConvertActors(SelectedActors, ReplaceWithClass, ComponentsToConsider, true);


	if (SelectedActors.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("No static mesh actors are selected")));
		return;
	}

	
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Selected Static Mesh Actors Converted To Jolt Static Mesh Actors")));
}

void FJoltEditorModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FJoltToolbarCommands::Get().PluginAction,
				PluginCommands,
				TAttribute<FText>(FText::FromString(TEXT("Convert Actors"))),
					FText::FromString(TEXT("Converts selected AStaticMeshActor mesh components to UJoltStaticMeshComponent ")),
					 FSlateIcon(FAppStyle::Get().GetStyleSetName(), "MergeActors.MeshMergingTool"));
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FJoltToolbarCommands::Get().PluginAction));
				Entry.SetCommandList(PluginCommands);
				
				Entry.Icon = FSlateIcon(FAppStyle::Get().GetStyleSetName(), "MergeActors.MeshMergingTool");
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FJoltEditorModule, JoltEditor)