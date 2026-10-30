#pragma once
#include <JuceHeader.h>
#include "PluginChain.h"
#include "HostPlayHead.h"
#include "LevelMeterSource.h"

// How the enabled audio input channels are mapped onto the stereo chain.
enum class InputMode
{
    monoLeft = 0,   // first enabled input -> both L and R (default: a guitar)
    monoRight,      // second enabled input -> both L and R (first if only one)
    stereo          // first two enabled inputs -> L / R (one input is duplicated)
};

// The real-time audio callback. Everything that runs on the device thread
// lives here so it stays auditable: input mapping and gain, MIDI gathering,
// the plugin chain, the click-free master gain and the meter capture.
class EngineCallback : public juce::AudioIODeviceCallback
{
public:
    EngineCallback (PluginChain& c, HostPlayHead& ph, juce::MidiMessageCollector& mc,
                    LevelMeterSource& in, LevelMeterSource& out)
        : chain (c), playHead (ph), midiCollector (mc), inputMeter (in), outputMeter (out) {}

    // Message thread; the ramps in the callback make the changes click-free.
    void setTargetGain (float linearGain)       { targetGain.store (linearGain, std::memory_order_relaxed); }
    void setTargetInputGain (float linearGain)  { targetInputGain.store (linearGain, std::memory_order_relaxed); }
    void setInputMode (InputMode m)             { inputMode.store ((int) m, std::memory_order_relaxed); }

    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;

private:
    // Constant-rate ramp towards `target`, full scale in 20 ms. Applies the
    // gain to `buffer` and returns the gain reached at the end of the block.
    float applyRampedGain (juce::AudioBuffer<float>& buffer, float current, float target, int numSamples) const;

    PluginChain& chain;
    HostPlayHead& playHead;
    juce::MidiMessageCollector& midiCollector;
    LevelMeterSource& inputMeter;
    LevelMeterSource& outputMeter;

    juce::AudioBuffer<float> work;   // stereo render buffer, sized at device start
    juce::MidiBuffer midi;
    std::atomic<float> targetGain { 1.0f };
    std::atomic<float> targetInputGain { 1.0f };
    std::atomic<int> inputMode { (int) InputMode::monoLeft };
    float currentGain = 1.0f;        // audio-thread state
    float currentInputGain = 1.0f;
    double sampleRate = 44100.0;
};
