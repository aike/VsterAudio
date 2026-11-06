#pragma once
#include <JuceHeader.h>
#include <map>
#include "PluginDpiPolicy.h"

// One editor window per chain slot, keyed by the slot's stable id. Windows
// must always be closed BEFORE their plugin instance is destroyed — callers
// do that by calling closeEditorFor() (or closeAll()) before touching the slot.
class PluginWindowManager
{
public:
    explicit PluginWindowManager (juce::PropertiesFile& settings);
    ~PluginWindowManager();   // defined in the .cpp where PluginWindow is complete

    void showEditorFor (int slotId, juce::AudioPluginInstance&, juce::Point<int> position);
    void closeEditorFor (int slotId);
    void closeAll();
    void forgetSlot (int slotId);   // closes the editor and drops the remembered position

    bool isOpen (int slotId) const;
    juce::Point<int> getPosition (int slotId) const;   // live position, last known, or a cascaded default

    // HiDPI handling for one plugin (see PluginDpiPolicy). Changing it reopens
    // the slot's editor so the new mode is visible at once.
    EditorScaling getEditorScaling (const juce::PluginDescription&) const;
    void setEditorScaling (int slotId, juce::AudioPluginInstance&, EditorScaling);

    // Called (message thread, asynchronously) when the hosting mode of a
    // slot's plugin changed: automatically because its editor turned out to
    // be cut off (`detected` true), or through setEditorScaling. The new mode
    // only takes effect once the plugin is loaded in a fresh process - a
    // plugin's module reads the display scale when it is first loaded and
    // stays loaded until NamCapt exits - so the handler offers a restart.
    // Without a handler the window alone is reopened.
    std::function<void (int slotId, bool detected)> onHostingModeChanged;

private:
    class PluginWindow;
    void reopen (int slotId);

    juce::PropertiesFile& settings;
    std::map<int, std::unique_ptr<PluginWindow>> windows;
    std::map<int, juce::Point<int>> lastPositions;

    JUCE_DECLARE_NON_COPYABLE (PluginWindowManager)
};
