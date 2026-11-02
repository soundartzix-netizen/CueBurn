#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// SoundArtz プラグイン共通のUI部品 (配色・ノブ・ヘルプ付きラベル・ツールチップ)

namespace ui
{
inline const juce::Colour bgTop (0xff0e1117);
inline const juce::Colour bgBottom (0xff171b25);
inline const juce::Colour panelCol (0xff1a1e28);
inline const juce::Colour panelEdge (0xff272c39);
inline const juce::Colour textCol (0xffdfe3ea);
inline const juce::Colour mutedCol (0xff7c8496);
inline const juce::Colour teal (0xff36d6b5);
inline const juce::Colour orange (0xffff9f43);
inline const juce::Colour blue (0xff5aa9ff);
inline const juce::Colour violet (0xffa596ff);

juce::Font fontOf (float size, bool bold = false, float kerning = 0.0f);
void setFontOverride (juce::Font (*fn) (float size, bool bold)); // 見た目を変える時に、全体の文字を差し替える (nullptr で元に戻す)
juce::String u8 (const char* utf8);
bool hasWide (const juce::String& text); // 日本語など非ASCII文字を含むか
}

struct LocEntry
{
    const char* id;
    const char* enLabel;
    const char* jaLabel;
    const char* enHelp;
    const char* jaHelp;
};

class PluginLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PluginLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
                           juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool isDown, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool isOver, bool isDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;

    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
};

// 「ラベル + ?マーク」。カーソルを合わせると説明(ツールチップ)が出る
class HelpLabel : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setLabel (const juce::String& newText, juce::Justification j);
    void paint (juce::Graphics&) override;

    // 見た目ごとの色 (全部のラベルで共通)。showIcon = false で ? の印を出さない
    static inline juce::Colour idleColour = ui::mutedCol, hoverColour = ui::textCol, iconHover = ui::teal;
    static inline bool showIcon = true;

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    juce::String text;
    juce::Justification just { juce::Justification::centred };
    bool hover = false;
};
