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

// 見た目: Deck = CDJ 風 (既定) / White = 白い筐体に黒い画面 (白い限定版の CDJ のような)。PC 全体で共通 (保存は設定フォルダ)
enum class Skin { deck, white };
Skin currentSkin() noexcept;
void setSkin (Skin) noexcept;
inline bool white() noexcept { return currentSkin() == Skin::white; }
void loadSkin();
void saveSkin();

inline const juce::Colour whiteInk (0xff1f2a44);    // White: 文字
inline const juce::Colour whiteMuted (0xff7a8190);  // White: 補足の文字
inline const juce::Colour whiteTeal (0xff1a9aaa);   // White: 動き・フィル (Deck のオレンジ)
inline const juce::Colour whitePurple (0xff6a59d4); // White: 音・音源 (Deck の青緑)
inline const juce::Colour whiteBlue (0xff2f7ff0);   // White: 出力・レコードのラベル
inline const juce::Colour screenTeal (0xff3cc8d4);  // White: 黒い画面の上の色 (フィル・パターン)
inline const juce::Colour screenPurple (0xffa392ff);// White: 黒い画面の上の色 (頭出しの範囲・ユーザー波形)
inline const juce::Colour ringBlue (0xff52b4ff);    // White: レコードの LED

// 今の見た目に合う色を選ぶ
inline juce::Colour pick (juce::Colour forDeck, juce::Colour forWhite) noexcept { return white() ? forWhite : forDeck; }
juce::Colour accent (juce::Colour deckAccent); // Deck のアクセント色を、今の見た目の色に置き換える
juce::Font logoFont (float size);              // White のロゴの書体

// White の書体 (埋め込み)。エディタが開いている間だけ持っておく
struct WhiteFonts
{
    WhiteFonts();
    juce::Typeface::Ptr body, logo;
};

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

    void refreshSkin(); // 見た目を変えた時に、部品の色を合わせる
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool isOver, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isOver, bool isDown) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool isDown, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override; // ノブの数値は、液晶の窓に

private:
    juce::SharedResourcePointer<deck::WhiteFonts> whiteFonts;
};
