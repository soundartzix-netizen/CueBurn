#include "DeckStyle.h"
#include "BinaryData.h"
#include <atomic>

using namespace ui;

namespace
{
// 決まった乱数 (ヘアラインの濃さ。描くたびに同じ模様)
float hash (int i)
{
    unsigned x = (unsigned) i * 2654435761u;
    x ^= x >> 13;
    x *= 0x5bd1e995u;
    x ^= x >> 15;
    return (float) (x & 0xffff) / 65535.0f;
}

std::atomic<int> skinNow { (int) deck::Skin::deck };

juce::File skinFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Sound Artz/Cue Burn/skin.txt");
}

// White の文字: 埋め込んだ Murecho (太さ 500)
juce::Font whiteFont (float size, bool)
{
    const juce::SharedResourcePointer<deck::WhiteFonts> fonts;
    return juce::Font (juce::FontOptions (fonts->body).withHeight (size * 1.08f));
}

// White のノブ: 白い本体 (縁は上が明るく下が暗い、面は少しへこむ) + 柔らかい影 + 値の色の弧
// 角度は 12 時から時計回り。弧は from から angle まで (両側に振れるノブは真ん中から)
void drawWhiteKnob (juce::Graphics& g, juce::Point<float> c, float r, float from, float angle, float start, float end, juce::Colour accent)
{
    const float ringR = r * 1.32f + 2.0f, ringW = juce::jmax (3.0f, r * 0.15f);
    auto at = [&] (float a, float rad) { return juce::Point<float> (c.x + std::sin (a) * rad, c.y - std::cos (a) * rad); };

    const juce::PathStrokeType st (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path track, value;
    track.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, start, end, true);
    g.setColour (juce::Colour (0xffd9dde4));
    g.strokePath (track, st);
    const float a0 = juce::jmin (from, angle), a1 = juce::jmax (from, angle);
    if (a1 - a0 > 0.01f)
    {
        value.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, a0, a1, true);
        g.setColour (accent.withAlpha (0.13f));
        g.strokePath (value, juce::PathStrokeType (ringW * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const auto p0 = at (a0, ringR), p1 = at (a1, ringR);
        g.setGradientFill (juce::ColourGradient (accent.brighter (0.45f), p0.x, p0.y, accent.darker (0.1f), p1.x, p1.y, false));
        g.strokePath (value, st);
    }

    const auto bodyR = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
    juce::Path body;
    body.addEllipse (bodyR);
    juce::DropShadow (juce::Colours::black.withAlpha (0.26f), juce::roundToInt (r * 0.8f), { 0, juce::roundToInt (r * 0.38f) }).drawForPath (g, body);
    juce::DropShadow (juce::Colours::black.withAlpha (0.20f), 3, { 0, 1 }).drawForPath (g, body);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, c.x, bodyR.getY(), juce::Colour (0xffbfc5ce), c.x, bodyR.getBottom(), false));
    g.fillEllipse (bodyR);
    const auto face = bodyR.reduced (r * 0.13f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe3e6eb), c.x, face.getY(), juce::Colour (0xfffbfcfd), c.x, face.getBottom(), false));
    g.fillEllipse (face);
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.drawEllipse (face.reduced (0.5f), 1.0f);

    g.setColour (accent.darker (0.15f));
    g.drawLine ({ at (angle, r * 0.22f), at (angle, r * 0.68f) }, juce::jmax (2.0f, r * 0.09f));
}

// ヘアライン (横方向の細い筋)
void hairlines (juce::Graphics& g, juce::Rectangle<float> r, float strength)
{
    for (int y = (int) r.getY(), k = 0; y < (int) r.getBottom(); y += 2, ++k)
    {
        const float h = hash (k * 7 + (int) r.getX());
        g.setColour ((h > 0.5f ? juce::Colours::white : juce::Colours::black).withAlpha (strength * (0.3f + 0.7f * std::abs (h - 0.5f) * 2.0f)));
        g.fillRect (r.getX(), (float) y, r.getWidth(), 1.0f);
    }
}

