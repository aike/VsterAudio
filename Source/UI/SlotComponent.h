#pragma once
#include <JuceHeader.h>
#include "../Engine/AudioEngine.h"
#include "../Plugins/PluginHostManager.h"
#include "../Plugins/PluginWindowManager.h"

// One chain row: position label, plugin-name button (click = load/remove
// menu), bypass toggle, a button to open the plugin's editor, move up/down
// arrows and a delete button. Plugins come from the known-plugin list
// (populated by the Plugin Manager), effects only, listed by manufacturer or
// by category.
class SlotComponent : public juce::Component
{
public:
    static constexpr int rowHeight = 26;

    SlotComponent (int slotId, AudioEngine&, PluginHostManager&, PluginWindowManager&);

    void resized() override;
    void refresh();   // re-read slot state (index, plugin, bypass) from the engine

    int getSlotId() const { return slotId; }

    // Wired up by the chain list / MainComponent, which own the actual logic.
    std::function<void (int slotId, const juce::PluginDescription&)> onLoadRequested;
    std::function<void (int slotId)> onClearRequested;        // remove the plugin, keep the slot
    std::function<void (int slotId)> onDeleteRequested;       // remove the whole slot
    std::function<void (int slotId, int delta)> onMoveRequested;

private:
    void showMenu();

    int slotId;
    AudioEngine& engine;
    PluginHostManager& plugins;
    PluginWindowManager& windows;

    juce::Label indexLabel;
    juce::TextButton nameButton;
    juce::TextButton bypassButton { "BYP" };
    juce::TextButton uiButton { "UI" };
    juce::ArrowButton upButton   { "up",   0.75f, juce::Colours::lightgrey };
    juce::ArrowButton downButton { "down", 0.25f, juce::Colours::lightgrey };
    juce::TextButton deleteButton { "X" };
};
