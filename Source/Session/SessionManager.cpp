#include "SessionManager.h"

static constexpr int sessionFormatVersion = 1;

namespace
{
    const char* inputModeToString (InputMode m)
    {
        switch (m)
        {
            case InputMode::monoRight: return "monoRight";
            case InputMode::stereo:    return "stereo";
            case InputMode::monoLeft:
            default:                   return "monoLeft";
        }
    }

    InputMode inputModeFromString (const juce::String& s)
    {
        if (s == "monoRight") return InputMode::monoRight;
        if (s == "stereo")    return InputMode::stereo;
        return InputMode::monoLeft;
    }
}

SessionManager::SessionManager (AudioEngine& e, PluginHostManager& p, PluginWindowManager& w)
    : engine (e), plugins (p), windows (w)
{
    lastSessionDirectory = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
}

SessionManager::~SessionManager() = default;

//==============================================================================
juce::Result SessionManager::saveToFile (const juce::File& file)
{
    juce::XmlElement root ("VsterAudioSession");
    root.setAttribute ("formatVersion", sessionFormatVersion);
    root.setAttribute ("appVersion", JUCE_APPLICATION_VERSION_STRING);

    auto* transport = root.createNewChildElement ("Transport");
    transport->setAttribute ("bpm", engine.getBpm());
    transport->setAttribute ("playing", engine.isTransportPlaying());

    auto* input = root.createNewChildElement ("Input");
    input->setAttribute ("mode", inputModeToString (engine.getInputMode()));
    input->setAttribute ("gainDb", engine.getInputGainDb());

    auto* master = root.createNewChildElement ("Master");
    master->setAttribute ("gainDb", engine.getMasterGainDb());
    master->setAttribute ("muted", engine.isMuted());

    auto* slots = root.createNewChildElement ("Slots");
    for (int i = 0; i < engine.chain.getNumSlots(); ++i)
    {
        auto* chainSlot = engine.chain.getSlot (i);
        auto* slot = slots->createNewChildElement ("Slot");
        slot->setAttribute ("bypassed", chainSlot->bypassed.load (std::memory_order_relaxed));

        if (auto* plugin = chainSlot->plugin.get())
        {
            slot->addChildElement (plugin->description.createXml().release());

            juce::MemoryBlock state;
            plugin->instance->getStateInformation (state);
            slot->createNewChildElement ("State")->addTextElement (state.toBase64Encoding());

            auto* editor = slot->createNewChildElement ("Editor");
            editor->setAttribute ("open", windows.isOpen (chainSlot->id));
            const auto pos = windows.getPosition (chainSlot->id);
            editor->setAttribute ("x", pos.x);
            editor->setAttribute ("y", pos.y);
        }
        else if (auto* m = chainSlot->missing.get())
        {
            // Plugin was missing on load: write its record back untouched so
            // nothing is lost.
            slot->setAttribute ("missing", true);
            if (m->descriptionXml != nullptr)
                slot->addChildElement (new juce::XmlElement (*m->descriptionXml));
            slot->createNewChildElement ("State")->addTextElement (m->state.toBase64Encoding());
        }
        // An empty slot is saved as an empty <Slot/> so the layout survives.
    }

    if (! file.getParentDirectory().createDirectory().wasOk())
        return juce::Result::fail ("Could not create " + file.getParentDirectory().getFullPathName());

    if (! root.writeTo (file))
        return juce::Result::fail ("Could not write " + file.getFullPathName());

    return juce::Result::ok();
}