// 面取り: 左上は明るく、右下は暗い線
void chamfer (juce::Graphics& g, juce::Rectangle<float> r, float corner, float strength, bool raised)
{
    const auto light = juce::Colours::white.withAlpha (0.16f * strength), dark = juce::Colours::black.withAlpha (0.55f * strength);
    juce::Path p;
    p.addRoundedRectangle (r, corner);
    g.setGradientFill (juce::ColourGradient (raised ? light : dark, r.getX(), r.getY(), raised ? dark : light, r.getRight(), r.getBottom(), false));
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

// 光る線 (導光板): 両端は消えていく
void lightPipe (juce::Graphics& g, float x0, float x1, float y, juce::Colour col, float intensity)
{
    juce::ColourGradient cg (col.withAlpha (0.0f), x0, y, col.withAlpha (0.0f), x1, y, false);
    cg.addColour (0.12, col.withAlpha (0.9f * intensity));
    cg.addColour (0.88, col.withAlpha (0.9f * intensity));
    g.setGradientFill (cg);
    g.fillRect (x0, y - 0.6f, x1 - x0, 1.2f);
    juce::ColourGradient bloom (col.withAlpha (0.0f), x0, y, col.withAlpha (0.0f), x1, y, false);
    bloom.addColour (0.15, col.withAlpha (0.14f * intensity));
    bloom.addColour (0.85, col.withAlpha (0.14f * intensity));
    g.setGradientFill (bloom);
    g.fillRect (x0, y - 3.0f, x1 - x0, 6.0f);
}
}

namespace deck
{
Skin currentSkin() noexcept { return (Skin) skinNow.load(); }

void setSkin (Skin s) noexcept
{
    skinNow.store ((int) s);
    const bool w = s == Skin::white;
    ui::setFontOverride (w ? whiteFont : nullptr);
    HelpLabel::idleColour = w ? whiteInk.withAlpha (0.78f) : mutedCol;
    HelpLabel::hoverColour = w ? whiteInk : textCol;
    HelpLabel::iconHover = w ? whiteTeal : teal;
}

void loadSkin()
{
    setSkin (skinFile().loadFileAsString().trim() == "white" ? Skin::white : Skin::deck);
}

void saveSkin()
{
    const auto f = skinFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (white() ? "white" : "deck");
}

WhiteFonts::WhiteFonts()
    : body (juce::Typeface::createSystemTypefaceFor (BinaryData::MurechoMedium_ttf, (size_t) BinaryData::MurechoMedium_ttfSize)),
      logo (juce::Typeface::createSystemTypefaceFor (BinaryData::SedgwickAveDisplayRegular_ttf, (size_t) BinaryData::SedgwickAveDisplayRegular_ttfSize))
{
}

juce::Font logoFont (float size)
{
    const juce::SharedResourcePointer<WhiteFonts> fonts;
    return juce::Font (juce::FontOptions (fonts->logo).withHeight (size));
}

juce::Colour accent (juce::Colour c)
{
    if (! white())
        return c;
    if (c == orange || c == ledAmber)
        return whiteTeal;
    if (c == teal || c == ledCyan)
        return whitePurple;
    return whiteBlue;
}

void drawChassis (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (white())
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff5f6f8), 0.0f, r.getY(), juce::Colour (0xffdcdfe5), 0.0f, r.getBottom(), false));
        g.fillRect (r);
        return;
    }
    g.setGradientFill (juce::ColourGradient (chassisTop, 0.0f, r.getY(), chassisBottom, 0.0f, r.getBottom(), false));
    g.fillRect (r);
    hairlines (g, r, 0.02f);
    // 斜めに差し込む光
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY(), juce::Colours::white.withAlpha (0.0f), r.getRight(), r.getBottom(), false);
    sheen.addColour (0.35, juce::Colours::white.withAlpha (0.045f));
    sheen.addColour (0.5, juce::Colours::white.withAlpha (0.0f));
    g.setGradientFill (sheen);
    g.fillRect (r);
    // 周辺を暗く
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                                             juce::Colours::black.withAlpha (0.4f), r.getX(), r.getY(), true));
    g.fillRect (r);
}

