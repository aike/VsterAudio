#include "SlotComponent.h"

SlotComponent::SlotComponent (int id, AudioEngine& e, PluginHostManager& p, PluginWindowManager& w)
    : slotId (id), engine (e), plugins (p), windows (w)
{
    indexLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    indexLabel.setJustificationType (juce::Justification::centred);

    nameButton.onClick = [this] { showMenu(); };

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::darkorange);
    bypassButton.setTooltip ("Bypass this effect");
    bypassButton.onClick = [this] { engine.setBypassed (slotId, bypassButton.getToggleState()); };

    uiButton.onClick = [this]
    {
        if (auto* instance = engine.chain.getInstance (slotId))
            windows.showEditorFor (slotId, *instance, windows.getPosition (slotId));
    };

    upButton.onClick   = [this] { if (onMoveRequested) onMoveRequested (slotId, -1); };
    downButton.onClick = [this] { if (onMoveRequested) onMoveRequested (slotId, +1); };

    deleteButton.setTooltip ("Remove this slot from the chain");
    deleteButton.onClick = [this] { if (onDeleteRequested) onDeleteRequested (slotId); };

    addAndMakeVisible (indexLabel);
    addAndMakeVisible (nameButton);
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (uiButton);
    addAndMakeVisible (upButton);
    addAndMakeVisible (downButton);
    addAndMakeVisible (deleteButton);
    refresh();
}

void SlotComponent::resized()
{
    auto area = getLocalBounds();
    indexLabel.setBounds (area.removeFromLeft (52));
    area.removeFromLeft (4);

    deleteButton.setBounds (area.removeFromRight (26));
    area.removeFromRight (6);
    downButton.setBounds (area.removeFromRight (18).reduced (0, 6));
    area.removeFromRight (4);
    upButton.setBounds (area.removeFromRight (18).reduced (0, 6));
    area.removeFromRight (6);
    uiButton.setBounds (area.removeFromRight (44));
    area.removeFromRight (4);
    bypassButton.setBounds (area.removeFromRight (44));
    area.removeFromRight (4);
    nameButton.setBounds (area);
}

void SlotComponent::refresh()
{
    const int index = engine.chain.indexOf (slotId);
    indexLabel.setText ("FX " + juce::String (index + 1), juce::dontSendNotification);

    const bool first = index <= 0;
    const bool last  = index >= engine.chain.getNumSlots() - 1;
    upButton.setEnabled (! first);
    downButton.setEnabled (! last);
    upButton.setAlpha (first ? 0.3f : 1.0f);
    downButton.setAlpha (last ? 0.3f : 1.0f);

    auto* slot = engine.chain.getSlotById (slotId);
    bypassButton.setToggleState (slot != nullptr && slot->bypassed.load (std::memory_order_relaxed),
                                 juce::dontSendNotification);

    if (auto* desc = engine.chain.getDescription (slotId))
    {
        nameButton.setButtonText (desc->name);
        uiButton.setEnabled (true);
        bypassButton.setEnabled (true);
    }
    else if (slot != nullptr && slot->missing != nullptr)
    {
        nameButton.setButtonText ("[!] Missing: " + slot->missing->name);
        uiButton.setEnabled (false);
        bypassButton.setEnabled (false);
    }
    else
    {
        nameButton.setButtonText ("(empty)");
        uiButton.setEnabled (false);
        bypassButton.setEnabled (false);
    }
}

void SlotComponent::showMenu()
{
    juce::PopupMenu menu;

    auto* slot = engine.chain.getSlotById (slotId);
    if (slot != nullptr && (slot->plugin != nullptr || slot->missing != nullptr))
    {
        menu.addItem (1, "Remove plugin");
        menu.addSeparator();
    }

    // Effects only: instruments can't be inserted into an audio chain.
    juce::Array<juce::PluginDescription> filtered;
    for (const auto& d : plugins.knownPlugins.getTypes())
        if (! d.isInstrument)
            filtered.add (d);

    if (filtered.isEmpty())
    {
        menu.addItem (2, "No effects found - use Option > Plugin Manager", false);
    }
    else
    {
        // Both views share the same array, so the menu IDs (base + array
        // index) resolve identically whichever submenu the pick came from.
        juce::PopupMenu byManufacturer, byCategory;
        juce::KnownPluginList::addToMenu (byManufacturer, filtered, juce::KnownPluginList::sortByManufacturer);
        juce::KnownPluginList::addToMenu (byCategory, filtered, juce::KnownPluginList::sortByCategory);
        menu.addSubMenu ("By manufacturer", byManufacturer);
        menu.addSubMenu ("By category", byCategory);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (nameButton),
        [this, filtered] (int result)
        {
            if (result == 0)
                return;
            if (result == 1) { if (onClearRequested) onClearRequested (slotId); return; }

            const int index = juce::KnownPluginList::getIndexChosenByMenu (filtered, result);
            if (index >= 0 && onLoadRequested)
                onLoadRequested (slotId, filtered.getReference (index));
        });
}