//==============================================================================
void SessionManager::loadFromFile (const juce::File& file,
                                   std::function<void (const juce::String&)> onFinished)
{
    if (isLoading())
    {
        if (onFinished) onFinished ("A session is already loading.");
        return;
    }

    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("VsterAudioSession"))
    {
        if (onFinished) onFinished ("Not a valid VsterAudio session file.");
        return;
    }
    if (xml->getIntAttribute ("formatVersion", 1) > sessionFormatVersion)
    {
        if (onFinished) onFinished ("This session was saved by a newer version of VsterAudio.");
        return;
    }

    // From here on the load really happens, so plain Save targets this file.
    // Autosave recovery resets this afterwards (see offerAutosaveRecovery).
    currentSessionFile = file;

    if (auto* t = xml->getChildByName ("Transport"))
    {
        engine.setBpm (t->getDoubleAttribute ("bpm", 120.0));
        engine.setPlaying (t->getBoolAttribute ("playing", false));
    }

    if (auto* in = xml->getChildByName ("Input"))
    {
        engine.setInputMode (inputModeFromString (in->getStringAttribute ("mode")));
        engine.setInputGainDb ((float) in->getDoubleAttribute ("gainDb", 0.0));
    }

    savedMuteState = false;
    if (auto* m = xml->getChildByName ("Master"))
    {
        engine.setMasterGainDb ((float) m->getDoubleAttribute ("gainDb", 0.0));
        savedMuteState = m->getBoolAttribute ("muted", false);
    }

    // Keep the output quiet while plugins come and go.
    engine.setMuted (true);

    windows.closeAll();
    engine.removeAllSlots();

    std::vector<std::shared_ptr<SlotJob>> jobs;
    if (auto* slots = xml->getChildByName ("Slots"))
    {
        for (auto* slotXml : slots->getChildWithTagNameIterator ("Slot"))
        {
            // Slots are recreated in file order, so the chain layout is kept
            // even where a plugin is missing or the slot is empty.
            const int id = engine.addSlot();
            engine.setBypassed (id, slotXml->getBoolAttribute ("bypassed", false));

            auto* descXml = slotXml->getChildByName ("PLUGIN");
            if (descXml == nullptr)
                continue;   // empty slot

            auto job = std::make_shared<SlotJob>();
            job->slotId = id;
            job->descriptionXml = std::make_unique<juce::XmlElement> (*descXml);
            if (auto* state = slotXml->getChildByName ("State"))
                job->state.fromBase64Encoding (state->getAllSubText().trim());
            if (auto* editor = slotXml->getChildByName ("Editor"))
            {
                job->editorOpen = editor->getBoolAttribute ("open");
                job->editorPos = { editor->getIntAttribute ("x", 120), editor->getIntAttribute ("y", 120) };
            }
            jobs.push_back (std::move (job));
        }
    }

    missingNames.clear();
    loadFinishedCallback = std::move (onFinished);
    pendingLoads = (int) jobs.size();

    if (pendingLoads == 0)
    {
        engine.setMuted (savedMuteState);
        if (onSessionChanged) onSessionChanged();
        if (loadFinishedCallback) { loadFinishedCallback ({}); loadFinishedCallback = nullptr; }
        return;
    }

    // Show the (still empty) slots right away; rows fill in as plugins load.
    if (onSessionChanged) onSessionChanged();

    for (auto& job : jobs)
    {
        juce::PluginDescription saved;
        juce::Array<juce::PluginDescription> candidates;

        if (saved.loadFromXml (*job->descriptionXml))
        {
            candidates.add (saved);
            // Fallback: same plugin known under a different path (moved folder).
            for (const auto& known : plugins.knownPlugins.getTypes())
                if (known.uniqueId == saved.uniqueId
                    && known.fileOrIdentifier != saved.fileOrIdentifier)
                    candidates.add (known);
        }

        attemptLoad (job, candidates, 0);
    }
}

void SessionManager::attemptLoad (std::shared_ptr<SlotJob> job,
                                  juce::Array<juce::PluginDescription> candidates, int index)
{
    if (index >= candidates.size())
    {
        markMissing (*job);
        finishOne();
        return;
    }

    const auto desc = candidates[index];
    plugins.createInstance (desc, engine.getCurrentSampleRate(), engine.getCurrentBlockSize(),
        [this, job, candidates, index, desc] (std::unique_ptr<juce::AudioPluginInstance> instance,
                                              const juce::String&)
        {
            if (instance == nullptr)
            {
                attemptLoad (job, candidates, index + 1);
                return;
            }

            const auto error = engine.setPluginInSlot (job->slotId, std::move (instance), desc, &job->state);
            if (error.isNotEmpty())
            {
                markMissing (*job);
            }
            else if (job->editorOpen)
            {
                // Editors that were open when the session was saved reopen
                // where they were, so an amp sim is back on screen at once.
                if (auto* loaded = engine.chain.getInstance (job->slotId))
                    windows.showEditorFor (job->slotId, *loaded, job->editorPos);
            }
            finishOne();
        });
}

