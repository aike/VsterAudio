#include "MainComponent.h"

MainComponent::MainComponent (AudioEngine& e, PluginHostManager& p, PluginWindowManager& w, SessionManager& s)
    : engine (e), plugins (p), windows (w), session (s)
{
    addAndMakeVisible (transport);

    chainList.onLoadRequested   = [this] (int id, const juce::PluginDescription& d) { loadPluginIntoSlot (id, d); };
    chainList.onClearRequested  = [this] (int id)            { clearSlot (id); };
    chainList.onDeleteRequested = [this] (int id)            { deleteSlot (id); };
    chainList.onMoveRequested   = [this] (int id, int delta) { moveSlot (id, delta); };
    chainList.onAddRequested    = [this]                     { addSlot(); };
    addAndMakeVisible (chainList);

    // ---- input strip ---------------------------------------------------------
    inputLabel.setJustificationType (juce::Justification::centred);
    inputLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (inputLabel);

    inputModeBox.addItem ("Mono L", (int) InputMode::monoLeft + 1);
    inputModeBox.addItem ("Mono R", (int) InputMode::monoRight + 1);
    inputModeBox.addItem ("Stereo", (int) InputMode::stereo + 1);
    inputModeBox.setSelectedId ((int) engine.getInputMode() + 1, juce::dontSendNotification);
    inputModeBox.setTooltip ("Which enabled input channel(s) feed the chain");
    inputModeBox.onChange = [this]
    {
        engine.setInputMode ((InputMode) (inputModeBox.getSelectedId() - 1));
    };
    addAndMakeVisible (inputModeBox);

    inputGainSlider.setSliderStyle (juce::Slider::LinearVertical);
    inputGainSlider.setRange (-24.0, 24.0, 0.1);
    inputGainSlider.setValue (engine.getInputGainDb(), juce::dontSendNotification);
    inputGainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);
    inputGainSlider.setTextValueSuffix (" dB");
    inputGainSlider.setDoubleClickReturnValue (true, 0.0);
    inputGainSlider.onValueChange = [this] { engine.setInputGainDb ((float) inputGainSlider.getValue()); };
    addAndMakeVisible (inputGainSlider);

    addAndMakeVisible (inputMeter);

    // ---- master strip --------------------------------------------------------
    volumeLabel.setJustificationType (juce::Justification::centred);
    volumeLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (volumeLabel);

    volumeSlider.setSliderStyle (juce::Slider::LinearVertical);
    volumeSlider.setRange (-60.0, 6.0, 0.1);
    volumeSlider.setValue (engine.getMasterGainDb(), juce::dontSendNotification);
    volumeSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);
    volumeSlider.setTextValueSuffix (" dB");
    volumeSlider.setDoubleClickReturnValue (true, 0.0);
    volumeSlider.onValueChange = [this] { engine.setMasterGainDb ((float) volumeSlider.getValue()); };
    addAndMakeVisible (volumeSlider);

    addAndMakeVisible (outputMeter);

    session.onSessionChanged = [this] { syncControls(); };

    // Ctrl+S anywhere in the main window triggers Save; the menu bar shows
    // the shortcut because it watches this command manager.
    commandManager.registerAllCommandsForTarget (this);
    addKeyListener (commandManager.getKeyMappings());
    setWantsKeyboardFocus (true);
    setApplicationCommandManagerToWatch (&commandManager);

    setSize (860, 300);   // 40 transport + strips / chain list
}

MainComponent::~MainComponent()
{
    session.onSessionChanged = nullptr;
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Option" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    if (topLevelMenuIndex == 0)
    {
        menu.addItem (menuFileNew,  "New");
        menu.addItem (menuFileLoad, "Load...");
        menu.addCommandItem (&commandManager, commandSave);   // "Save  Ctrl+S"
        menu.addItem (menuFileSaveAs, "Save As...");
    }
    else if (topLevelMenuIndex == 1)
    {
        menu.addItem (menuOptionAudioSettings, "Audio Settings...");
        menu.addItem (menuOptionPluginManager, "Plugin Manager...");
    }

    return menu;
}