void drawScrew (juce::Graphics& g, juce::Point<float> c, float rad, float angle)
{
    // 座ぐり (ビスの周りのくぼみ)
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (c.x - rad - 1.4f, c.y - rad - 1.0f, (rad + 1.4f) * 2.0f, (rad + 1.4f) * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (c.x - rad - 1.4f, c.y - rad - 0.6f, (rad + 1.4f) * 2.0f, (rad + 1.4f) * 2.0f, 0.6f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc4c9d1), c.x - rad * 0.4f, c.y - rad * 0.6f,
                                             juce::Colour (0xff2c3036), c.x + rad, c.y + rad, true));
    g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
    // 六角穴
    juce::Path hex;
    for (int i = 0; i < 6; ++i)
    {
        const float a = angle + (float) i * juce::MathConstants<float>::twoPi / 6.0f;
        const juce::Point<float> p (c.x + std::cos (a) * rad * 0.55f, c.y + std::sin (a) * rad * 0.55f);
        if (i == 0)
            hex.startNewSubPath (p);
        else
            hex.lineTo (p);
    }
    hex.closeSubPath();
    g.setColour (juce::Colour (0xff121417));
    g.fillPath (hex);
}

void drawMetalPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent, const juce::String& title, bool ja)
{
    if (white())
    {
        // 白い板: 柔らかい影、上の縁だけ明るく。見出しは色の点と文字
        constexpr float corner = 10.0f;
        juce::Path body;
        body.addRoundedRectangle (r, corner);
        juce::DropShadow (juce::Colours::black.withAlpha (0.10f), 16, { 0, 5 }).drawForPath (g, body);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffcfcfd), 0.0f, r.getY(), juce::Colour (0xffeef0f3), 0.0f, r.getBottom(), false));
        g.fillPath (body);
        g.setColour (juce::Colour (0xffd3d8df));
        g.strokePath (body, juce::PathStrokeType (1.0f));
        g.setColour (juce::Colours::white);
        g.drawLine (r.getX() + corner, r.getY() + 1.0f, r.getRight() - corner, r.getY() + 1.0f, 1.0f);
        g.setColour (deck::accent (accent));
        g.fillEllipse (r.getX() + 13.0f, r.getY() + 13.5f, 6.0f, 6.0f);
        g.setFont (fontOf (ja ? 12.5f : 11.5f, true, 0.12f));
        g.setColour (whiteInk.withAlpha (0.82f));
        g.drawText (title, juce::Rectangle<float> (r.getX() + 25.0f, r.getY() + 7.0f, r.getWidth() - 40.0f, 19.0f), juce::Justification::centredLeft, false);
        return;
    }
    constexpr float corner = 9.0f;
    // 落ち影
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (0.0f, 4.0f).expanded (1.0f), corner + 1.0f);

    // 本体: ガンメタル + ヘアライン
    juce::Path body;
    body.addRoundedRectangle (r, corner);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff353a43), 0.0f, r.getY(), juce::Colour (0xff1a1d22), 0.0f, r.getBottom(), false));
    g.fillPath (body);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (body);
        hairlines (g, r, 0.035f);
        // 上からの光
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), 0.0f, r.getY(),
                                                 juce::Colours::transparentWhite, 0.0f, r.getY() + 40.0f, false));
        g.fillRect (r.withHeight (40.0f));
    }

    // 外側の面取りと、内側の削り出しの段差
    chamfer (g, r, corner, 1.2f, true);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r.expanded (0.8f), corner + 0.8f, 0.8f);
    chamfer (g, r.reduced (4.0f), corner - 3.0f, 0.7f, false);

    // 上辺の光る線 (アクセントの色)
    lightPipe (g, r.getX() + corner, r.getRight() - corner, r.getY() + 2.2f, accent, 1.0f);

    // ビス
    const float inset = 8.0f;
    drawScrew (g, { r.getX() + inset, r.getBottom() - inset }, 2.5f, 0.3f);
    drawScrew (g, { r.getRight() - inset, r.getBottom() - inset }, 2.5f, 0.9f);
    drawScrew (g, { r.getRight() - inset, r.getY() + inset + 2.0f }, 2.5f, 0.5f);

    // 見出し: 彫り込んだ板 + LED + シルク印刷の文字
    g.setFont (fontOf (ja ? 12.0f : 10.5f, true, 0.22f));
    const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title);
    const auto plate = juce::Rectangle<float> (r.getX() + 9.0f, r.getY() + 7.0f, tw + 32.0f, 19.0f);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (plate, 4.0f);
    chamfer (g, plate, 4.0f, 1.0f, false);
    drawLed (g, { plate.getX() + 10.0f, plate.getCentreY() }, 2.8f, accent, true);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawText (title, (int) plate.getX() + 19, (int) plate.getY() + 2, (int) tw + 10, 16, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xfff1f3f6));
    g.drawText (title, (int) plate.getX() + 19, (int) plate.getY() + 1, (int) tw + 10, 16, juce::Justification::centredLeft);
}