void SessionManager::markMissing (SlotJob& job)
{
    auto m = std::make_unique<MissingPlugin>();
    juce::PluginDescription desc;
    if (job.descriptionXml != nullptr && desc.loadFromXml (*job.descriptionXml))
    {
        m->name = desc.name;
        m->uniqueId = desc.uniqueId;
    }
    m->descriptionXml = std::move (job.descriptionXml);
    m->state = std::move (job.state);
    missingNames.add (m->name.isNotEmpty() ? m->name : "(unknown)");
    engine.setMissing (job.slotId, std::move (m));
}

void SessionManager::finishOne()
{
    jassert (pendingLoads > 0);
    if (--pendingLoads > 0)
        return;

    engine.setMuted (savedMuteState);
    if (onSessionChanged)
        onSessionChanged();

    juce::String error;
    if (! missingNames.isEmpty())
        error = "Some plugins could not be found: " + missingNames.joinIntoString (", ")
                + "\n\nTheir saved settings are preserved and will be written back on the next save.";

    if (loadFinishedCallback)
    {
        loadFinishedCallback (error);
        loadFinishedCallback = nullptr;
    }
    else if (error.isNotEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Missing plugins", error);
    }
}

//==============================================================================
void SessionManager::newInteractive()
{
    if (isLoading())
        return;

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
        "New session",
        "Discard the current session and start a new one?",
        "New", "Cancel", nullptr,
        juce::ModalCallbackFunction::create ([this] (int result)
        {
            if (result != 1)
                return;

            windows.closeAll();
            engine.removeAllSlots();

            currentSessionFile = juce::File();
            engine.setBpm (120.0);
            engine.setPlaying (false);
            engine.setInputMode (InputMode::monoLeft);
            engine.setInputGainDb (0.0f);
            engine.setMasterGainDb (0.0f);
            engine.setMuted (false);

            if (onSessionChanged)
                onSessionChanged();
        }));
}

void SessionManager::saveInteractive()
{
    if (currentSessionFile == juce::File())
    {
        saveAsInteractive();
        return;
    }

    const auto result = saveToFile (currentSessionFile);
    if (result.failed())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Save failed", result.getErrorMessage());
}

void SessionManager::saveAsInteractive()
{
    const auto suggested = currentSessionFile != juce::File()
                               ? currentSessionFile
                               : lastSessionDirectory.getChildFile (juce::String ("session") + fileExtension);

    chooser = std::make_unique<juce::FileChooser> ("Save session", suggested, juce::String ("*") + fileExtension);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::canSelectFiles
                          | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;
            if (file.getFileExtension().isEmpty())
                file = file.withFileExtension (fileExtension);
            lastSessionDirectory = file.getParentDirectory();

            const auto result = saveToFile (file);
            if (result.failed())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Save failed", result.getErrorMessage());
                return;
            }

            currentSessionFile = file;   // plain Save overwrites this from now on
        });
}

void SessionManager::loadInteractive()
{
    if (isLoading())
        return;

    chooser = std::make_unique<juce::FileChooser> ("Load session", lastSessionDirectory,
                                                   juce::String ("*") + fileExtension);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;
            lastSessionDirectory = file.getParentDirectory();

            loadFromFile (file, [] (const juce::String& error)
            {
                if (error.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                            "Session load", error);
            });
        });
}

//==============================================================================
juce::File SessionManager::getAutosaveFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("VsterAudio").getChildFile (juce::String ("autosave") + fileExtension);
}

void SessionManager::autosaveNow()
{
    if (isLoading())
        return;

    bool anythingLoaded = false;
    for (int i = 0; i < engine.chain.getNumSlots(); ++i)
        if (auto* s = engine.chain.getSlot (i))
            if (s->plugin != nullptr || s->missing != nullptr)
                anythingLoaded = true;

    if (anythingLoaded)
        saveToFile (getAutosaveFile());
}

void SessionManager::offerAutosaveRecovery()
{
    if (! getAutosaveFile().existsAsFile())
        return;

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
        "Restore session?",
        "An autosaved session from the last run was found. Restore it?",
        "Restore", "Ignore", nullptr,
        juce::ModalCallbackFunction::create ([this] (int result)
        {
            if (result == 1)
                restoreAutosave();
        }));
}

void SessionManager::restoreAutosave()
{
    if (! getAutosaveFile().existsAsFile())
        return;

    loadFromFile (getAutosaveFile(), [] (const juce::String& error)
    {
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Session restore", error);
    });

    // A recovered session has no user-chosen file; don't let a plain Save
    // silently overwrite the autosave slot.
    currentSessionFile = juce::File();
}
