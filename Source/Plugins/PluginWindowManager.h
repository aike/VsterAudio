#pragma once
#include <JuceHeader.h>
#include <map>

// One editor window per chain slot, keyed by the slot's stable id. Windows
// must always be closed BEFORE their plugin instance is destroyed — callers
// do that by calling closeEditorFor() (or closeAll()) before touching the slot.
class PluginWindowManager
{
public:
    PluginWindowManager();
    ~PluginWindowManager();   // defined in the .cpp where PluginWindow is complete

    void showEditorFor (int slotId, juce::AudioPluginInstance&, juce::Point<int> position);
    void closeEditorFor (int slotId);
    void closeAll();
    void forgetSlot (int slotId);   // closes the editor and drops the remembered position

    bool isOpen (int slotId) const;
    juce::Point<int> getPosition (int slotId) const;   // live position, last known, or a cascaded default

private:
    class PluginWindow;
    std::map<int, std::unique_ptr<PluginWindow>> windows;
    std::map<int, juce::Point<int>> lastPositions;

    JUCE_DECLARE_NON_COPYABLE (PluginWindowManager)
};
