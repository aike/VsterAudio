#pragma once
#include <JuceHeader.h>
#include <vector>
#include "HostPlayHead.h"

// A plugin fully configured and ready to be swapped into the chain.
struct LoadedPlugin
{
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::PluginDescription description;
    bool direct = true;          // can process straight into the host stereo buffer
    int totalIn = 2, totalOut = 2;
    juce::AudioBuffer<float> scratch;   // used when !direct, sized on the message thread
};

// Saved state for a slot whose plugin could not be found on session load.
// Kept verbatim so the next save never loses the user's settings.
struct MissingPlugin
{
    std::unique_ptr<juce::XmlElement> descriptionXml;
    juce::MemoryBlock state;
    juce::String name;
    int uniqueId = 0;
};

// One position in the serial effect chain. `id` never changes for the life
// of the slot, so the UI, the editor windows and the session loader refer to
// slots by id while their index moves around.
struct ChainSlot
{
    int id = 0;
    std::unique_ptr<LoadedPlugin> plugin;     // audio thread reads this under the chain lock
    std::unique_ptr<MissingPlugin> missing;   // message thread only
    std::atomic<bool> bypassed { false };
};

// Serial chain of any number of effect slots: the input signal enters slot 0
// and each loaded, non-bypassed plugin processes it in place.
// The audio thread only ever takes `lock` around process(); the message thread
// takes it for pointer swaps, so worst-case audio blocking is microseconds.
class PluginChain
{
public:
    explicit PluginChain (HostPlayHead& ph) : playHead (ph) {}

    // ---- message thread ----------------------------------------------------
    // Negotiates bus layouts, sets the play head, sizes the scratch buffer and
    // prepares the plugin if the device is running. Returns nullptr + error if
    // no workable layout exists.
    std::unique_ptr<LoadedPlugin> configure (std::unique_ptr<juce::AudioPluginInstance>,
                                             const juce::PluginDescription&,
                                             juce::String& error);

    // Slot structure. All of these swap pointers under the lock; the memory
    // they need is allocated before the lock is taken.
    int addSlot();                                       // appends an empty slot, returns its id
    std::unique_ptr<ChainSlot> removeSlot (int id);      // returns the slot for destruction outside the lock
    bool moveSlot (int id, int delta);                   // -1 = up, +1 = down; false if impossible
    void clearAllSlots (std::vector<std::unique_ptr<ChainSlot>>& removed);

    // Swaps the plugin of a slot under the lock and returns the previous one
    // so the caller can destroy it outside the lock, on the message thread.
    std::unique_ptr<LoadedPlugin> exchangePlugin (int id, std::unique_ptr<LoadedPlugin> newPlugin);

    void setBypassed (int id, bool shouldBypass);

    // Read-only access for the message thread (which is the only mutator, so
    // no lock is needed to read the structure there).
    int getNumSlots() const                    { return (int) slots.size(); }
    int indexOf (int id) const;
    ChainSlot* getSlot (int index) const;
    ChainSlot* getSlotById (int id) const      { return getSlot (indexOf (id)); }
    juce::AudioPluginInstance* getInstance (int id) const;
    const juce::PluginDescription* getDescription (int id) const;
    int getTotalLatencySamples() const;        // sum over loaded, non-bypassed plugins

    // ---- device lifecycle (audio is stopped while these run) ---------------
    void prepareAll (double sampleRate, int blockSize);
    void releaseAll();
    bool isPrepared() const    { return prepared; }
    double getSampleRate() const { return sampleRate; }
    int getBlockSize() const   { return blockSize; }

    // ---- audio thread -------------------------------------------------------
    // `stereo` must have exactly 2 channels and already contain the input
    // signal; every active plugin processes it in place. `midi` is handed to
    // every plugin (copied, so one plugin's MIDI output never leaks downstream).
    void process (juce::AudioBuffer<float>& stereo, const juce::MidiBuffer& midi);

private:
    void processOne (LoadedPlugin&, juce::AudioBuffer<float>& host, juce::MidiBuffer& midi);
    static void preparePlugin (LoadedPlugin&, double sampleRate, int blockSize);

    HostPlayHead& playHead;
    juce::CriticalSection lock;
    std::vector<std::unique_ptr<ChainSlot>> slots;
    juce::MidiBuffer pluginMidi;   // per-plugin copy of the incoming MIDI, pre-sized
    int nextSlotId = 1;
    double sampleRate = 0.0;
    int blockSize = 0;
    bool prepared = false;
};