void drawLcd (juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    // クロームのふち
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff15171b), 0.0f, r.getY() - 1.5f, juce::Colour (0xff7d838c), 0.0f, r.getBottom() + 1.5f, false));
    g.fillRoundedRectangle (r.expanded (1.5f), corner + 1.5f);
    g.setColour (juce::Colour (0xff040506));
    g.fillRoundedRectangle (r, corner);
    // ガラス: ほんのり青い黒
    const auto in = r.reduced (1.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e1824), 0.0f, in.getY(), juce::Colour (0xff05080c), 0.0f, in.getBottom(), false));
    g.fillRoundedRectangle (in, juce::jmax (1.0f, corner - 1.5f));
    // 画素の格子 (ごく薄く)
    for (float y = in.getY() + 1.0f; y < in.getBottom(); y += 3.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.014f));
        g.fillRect (in.getX() + 2.0f, y, in.getWidth() - 4.0f, 1.0f);
    }
    for (float x = in.getX() + 2.0f; x < in.getRight() - 2.0f; x += 3.0f)
    {
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillRect (x, in.getY() + 1.0f, 1.0f, in.getHeight() - 2.0f);
    }
}

void drawLcdGloss (juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    const auto in = r.reduced (1.5f);
    juce::Path p;
    p.addRoundedRectangle (in, juce::jmax (1.0f, corner - 1.5f));
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (p);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.08f), 0.0f, in.getY(),
                                             juce::Colours::transparentWhite, 0.0f, in.getY() + in.getHeight() * 0.42f, false));
    g.fillRect (in.withHeight (in.getHeight() * 0.42f));
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, in.getY(),
                                             juce::Colours::transparentBlack, 0.0f, in.getY() + 6.0f, false));
    g.fillRect (in.withHeight (6.0f));
}

void drawLed (juce::Graphics& g, juce::Point<float> c, float rad, juce::Colour col, bool on)
{
    if (on)
    {
        g.setColour (col.withAlpha (0.1f));
        g.fillEllipse (c.x - rad * 3.6f, c.y - rad * 3.6f, rad * 7.2f, rad * 7.2f);
        g.setColour (col.withAlpha (0.3f));
        g.fillEllipse (c.x - rad * 1.9f, c.y - rad * 1.9f, rad * 3.8f, rad * 3.8f);
        g.setColour (col);
        g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.fillEllipse (c.x - rad * 0.45f, c.y - rad * 0.6f, rad * 0.8f, rad * 0.7f);
    }
    else
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillEllipse (c.x - rad - 0.5f, c.y - rad - 0.5f, rad * 2.0f + 1.0f, rad * 2.0f + 1.0f);
        g.setColour (col.withMultipliedBrightness (0.28f).withAlpha (0.9f));
        g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
    }
}

