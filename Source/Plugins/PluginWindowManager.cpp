#include "PluginWindowManager.h"

class PluginWindowManager::PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioPluginInstance& p, std::function<void()> onUserClose)
        : juce::DocumentWindow (p.getName(),
                                juce::Desktop::getInstance().getDefaultLookAndFeel()
                                    .findColour (juce::ResizableWindow::backgroundColourId),
                                juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton),
          onClose (std::move (onUserClose))
    {
        setUsingNativeTitleBar (true);   // VST3 editors behave better under native title bars

        auto* editor = p.createEditorIfNeeded();
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (p);

        // Escape hatch for plugins that render at the wrong HiDPI scale:
        // editor->setScaleFactor (1.0f);

        setContentOwned (editor, true);   // the window deletes the editor
        setResizable (editor->isResizable(), false);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        if (onClose)
            onClose();
    }

private:
    std::function<void()> onClose;
};

PluginWindowManager::PluginWindowManager() = default;
PluginWindowManager::~PluginWindowManager() { closeAll(); }

void PluginWindowManager::showEditorFor (int slotId, juce::AudioPluginInstance& instance, juce::Point<int> position)
{
    if (auto it = windows.find (slotId); it != windows.end())
    {
        it->second->toFront (true);
        return;
    }

    // Deleting a window from inside its own close callback would be deleting
    // `this` mid-callback, so defer it.
    auto window = std::make_unique<PluginWindow> (instance, [this, slotId]
    {
        juce::MessageManager::callAsync ([this, slotId] { closeEditorFor (slotId); });
    });
    window->setTopLeftPosition (position);
    lastPositions[slotId] = position;
    windows[slotId] = std::move (window);
}

void PluginWindowManager::closeEditorFor (int slotId)
{
    if (auto it = windows.find (slotId); it != windows.end())
    {
        lastPositions[slotId] = it->second->getPosition();
        windows.erase (it);
    }
}

void PluginWindowManager::closeAll()
{
    // Collect the ids first: closeEditorFor mutates the map.
    std::vector<int> ids;
    for (auto& [id, w] : windows)
        ids.push_back (id);
    for (int id : ids)
        closeEditorFor (id);
}

void PluginWindowManager::forgetSlot (int slotId)
{
    closeEditorFor (slotId);
    lastPositions.erase (slotId);
}

bool PluginWindowManager::isOpen (int slotId) const
{
    return windows.find (slotId) != windows.end();
}

juce::Point<int> PluginWindowManager::getPosition (int slotId) const
{
    if (auto it = windows.find (slotId); it != windows.end())
        return it->second->getPosition();

    if (auto it = lastPositions.find (slotId); it != lastPositions.end())
        return it->second;

    // First window for this slot: cascade so several editors don't stack
    // exactly on top of each other.
    const int step = (int) (lastPositions.size() % 8);
    return { 120 + 30 * step, 120 + 30 * step };
}
