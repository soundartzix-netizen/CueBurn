#pragma once

#include "UiKit.h"

// CDJ (DJ 用プレーヤー) をモチーフにした、精密機械らしい見た目の部品。Cue Burn 専用 (共通の UiKit は変えない)
// ガンメタルの筐体・削り出しの金属・刻印の目盛り・ゴムのパッドと LED・液晶のガラス・光る線
namespace deck
{
inline const juce::Colour chassisTop (0xff262a31);
inline const juce::Colour chassisBottom (0xff0f1114);
inline const juce::Colour lcdBlue (0xff7fe6ff);   // 液晶の文字・波形
inline const juce::Colour ledAmber (0xffffa53a);  // 点いている LED
inline const juce::Colour ledCyan (0xff38d9ff);   // 待機中のリング

void drawChassis (juce::Graphics&, juce::Rectangle<float>);                              // 背景 (筐体)
void drawMetalPanel (juce::Graphics&, juce::Rectangle<float>, juce::Colour accent, const juce::String& title, bool ja);
void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius, float angle);
void drawLcd (juce::Graphics&, juce::Rectangle<float>, float corner = 6.0f);              // 液晶のガラス (中身を描く前に)
void drawLcdGloss (juce::Graphics&, juce::Rectangle<float>, float corner = 6.0f);         // 液晶の映り込み (中身を描いた後に)
void drawPad (juce::Graphics&, juce::Rectangle<float>, bool lit, bool hover, bool down, juce::Colour led = ledAmber);
void drawLed (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour, bool on);
void drawChromeText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float size, juce::Colour glow); // クロームの文字
}

class DeckLookAndFeel : public PluginLookAndFeel
{
public:
    DeckLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool isOver, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isOver, bool isDown) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool isDown, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override; // ノブの数値は、液晶の窓に
};
