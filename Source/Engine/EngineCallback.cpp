#include "EngineCallback.h"

void EngineCallback::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate = device->getCurrentSampleRate();
    const int block = device->getCurrentBufferSizeSamples();

    work.setSize (2, juce::jmax (block, 16));
    midi.ensureSize (2048);

    playHead.setSampleRate (sampleRate);
    midiCollector.reset (sampleRate);
    chain.prepareAll (sampleRate, block);
    currentGain = targetGain.load (std::memory_order_relaxed);
    currentInputGain = targetInputGain.load (std::memory_order_relaxed);
}

void EngineCallback::audioDeviceStopped()
{
    chain.releaseAll();
}

float EngineCallback::applyRampedGain (juce::AudioBuffer<float>& buffer, float current, float target,
                                       int numSamples) const
{
    if (juce::approximatelyEqual (current, target))
    {
        buffer.applyGain (target);
        return target;
    }

    const float maxStep = (float) (numSamples / (0.02 * sampleRate));
    const float next = current + juce::jlimit (-maxStep, maxStep, target - current);
    buffer.applyGainRamp (0, numSamples, current, next);
    return next;
}

void EngineCallback::audioDeviceIOCallbackWithContext (const float* const* in, int numIn,
                                                       float* const* out, int numOut,
                                                       int numSamples,
                                                       const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    if (numOut <= 0)
        return;

    // A block larger than the device announced shouldn't happen; keep silence.
    if (numSamples > work.getNumSamples())
    {
        for (int ch = 0; ch < numOut; ++ch)
            if (out[ch] != nullptr)
                juce::FloatVectorOperations::clear (out[ch], numSamples);
        return;
    }

    juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), 2, numSamples);

    // ---- input: enabled device channels -> stereo chain input ---------------
    const float* left = nullptr;
    const float* right = nullptr;

    if (numIn > 0 && in[0] != nullptr)
    {
        const float* first  = in[0];
        const float* second = (numIn > 1 && in[1] != nullptr) ? in[1] : in[0];

        switch ((InputMode) inputMode.load (std::memory_order_relaxed))
        {
            case InputMode::monoLeft:  left = right = first;  break;
            case InputMode::monoRight: left = right = second; break;
            case InputMode::stereo:    left = first; right = second; break;
        }
    }

    if (left != nullptr && right != nullptr)
    {
        juce::FloatVectorOperations::copy (view.getWritePointer (0), left,  numSamples);
        juce::FloatVectorOperations::copy (view.getWritePointer (1), right, numSamples);
    }
    else
    {
        view.clear();
    }

    currentInputGain = applyRampedGain (view, currentInputGain,
                                        targetInputGain.load (std::memory_order_relaxed), numSamples);
    inputMeter.push (view, numSamples);

    // ---- effects ------------------------------------------------------------
    midi.clear();
    midiCollector.removeNextBlockOfMessages (midi, numSamples);

    chain.process (view, midi);

    // ---- master -------------------------------------------------------------
    currentGain = applyRampedGain (view, currentGain,
                                   targetGain.load (std::memory_order_relaxed), numSamples);
    outputMeter.push (view, numSamples);
    playHead.advance (numSamples);

    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch] != nullptr)
            juce::FloatVectorOperations::copy (out[ch], view.getReadPointer (juce::jmin (ch, 1)), numSamples);
}
