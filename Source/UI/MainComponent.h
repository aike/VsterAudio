#pragma once
#include <JuceHeader.h>
#include "../Engine/AudioEngine.h"
#include "../Plugins/PluginHostManager.h"
#include "../Plugins/PluginWindowManager.h"
#include "../Session/SessionManager.h"
#include "TransportBar.h"
#include "ChainListComponent.h"
#include "LevelMeterComponent.h"
#include "PluginManagerWindow.h"

class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      public juce::ApplicationCommandTarget
{
public:
    static constexpr int minimumHeight = 230;   // transport + a few rows + the Add button

    MainComponent (AudioEngine&, PluginHostManager&, PluginWindowManager&, SessionManager&);
    ~MainComponent() override;

    void resized() override;

    // MenuBarModel (the owning window installs this as its menu bar model)
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    // ApplicationCommandTarget (Save lives here so Ctrl+S works app-wide)
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

private:
    enum MenuIds
    {
        menuFileNew = 1,
        menuFileLoad,
        menuFileSaveAs,
        menuOptionAudioSettings,
        menuOptionPluginManager
    };

    enum CommandIds
    {
        commandSave = 0x2001
    };

    void showAudioSettings();
    void addSlot();
    void loadPluginIntoSlot (int slotId, const juce::PluginDescription&);
    void clearSlot (int slotId);
    void deleteSlot (int slotId);
    void moveSlot (int slotId, int delta);
    void syncControls();   // after a session load / New

    AudioEngine& engine;
    PluginHostManager& plugins;
    PluginWindowManager& windows;
    SessionManager& session;

    juce::ApplicationCommandManager commandManager;

    TransportBar transport { engine };
    ChainListComponent chainList { engine, plugins, windows };

    // Input strip (left)
    juce::Label inputLabel { {}, "INPUT" };
    juce::ComboBox inputModeBox;
    juce::Slider inputGainSlider;
    LevelMeterComponent inputMeter { engine.inputMeter };

    // Master strip (right)
    juce::Label volumeLabel { {}, "MASTER" };
    juce::Slider volumeSlider;
    LevelMeterComponent outputMeter { engine.outputMeter };

    std::unique_ptr<PluginManagerWindow> pluginManagerWindow;
    juce::TooltipWindow tooltipWindow { nullptr, 600 };
};
