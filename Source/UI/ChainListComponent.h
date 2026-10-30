#pragma once
#include <JuceHeader.h>
#include "../Engine/AudioEngine.h"
#include "../Plugins/PluginHostManager.h"
#include "../Plugins/PluginWindowManager.h"
#include "SlotComponent.h"

// The scrollable list of effect rows plus the "Add Effect" button. Rows are
// rebuilt from the engine's chain whenever its structure changes, so the
// view never holds state of its own.
class ChainListComponent : public juce::Component
{
public:
    ChainListComponent (AudioEngine&, PluginHostManager&, PluginWindowManager&);

    void resized() override;

    void rebuild();              // recreate every row from the chain
    void refreshRow (int slotId);
    void refreshAll();           // re-read state of the existing rows (index, bypass, name)
    void scrollToBottom();       // show the row that was just added

    // Forwarded from the rows; MainComponent owns the actual logic.
    std::function<void (int slotId, const juce::PluginDescription&)> onLoadRequested;
    std::function<void (int slotId)> onClearRequested;
    std::function<void (int slotId)> onDeleteRequested;
    std::function<void (int slotId, int delta)> onMoveRequested;
    std::function<void()> onAddRequested;

private:
    static constexpr int rowGap = 4;

    struct Rows : public juce::Component
    {
        void resized() override;
        std::vector<std::unique_ptr<SlotComponent>> rows;
    };

    void layoutRows();
    SlotComponent* findRow (int slotId) const;

    AudioEngine& engine;
    PluginHostManager& plugins;
    PluginWindowManager& windows;

    juce::Viewport viewport;
    Rows rows;
    juce::TextButton addButton { "+ Add Effect" };
};
