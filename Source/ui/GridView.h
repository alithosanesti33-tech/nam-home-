#pragma once
#include "Look.h"
#include "../Engine.h"

class NAMHomeProcessor;

// The 4 x 10 signal-chain grid. Drag tiles to move them, drop captures/IRs/effects on cells,
// click "+" to add a block, double-click a tile to switch it on/off, right-click for more.
class GridView : public juce::Component,
                 public juce::DragAndDropTarget,
                 public juce::FileDragAndDropTarget
{
public:
    explicit GridView (NAMHomeProcessor& p);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverCell = { -1, -1 }; repaint(); }

    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override { dropCell = { -1, -1 }; repaint(); }
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragMove (const juce::StringArray&, int x, int y) override { dropCell = cellAt ({ x, y }); repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropCell = { -1, -1 }; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    juce::Point<int> selected { 0, 1 };              // x = column, y = row
    std::function<void (juce::Point<int>)> onSelect;  // cell clicked (block or empty)
    std::function<void (int row, juce::Rectangle<int> nodeArea)> onMergeClicked;
    std::function<void (const juce::String&, bool warning)> onMessage;
    std::function<void()> onChanged;

    void showAddMenu (juce::Point<int> cell);
    void showContextMenu (juce::Point<int> cell);
    void addBlockAt (Block::Type t, juce::Point<int> cell);
    void removeAt (juce::Point<int> cell);
    juce::Rectangle<float> cellBounds (int r, int c) const;

private:
    juce::Point<int> cellAt (juce::Point<int> p) const;
    int mergeNodeAt (juce::Point<int> p) const;
    float mergeX (int row) const;
    float splitX (int row) const;
    void drawTile (juce::Graphics&, Block& b, juce::Rectangle<float> r, bool isSelected);

    NAMHomeProcessor& proc;
    float tile = 90, gap = 16, rowPitch = 120, x0 = 60, y0 = 20;
    juce::Point<int> hoverCell { -1, -1 }, dropCell { -1, -1 }, pressCell { -1, -1 };
    bool dragStarted = false;
};
