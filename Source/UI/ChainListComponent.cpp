#include "ChainListComponent.h"

void ChainListComponent::Rows::resized()
{
    auto area = getLocalBounds();
    for (auto& row : rows)
    {
        row->setBounds (area.removeFromTop (SlotComponent::rowHeight));
        area.removeFromTop (rowGap);
    }
}

ChainListComponent::ChainListComponent (AudioEngine& e, PluginHostManager& p, PluginWindowManager& w)
    : engine (e), plugins (p), windows (w)
{
    viewport.setViewedComponent (&rows, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    addButton.onClick = [this] { if (onAddRequested) onAddRequested(); };
    addAndMakeVisible (addButton);

    rebuild();
}

void ChainListComponent::resized()
{
    auto area = getLocalBounds();
    addButton.setBounds (area.removeFromBottom (SlotComponent::rowHeight).withTrimmedLeft (56));
    area.removeFromBottom (rowGap);
    viewport.setBounds (area);
    layoutRows();
}

void ChainListComponent::layoutRows()
{
    const int n = (int) rows.rows.size();
    const int height = n > 0 ? n * (SlotComponent::rowHeight + rowGap) - rowGap : 0;

    // Leave room for the scrollbar only when the rows overflow.
    const bool needsBar = height > viewport.getHeight();
    const int width = viewport.getWidth() - (needsBar ? viewport.getScrollBarThickness() : 0);

    rows.setSize (juce::jmax (0, width), height);
}

void ChainListComponent::rebuild()
{
    rows.rows.clear();

    for (int i = 0; i < engine.chain.getNumSlots(); ++i)
    {
        auto* slot = engine.chain.getSlot (i);
        auto row = std::make_unique<SlotComponent> (slot->id, engine, plugins, windows);
        row->onLoadRequested   = [this] (int id, const juce::PluginDescription& d) { if (onLoadRequested) onLoadRequested (id, d); };
        row->onClearRequested  = [this] (int id)            { if (onClearRequested) onClearRequested (id); };
        row->onDeleteRequested = [this] (int id)            { if (onDeleteRequested) onDeleteRequested (id); };
        row->onMoveRequested   = [this] (int id, int delta) { if (onMoveRequested) onMoveRequested (id, delta); };
        rows.addAndMakeVisible (*row);
        rows.rows.push_back (std::move (row));
    }

    layoutRows();
    rows.resized();   // setSize() only triggers resized() when the size changes
}

SlotComponent* ChainListComponent::findRow (int slotId) const
{
    for (auto& row : rows.rows)
        if (row->getSlotId() == slotId)
            return row.get();
    return nullptr;
}

void ChainListComponent::refreshRow (int slotId)
{
    if (auto* row = findRow (slotId))
        row->refresh();
}

void ChainListComponent::refreshAll()
{
    for (auto& row : rows.rows)
        row->refresh();
}

void ChainListComponent::scrollToBottom()
{
    viewport.setViewPosition (0, juce::jmax (0, rows.getHeight() - viewport.getViewHeight()));
}
