#include "AudioEngine.h"

AudioEngine::AudioEngine()
{
    // MidiMessageCollector asserts if used before reset; the real rate is set
    // again in audioDeviceAboutToStart.
    midiCollector.reset (44100.0);
    updateGain();
    callback.setInputMode (inputMode);
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

void AudioEngine::initialise (const juce::XmlElement* savedDeviceState)
{
    deviceManager.initialise (2, 2, savedDeviceState, true);

    // Enable every hardware MIDI input on every launch, not just the first:
    // on Windows a device's identifier changes with the USB port, so relying
    // on the saved device state silently leaves a reconnected controller off.
    for (const auto& d : juce::MidiInput::getAvailableDevices())
    {
        knownMidiInputs.add (d.identifier);
        deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
    }

    // Hot-plug: a controller connected while the app is running gets enabled
    // too. Only genuinely new devices, so a device the user disabled in the
    // settings dialog stays off for the rest of the session.
    midiListConnection = juce::MidiDeviceListConnection::make ([this] { enableNewMidiInputs(); });

    // Empty identifier = receive from all enabled MIDI inputs.
    deviceManager.addMidiInputDeviceCallback ({}, &midiCollector);
    deviceManager.addMidiInputDeviceCallback ({}, &midiActivity);
    deviceManager.addAudioCallback (&callback);
    initialised = true;
}

void AudioEngine::shutdown()
{
    if (initialised)
    {
        midiListConnection = {};
        deviceManager.removeAudioCallback (&callback);
        deviceManager.removeMidiInputDeviceCallback ({}, &midiActivity);
        deviceManager.removeMidiInputDeviceCallback ({}, &midiCollector);
        initialised = false;
    }

    removeAllSlots();
}

void AudioEngine::retire (std::unique_ptr<LoadedPlugin>& old)
{
    if (old != nullptr && old->instance != nullptr)
    {
        old->instance->setPlayHead (nullptr);
        old->instance->releaseResources();
    }
    old.reset();   // destroyed on the message thread, outside the chain lock
}

juce::String AudioEngine::setPluginInSlot (int id,
                                           std::unique_ptr<juce::AudioPluginInstance> instance,
                                           const juce::PluginDescription& desc,
                                           const juce::MemoryBlock* initialState)
{
    auto* slot = chain.getSlotById (id);
    if (slot == nullptr)
        return "The slot no longer exists.";

    if (desc.isInstrument)
        return desc.name + " is an instrument. VsterAudio hosts effects only.";

    juce::String error;
    auto loaded = chain.configure (std::move (instance), desc, error);
    if (loaded == nullptr)
        return error;

    if (initialState != nullptr && initialState->getSize() > 0)
        loaded->instance->setStateInformation (initialState->getData(), (int) initialState->getSize());

    slot->missing.reset();

    auto old = chain.exchangePlugin (id, std::move (loaded));
    retire (old);
    return {};
}

void AudioEngine::clearSlotPlugin (int id)
{
    if (auto* slot = chain.getSlotById (id))
        slot->missing.reset();

    auto old = chain.exchangePlugin (id, nullptr);
    retire (old);
}

void AudioEngine::setMissing (int id, std::unique_ptr<MissingPlugin> m)
{
    if (auto* slot = chain.getSlotById (id))
        slot->missing = std::move (m);
}

void AudioEngine::removeSlot (int id)
{
    auto slot = chain.removeSlot (id);
    if (slot != nullptr)
        retire (slot->plugin);
}

void AudioEngine::removeAllSlots()
{
    std::vector<std::unique_ptr<ChainSlot>> removed;
    chain.clearAllSlots (removed);
    for (auto& s : removed)
        retire (s->plugin);
}

int AudioEngine::getNumActiveInputChannels() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getActiveInputChannels().countNumberOfSetBits();
    return 0;
}

void AudioEngine::enableNewMidiInputs()
{
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        if (! knownMidiInputs.contains (d.identifier))
        {
            knownMidiInputs.add (d.identifier);
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
        }
}

void AudioEngine::updateGain()
{
    const float gain = muted ? 0.0f : juce::Decibels::decibelsToGain (userGainDb, -60.0f);
    callback.setTargetGain (gain);
    callback.setTargetInputGain (juce::Decibels::decibelsToGain (inputGainDb, -60.0f));
}