void drawPad (juce::Graphics& g, juce::Rectangle<float> r, bool lit, bool hover, bool down, juce::Colour led)
{
    if (white())
    {
        // 白いキー: 押していない時は少し浮かせる。点いている物は色の枠と下の線
        constexpr float corner = 6.0f;
        r = r.reduced (2.0f);
        const auto body = down ? r.translated (0.0f, 1.0f) : r;
        juce::Path p;
        p.addRoundedRectangle (body, corner);
        if (! down)
            juce::DropShadow (juce::Colours::black.withAlpha (0.12f), 6, { 0, 2 }).drawForPath (g, p);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, 0.0f, body.getY(),
                                                 hover ? juce::Colour (0xfff1f3f6) : juce::Colour (0xffe8ebef), 0.0f, body.getBottom(), false));
        g.fillPath (p);
        const auto col = accent (led);
        if (lit)
        {
            g.setColour (col.withAlpha (0.09f));
            g.fillPath (p);
            g.setColour (col);
            g.strokePath (p, juce::PathStrokeType (1.4f));
            g.fillRoundedRectangle (body.getX() + corner + 2.0f, body.getBottom() - 3.5f, body.getWidth() - corner * 2.0f - 4.0f, 2.0f, 1.0f);
        }
        else
        {
            g.setColour (hover ? juce::Colour (0xffbfc5ce) : juce::Colour (0xffd0d5dc));
            g.strokePath (p, juce::PathStrokeType (1.0f));
        }
        return;
    }
    constexpr float corner = 5.0f;
    r = r.reduced (2.0f);
    // クロームの枠 (パッドがはまっている穴)
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0d0e11), 0.0f, r.getY() - 2.0f, juce::Colour (0xff8a9099), 0.0f, r.getBottom() + 2.0f, false));
    g.fillRoundedRectangle (r.expanded (2.0f), corner + 2.0f);
    g.setColour (juce::Colour (0xff08090b));
    g.fillRoundedRectangle (r.expanded (0.6f), corner + 0.6f);

    const auto body = down ? r.translated (0.0f, 1.0f).reduced (0.5f) : r;
    // ゴムのパッド
    const auto top = lit ? juce::Colour (0xff4a4236) : (hover ? juce::Colour (0xff3c4049) : juce::Colour (0xff32363e));
    const auto bottom = lit ? juce::Colour (0xff27221c) : juce::Colour (0xff1b1e23);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, body.getY(), bottom, 0.0f, body.getBottom(), false));
    g.fillRoundedRectangle (body, corner);

    if (lit)
    {
        // 中から LED の光 + 外へにじむ光
        g.setGradientFill (juce::ColourGradient (led.withAlpha (0.08f), 0.0f, body.getY(), led.withAlpha (0.42f), 0.0f, body.getBottom(), false));
        g.fillRoundedRectangle (body, corner);
        g.setColour (led.withAlpha (0.18f));
        g.drawRoundedRectangle (body.expanded (2.5f), corner + 2.5f, 4.0f);
        g.setColour (led);
        g.drawRoundedRectangle (body, corner, 1.4f);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawLine (body.getX() + corner + 2.0f, body.getBottom() - 1.2f, body.getRight() - corner - 2.0f, body.getBottom() - 1.2f, 1.0f);
    }
    g.setColour (juce::Colours::white.withAlpha (lit ? 0.22f : 0.12f));
    g.drawLine (body.getX() + corner, body.getY() + 1.0f, body.getRight() - corner, body.getY() + 1.0f, 1.0f);
}

void drawChromeText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float size, juce::Colour glow)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (fontOf (size, true, 0.18f), text, r.getX(), r.getCentreY() + size * 0.36f);
    juce::Path p;
    ga.createPath (p);
    const auto b = p.getBounds();
    // にじむ光
    g.setColour (glow.withAlpha (0.12f));
    g.strokePath (p, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (glow.withAlpha (0.22f));
    g.strokePath (p, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // 影
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillPath (p, juce::AffineTransform::translation (0.0f, 1.6f));
    // クローム: 上は白く、真ん中で暗く折り返し、下はまた明るい
    juce::ColourGradient cg (juce::Colours::white, 0.0f, b.getY(), juce::Colour (0xffb9bec6), 0.0f, b.getBottom(), false);
    cg.addColour (0.46, juce::Colour (0xffdfe3e9));
    cg.addColour (0.52, juce::Colour (0xff5b616b));
    cg.addColour (0.75, juce::Colour (0xff9aa0a9));
    g.setGradientFill (cg);
    g.fillPath (p);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.strokePath (p, juce::PathStrokeType (0.5f));
}
}

//==============================================================================
DeckLookAndFeel::DeckLookAndFeel()
{
    refreshSkin();
}

void DeckLookAndFeel::refreshSkin()
{
    using namespace deck;
    const auto word = pick (lcdBlue, whiteInk);
    setColour (juce::ComboBox::textColourId, pick (juce::Colour (0xffd8f0ff), whiteInk));
    setColour (juce::Label::textColourId, pick (textCol, whiteInk));
    setColour (juce::PopupMenu::backgroundColourId, pick (juce::Colour (0xff1a1d22), juce::Colour (0xfffbfcfd)));
    setColour (juce::PopupMenu::textColourId, pick (textCol, whiteInk));
    setColour (juce::PopupMenu::headerTextColourId, pick (textCol, whiteMuted));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, pick (juce::Colour (0xff3a3428), whiteTeal.withAlpha (0.16f)));
    setColour (juce::PopupMenu::highlightedTextColourId, pick (juce::Colours::white, whiteInk));
    setColour (juce::TextEditor::backgroundColourId, pick (juce::Colour (0xff0b1119), juce::Colours::white));
    setColour (juce::TextEditor::textColourId, word);
    setColour (juce::TextEditor::highlightColourId, pick (word.withAlpha (0.3f), whiteTeal.withAlpha (0.25f)));
    setColour (juce::TextEditor::highlightedTextColourId, pick (juce::Colours::white, whiteInk));
    setColour (juce::CaretComponent::caretColourId, word);
    setColour (juce::TooltipWindow::backgroundColourId, pick (juce::Colour (0xf0222735), juce::Colour (0xf8fbfcfd)));
    setColour (juce::TooltipWindow::outlineColourId, pick (juce::Colour (0xff3d4558), juce::Colour (0xffcfd4db)));
    setColour (juce::TooltipWindow::textColourId, pick (textCol, whiteInk));
}

void DeckLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool isOver, bool isDown)
{
    deck::drawPad (g, b.getLocalBounds().toFloat(), b.getToggleState(), isOver && b.isEnabled(), isDown);
}

void DeckLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool isDown)
{
    const bool lit = b.getToggleState();
    g.setFont (getTextButtonFont (b, b.getHeight()));
    auto r = b.getLocalBounds().toFloat().reduced (5.0f, 1.0f).translated (0.0f, isDown ? 1.0f : 0.0f);
    if (deck::white())
    {
        const auto col = lit ? deck::whiteTeal.darker (0.25f) : deck::whiteInk;
        g.setColour (b.isEnabled() ? col : col.withAlpha (0.35f));
        g.drawFittedText (b.getButtonText(), r.toNearestInt(), juce::Justification::centred, 1);
        return;
    }
    const auto col = lit ? juce::Colour (0xffffe9c8) : juce::Colour (0xffcdd2db);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawFittedText (b.getButtonText(), r.translated (0.0f, 1.0f).toNearestInt(), juce::Justification::centred, 1);
    g.setColour (b.isEnabled() ? col : col.withAlpha (0.35f));
    g.drawFittedText (b.getButtonText(), r.toNearestInt(), juce::Justification::centred, 1);
}

void DeckLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool isDown, int, int, int, int, juce::ComboBox& box)
{
    const auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (1.5f);
    if (deck::white())
    {
        // 白い入力欄: 細い枠と、色の三角
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (isDown ? deck::whiteTeal : juce::Colour (0xffcbd1d9));
        g.drawRoundedRectangle (b, 5.0f, 1.0f);
        juce::Path arrow;
        const float cx = (float) w - 13.0f, cy = (float) h * 0.5f;
        arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (deck::accent (box.findColour (juce::ComboBox::outlineColourId)));
        g.fillPath (arrow);
        return;
    }
    deck::drawLcd (g, b, 5.0f);
    if (isDown)
    {
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (b.reduced (1.5f), 4.0f);
    }
    deck::drawLcdGloss (g, b, 5.0f);
    juce::Path arrow;
    const float cx = (float) w - 13.0f, cy = (float) h * 0.5f;
    arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    const auto col = box.findColour (juce::ComboBox::outlineColourId);
    g.setColour (col.withAlpha (0.3f));
    g.strokePath (arrow, juce::PathStrokeType (3.0f));
    g.setColour (col);
    g.fillPath (arrow);
}

void DeckLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // ノブの数値だけ、液晶の窓に光る文字で (ほかのラベルは、ふつうに)
    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) == nullptr)
    {
        PluginLookAndFeel::drawLabel (g, label);
        return;
    }
    if (deck::white())
    {
        if (! label.isBeingEdited())
        {
            g.setFont (fontOf (11.5f));
            g.setColour (deck::whiteMuted);
            g.drawText (label.getText(), label.getLocalBounds().toFloat(), juce::Justification::centred, false);
        }
        return;
    }
    const auto r = label.getLocalBounds().toFloat().reduced (8.0f, 1.0f);
    deck::drawLcd (g, r, 3.5f);
    if (! label.isBeingEdited())
    {
        g.setFont (fontOf (11.5f, true, 0.12f));
        const auto txt = label.getText();
        g.setColour (deck::lcdBlue.withAlpha (0.28f));
        g.drawText (txt, r.translated (0.0f, 0.5f).expanded (1.0f, 0.0f), juce::Justification::centred, false);
        g.setColour (deck::lcdBlue);
        g.drawText (txt, r, juce::Justification::centred, false);
    }
    deck::drawLcdGloss (g, r, 3.5f);
}

void DeckLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle,
                                        float endAngle, juce::Slider& s)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float d = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float r = d * 0.5f;
    const auto fill = s.findColour (juce::Slider::rotarySliderFillColourId);
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float halfPi = juce::MathConstants<float>::halfPi;
    auto at = [&] (float a, float rr) { return juce::Point<float> (c.x + std::cos (a - halfPi) * rr, c.y + std::sin (a - halfPi) * rr); };

    float from = 0.0f;
    if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0)
        from = (float) s.valueToProportionOfLength (0.0);
    if (deck::white())
    {
        drawWhiteKnob (g, c, r * 0.6f, startAngle + from * (endAngle - startAngle), angle, startAngle, endAngle, fill);
        return;
    }

    // LED の目盛り (点いている所まで光る)
    constexpr int dots = 21;
    for (int i = 0; i < dots; ++i)
    {
        const float t = (float) i / (float) (dots - 1);
        const bool on = (t >= juce::jmin (from, pos) - 1.0e-4f && t <= juce::jmax (from, pos) + 1.0e-4f);
        deck::drawLed (g, at (startAngle + t * (endAngle - startAngle), r * 0.95f), juce::jmax (1.1f, r * 0.045f), fill, on);
    }

    // 刻印の目盛り
    for (int i = 0; i <= 40; ++i)
    {
        const float a = startAngle + (float) i / 40.0f * (endAngle - startAngle);
        const bool major = i % 10 == 0;
        g.setColour (juce::Colours::white.withAlpha (major ? 0.5f : 0.22f));
        g.drawLine ({ at (a, r * (major ? 0.77f : 0.80f)), at (a, r * 0.855f) }, major ? 1.2f : 0.7f);
    }

    // 受け (筐体の穴): 面取りの段差
    const float sockR = r * 0.74f;
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillEllipse (c.x - sockR, c.y - sockR + 1.5f, sockR * 2.0f, sockR * 2.0f);
    g.setColour (juce::Colour (0xff090a0c));
    g.fillEllipse (c.x - sockR, c.y - sockR, sockR * 2.0f, sockR * 2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.8f), c.x, c.y - sockR, juce::Colours::white.withAlpha (0.18f), c.x, c.y + sockR, false));
    g.drawEllipse (c.x - sockR, c.y - sockR, sockR * 2.0f, sockR * 2.0f, 1.2f);

    // ローレットの側面
    const float capR = r * 0.665f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a707a), c.x, c.y - capR, juce::Colour (0xff1c1f23), c.x, c.y + capR, false));
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
    for (int i = 0; i < 48; ++i)
    {
        const float a = angle + (float) i * juce::MathConstants<float>::twoPi / 48.0f;
        const float lit = 0.5f + 0.5f * std::cos (a + 0.6f);
        g.setColour (juce::Colours::black.withAlpha (0.35f + 0.25f * (1.0f - lit)));
        g.drawLine ({ at (a, capR * 0.84f), at (a, capR * 0.995f) }, 1.0f);
    }

    // 天面: 削り出し (同心円の筋)、ふちの面取りは明るく光る
    const float topR = capR * 0.8f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd0d5dc), c.x - topR * 0.5f, c.y - topR * 0.7f,
                                             juce::Colour (0xff3e424a), c.x + topR * 0.6f, c.y + topR, true));
    g.fillEllipse (c.x - topR, c.y - topR, topR * 2.0f, topR * 2.0f);
    for (float rr = topR * 0.12f; rr < topR; rr += 1.3f)
    {
        g.setColour ((((int) (rr * 3.0f)) % 2 == 0 ? juce::Colours::white : juce::Colours::black).withAlpha (0.055f));
        g.drawEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f, 0.7f);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.85f), c.x - topR, c.y - topR, juce::Colours::black.withAlpha (0.6f), c.x + topR, c.y + topR, false));
    g.drawEllipse (c.x - topR, c.y - topR, topR * 2.0f, topR * 2.0f, 1.3f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (c.x - 1.5f, c.y - 1.5f, 3.0f, 3.0f);

    // 指針 (彫り込みの線) と、先の光る点
    const auto p0 = at (angle, topR * 0.22f), p1 = at (angle, topR * 0.78f);
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.drawLine ({ p0, p1 }, 3.0f);
    g.setColour (juce::Colours::white);
    g.drawLine ({ p0, p1 }, 1.4f);
    deck::drawLed (g, at (angle, topR * 0.86f), juce::jmax (1.2f, r * 0.04f), fill, true);
}
