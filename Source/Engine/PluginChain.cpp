#include "PluginChain.h"

namespace
{
    // Try one concrete layout: main buses as given, every aux bus disabled.
    bool tryLayout (juce::AudioPluginInstance& p,
                    const juce::AudioChannelSet& mainIn,
                    const juce::AudioChannelSet& mainOut)
    {
        auto layout = p.getBusesLayout();

        for (int i = 0; i < layout.inputBuses.size(); ++i)
            layout.inputBuses.getReference (i) = (i == 0 ? mainIn : juce::AudioChannelSet::disabled());
        for (int i = 0; i < layout.outputBuses.size(); ++i)
            layout.outputBuses.getReference (i) = (i == 0 ? mainOut : juce::AudioChannelSet::disabled());

        return p.checkBusesLayoutSupported (layout) && p.setBusesLayout (layout);
    }
}

std::unique_ptr<LoadedPlugin> PluginChain::configure (std::unique_ptr<juce::AudioPluginInstance> inst,
                                                      const juce::PluginDescription& desc,
                                                      juce::String& error)
{
    jassert (inst != nullptr);

    const auto stereo = juce::AudioChannelSet::stereo();
    const auto mono   = juce::AudioChannelSet::mono();

    const bool ok = tryLayout (*inst, stereo, stereo)
                 || tryLayout (*inst, mono, mono)
                 || tryLayout (*inst, mono, stereo)
                 || tryLayout (*inst, stereo, mono);

    if (! ok)
        inst->enableAllBuses();   // last resort: give the plugin its preferred layout

    const int totalIn  = inst->getTotalNumInputChannels();
    const int totalOut = inst->getTotalNumOutputChannels();

    if (totalOut <= 0)
    {
        error = desc.name + " has no audio output.";
        return nullptr;
    }
    if (totalIn <= 0)
    {
        error = desc.name + " has no audio input (is it an instrument?).";
        return nullptr;
    }
    if (totalIn > 32 || totalOut > 32)
    {
        error = desc.name + " uses an unsupported channel count ("
                + juce::String (totalIn) + " in / " + juce::String (totalOut) + " out).";
        return nullptr;
    }

    auto lp = std::make_unique<LoadedPlugin>();
    lp->description = desc;
    lp->totalIn     = totalIn;
    lp->totalOut    = totalOut;
    lp->direct      = (totalOut == 2 && totalIn == 2);
    lp->instance    = std::move (inst);
    lp->instance->setPlayHead (&playHead);

    if (prepared)
        preparePlugin (*lp, sampleRate, blockSize);

    return lp;
}

void PluginChain::preparePlugin (LoadedPlugin& lp, double sr, int block)
{
    if (! lp.direct)
        lp.scratch.setSize (juce::jmax (lp.totalIn, lp.totalOut, 1), block);

    lp.instance->setRateAndBufferSizeDetails (sr, block);
    lp.instance->prepareToPlay (sr, block);
}

//==============================================================================
int PluginChain::addSlot()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    auto slot = std::make_unique<ChainSlot>();
    slot->id = nextSlotId++;
    const int id = slot->id;

    // Reserve before locking so the swap-in never allocates under the lock.
    std::vector<std::unique_ptr<ChainSlot>> next;
    next.reserve (slots.size() + 1);

    {
        const juce::ScopedLock sl (lock);
        for (auto& s : slots)
            next.push_back (std::move (s));
        next.push_back (std::move (slot));
        slots.swap (next);
    }
    return id;   // `next` (the old, emptied vector) is freed here, outside the lock
}

std::unique_ptr<ChainSlot> PluginChain::removeSlot (int id)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    const int index = indexOf (id);
    if (index < 0)
        return nullptr;

    std::vector<std::unique_ptr<ChainSlot>> next;
    next.reserve (slots.size());
    std::unique_ptr<ChainSlot> removed;

    {
        const juce::ScopedLock sl (lock);
        for (int i = 0; i < (int) slots.size(); ++i)
        {
            if (i == index)
                removed = std::move (slots[(size_t) i]);
            else
                next.push_back (std::move (slots[(size_t) i]));
        }
        slots.swap (next);
    }
    return removed;
}

bool PluginChain::moveSlot (int id, int delta)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    const int from = indexOf (id);
    const int to = from + delta;
    if (from < 0 || ! juce::isPositiveAndBelow (to, (int) slots.size()))
        return false;

    const juce::ScopedLock sl (lock);
    std::swap (slots[(size_t) from], slots[(size_t) to]);
    return true;
}

