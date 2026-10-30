#pragma once
#include <JuceHeader.h>
#include "PluginChain.h"
#include "HostPlayHead.h"
#include "LevelMeterSource.h"
#include "EngineCallback.h"

// Counts incoming hardware MIDI messages so the UI can show an activity
// lamp. handleIncomingMidiMessage runs on the system MIDI thread.
struct MidiActivityMonitor : public juce::MidiInputCallback
{
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
    {
        if (! m.isActiveSense() && ! m.isMidiClock())
            count.fetch_add (1, std::memory_order_relaxed);
    }

    std::atomic<uint32_t> count { 0 };
};

// Owns the audio device, the chain and everything audible. All plugin
// lifecycle methods must be called on the message thread.
class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    void initialise (const juce::XmlElement* savedDeviceState);
    void shutdown();   // stop audio + remove every slot; safe to call twice

    // Transport / input / master ---------------------------------------------
    void setBpm (double b)             { playHead.setBpm (b); }
    double getBpm() const              { return playHead.getBpm(); }
    void setPlaying (bool p)           { playHead.setPlaying (p); }
    bool isTransportPlaying() const    { return playHead.isPlaying(); }
    void setInputMode (InputMode m)    { inputMode = m; callback.setInputMode (m); }
    InputMode getInputMode() const     { return inputMode; }
    void setInputGainDb (float db)     { inputGainDb = db; updateGain(); }
    float getInputGainDb() const       { return inputGainDb; }
    void setMasterGainDb (float db)    { userGainDb = db; updateGain(); }
    float getMasterGainDb() const      { return userGainDb; }
    void setMuted (bool m)             { muted = m; updateGain(); }
    bool isMuted() const               { return muted; }

    // Slots (message thread) --------------------------------------------------
    int addSlot()                      { return chain.addSlot(); }
    void removeSlot (int id);          // destroys the plugin (its editor must be closed first)
    void removeAllSlots();
    bool moveSlot (int id, int delta)  { return chain.moveSlot (id, delta); }
    void setBypassed (int id, bool b)  { chain.setBypassed (id, b); }

    // Takes ownership; applies initialState (if given) before the plugin is
    // audible. Returns an error message, or an empty string on success.
    juce::String setPluginInSlot (int id,
                                  std::unique_ptr<juce::AudioPluginInstance>,
                                  const juce::PluginDescription&,
                                  const juce::MemoryBlock* initialState = nullptr);
    void clearSlotPlugin (int id);     // the slot stays, empty; discards any missing-plugin record
    void setMissing (int id, std::unique_ptr<MissingPlugin>);

    int getTotalLatencySamples() const { return chain.getTotalLatencySamples(); }
    double getCurrentSampleRate() const { return chain.isPrepared() ? chain.getSampleRate() : 44100.0; }
    int getCurrentBlockSize() const     { return chain.isPrepared() ? chain.getBlockSize() : 512; }
    int getNumActiveInputChannels() const;

    juce::AudioDeviceManager deviceManager;
    HostPlayHead playHead;
    PluginChain chain { playHead };
    juce::MidiMessageCollector midiCollector;
    MidiActivityMonitor midiActivity;
    LevelMeterSource inputMeter;
    LevelMeterSource outputMeter;

private:
    static void retire (std::unique_ptr<LoadedPlugin>&);
    void updateGain();
    void enableNewMidiInputs();

    EngineCallback callback { chain, playHead, midiCollector, inputMeter, outputMeter };
    juce::MidiDeviceListConnection midiListConnection;
    juce::StringArray knownMidiInputs;
    InputMode inputMode = InputMode::monoLeft;
    float inputGainDb = 0.0f;
    float userGainDb = 0.0f;
    bool muted = false;
    bool initialised = false;

    JUCE_DECLARE_NON_COPYABLE (AudioEngine)
};