void MainComponent::menuItemSelected (int menuItemID, int)
{
    switch (menuItemID)
    {
        case menuFileNew:            session.newInteractive(); break;
        case menuFileLoad:           session.loadInteractive(); break;
        case menuFileSaveAs:         session.saveAsInteractive(); break;
        // commandSave arrives via the command manager, not here.
        case menuOptionAudioSettings: showAudioSettings(); break;
        case menuOptionPluginManager:
            if (pluginManagerWindow == nullptr)
                pluginManagerWindow = std::make_unique<PluginManagerWindow> (plugins);
            pluginManagerWindow->setVisible (true);
            pluginManagerWindow->toFront (true);
            break;
        default: break;
    }
}

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.add (commandSave);
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    if (id == commandSave)
    {
        info.setInfo ("Save", "Saves the session to its file", "File", 0);
        info.addDefaultKeypress ('S', juce::ModifierKeys::commandModifier);
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    if (info.commandID == commandSave)
    {
        session.saveInteractive();
        return true;
    }
    return false;
}

void MainComponent::showAudioSettings()
{
    // Inputs are listed channel by channel (not as stereo pairs) so a single
    // interface input - the one the guitar is plugged into - can be enabled.
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        engine.deviceManager,
        1, 2,      // one or two audio inputs
        2, 2,      // stereo output
        true,      // MIDI input selection
        false,     // no MIDI output
        false,     // individual channels, not stereo pairs
        false);
    selector->setSize (520, 560);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio / MIDI Settings";
    options.componentToCentreAround = getTopLevelComponent();
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    transport.setBounds (area.removeFromTop (40));

    auto input = area.removeFromLeft (130).reduced (6);
    inputLabel.setBounds (input.removeFromTop (16));
    input.removeFromTop (2);
    inputModeBox.setBounds (input.removeFromTop (22));
    input.removeFromTop (4);
    inputMeter.setBounds (input.removeFromRight (36));
    input.removeFromRight (6);
    inputGainSlider.setBounds (input);

    auto master = area.removeFromRight (130).reduced (6);
    volumeLabel.setBounds (master.removeFromTop (16));
    outputMeter.setBounds (master.removeFromRight (36));
    master.removeFromRight (6);
    volumeSlider.setBounds (master);

    chainList.setBounds (area.reduced (0, 6));
}

void MainComponent::addSlot()
{
    if (session.isLoading())
        return;

    engine.addSlot();
    chainList.rebuild();
    chainList.resized();
    chainList.scrollToBottom();
}

void MainComponent::loadPluginIntoSlot (int slotId, const juce::PluginDescription& desc)
{
    if (session.isLoading())
        return;

    session.autosaveNow();   // crash insurance before instantiating a new plugin

    plugins.createInstance (desc, engine.getCurrentSampleRate(), engine.getCurrentBlockSize(),
        [this, slotId, desc] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (instance == nullptr)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Plugin load failed", desc.name + "\n\n" + error);
                return;
            }

            auto* slot = engine.chain.getSlotById (slotId);
            if (slot == nullptr)
                return;   // the slot was deleted while the plugin was being created

            windows.closeEditorFor (slotId);   // old editor must die before its plugin

            // If this slot held a missing plugin's saved state and the user
            // just loaded that same plugin, restore the state.
            const juce::MemoryBlock* pendingState = nullptr;
            if (auto* m = slot->missing.get())
                if (m->uniqueId == desc.uniqueId)
                    pendingState = &m->state;

            const auto loadError = engine.setPluginInSlot (slotId, std::move (instance), desc, pendingState);
            if (loadError.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Plugin load failed", loadError);
            else if (auto* loaded = engine.chain.getInstance (slotId))
                windows.showEditorFor (slotId, *loaded, windows.getPosition (slotId));

            chainList.refreshRow (slotId);
        });
}

void MainComponent::clearSlot (int slotId)
{
    if (session.isLoading())
        return;

    windows.closeEditorFor (slotId);
    engine.clearSlotPlugin (slotId);
    chainList.refreshRow (slotId);
}

void MainComponent::deleteSlot (int slotId)
{
    if (session.isLoading())
        return;

    windows.forgetSlot (slotId);
    engine.removeSlot (slotId);
    chainList.rebuild();
    chainList.resized();
}

void MainComponent::moveSlot (int slotId, int delta)
{
    if (session.isLoading())
        return;

    if (engine.moveSlot (slotId, delta))
    {
        chainList.rebuild();
        chainList.resized();
    }
}

void MainComponent::syncControls()
{
    transport.syncFromEngine();   // also syncs the MUTE toggle
    inputModeBox.setSelectedId ((int) engine.getInputMode() + 1, juce::dontSendNotification);
    inputGainSlider.setValue (engine.getInputGainDb(), juce::dontSendNotification);
    volumeSlider.setValue (engine.getMasterGainDb(), juce::dontSendNotification);
    chainList.rebuild();
    chainList.resized();
}