void PluginChain::clearAllSlots (std::vector<std::unique_ptr<ChainSlot>>& removed)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    std::vector<std::unique_ptr<ChainSlot>> next;   // empty; nothing to allocate

    {
        const juce::ScopedLock sl (lock);
        slots.swap (next);
    }

    removed = std::move (next);
}

std::unique_ptr<LoadedPlugin> PluginChain::exchangePlugin (int id, std::unique_ptr<LoadedPlugin> newPlugin)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    auto* slot = getSlotById (id);
    if (slot == nullptr)
        return newPlugin;   // nothing to swap into; caller destroys it

    {
        const juce::ScopedLock sl (lock);
        std::swap (slot->plugin, newPlugin);
    }
    return newPlugin;   // the old plugin; destroyed by the caller outside the lock
}

void PluginChain::setBypassed (int id, bool shouldBypass)
{
    if (auto* slot = getSlotById (id))
        slot->bypassed.store (shouldBypass, std::memory_order_relaxed);
}

int PluginChain::indexOf (int id) const
{
    for (int i = 0; i < (int) slots.size(); ++i)
        if (slots[(size_t) i]->id == id)
            return i;
    return -1;
}

ChainSlot* PluginChain::getSlot (int index) const
{
    if (! juce::isPositiveAndBelow (index, (int) slots.size()))
        return nullptr;
    return slots[(size_t) index].get();
}

juce::AudioPluginInstance* PluginChain::getInstance (int id) const
{
    auto* s = getSlotById (id);
    return (s != nullptr && s->plugin != nullptr) ? s->plugin->instance.get() : nullptr;
}

const juce::PluginDescription* PluginChain::getDescription (int id) const
{
    auto* s = getSlotById (id);
    return (s != nullptr && s->plugin != nullptr) ? &s->plugin->description : nullptr;
}

int PluginChain::getTotalLatencySamples() const
{
    int total = 0;
    for (auto& s : slots)
        if (s->plugin != nullptr && s->plugin->instance != nullptr
            && ! s->bypassed.load (std::memory_order_relaxed))
            total += s->plugin->instance->getLatencySamples();
    return total;
}

//==============================================================================
void PluginChain::prepareAll (double sr, int block)
{
    const juce::ScopedLock sl (lock);
    sampleRate = sr;
    blockSize  = block;
    pluginMidi.ensureSize (2048);
    for (auto& s : slots)
        if (s->plugin != nullptr)
            preparePlugin (*s->plugin, sr, block);
    prepared = true;
}

void PluginChain::releaseAll()
{
    const juce::ScopedLock sl (lock);
    prepared = false;
    for (auto& s : slots)
        if (s->plugin != nullptr && s->plugin->instance != nullptr)
            s->plugin->instance->releaseResources();
}

void PluginChain::process (juce::AudioBuffer<float>& host, const juce::MidiBuffer& midi)
{
    const juce::ScopedLock sl (lock);

    if (! prepared)
        return;

    const int n = host.getNumSamples();

    for (auto& s : slots)
    {
        if (s->plugin == nullptr || s->bypassed.load (std::memory_order_relaxed))
            continue;

        // Fresh copy per plugin: processBlock may consume or emit MIDI.
        pluginMidi.clear();
        pluginMidi.addEvents (midi, 0, n, 0);
        processOne (*s->plugin, host, pluginMidi);
    }
}

void PluginChain::processOne (LoadedPlugin& lp, juce::AudioBuffer<float>& host, juce::MidiBuffer& midi)
{
    auto& p = *lp.instance;
    if (p.isSuspended())
        return;

    const int n = host.getNumSamples();

    if (lp.direct)
    {
        p.processBlock (host, midi);
        return;
    }

    if (lp.scratch.getNumSamples() < n)   // shouldn't happen; keep what we have
        return;

    const int chans = lp.scratch.getNumChannels();
    juce::AudioBuffer<float> view (lp.scratch.getArrayOfWritePointers(), chans, n);
    view.clear();

    if (lp.totalIn == 1)
    {
        view.copyFrom (0, 0, host, 0, 0, n);
        view.addFrom  (0, 0, host, 1, 0, n);
        view.applyGain (0, 0, n, 0.5f);
    }
    else if (lp.totalIn >= 2)
    {
        view.copyFrom (0, 0, host, 0, 0, n);
        view.copyFrom (1, 0, host, 1, 0, n);
    }

    p.processBlock (view, midi);

    if (lp.totalOut == 1)
    {
        host.copyFrom (0, 0, view, 0, 0, n);
        host.copyFrom (1, 0, view, 0, 0, n);
    }
    else
    {
        host.copyFrom (0, 0, view, 0, 0, n);
        host.copyFrom (1, 0, view, juce::jmin (1, chans - 1), 0, n);
    }
}
