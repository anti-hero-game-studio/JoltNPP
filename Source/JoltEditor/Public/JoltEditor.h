#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FToolBarBuilder;
class FMenuBuilder;


class FJoltEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    
    
    /** This function will be bound to Command. */
    void PluginButtonClicked();
	
private:

    void RegisterMenus();


private:
    TSharedPtr<class FUICommandList> PluginCommands;
};
