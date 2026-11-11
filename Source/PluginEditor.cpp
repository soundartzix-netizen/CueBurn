#include "PluginEditor.h"
#include "Localization.h"
#include "PresetSamples.h"

namespace { constexpr int kMyPatternId = 1000; } // テンプレートの一覧: マイパターンの番号はここから

using namespace ui;

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;

juce::StringArray choiceItems (const juce::String& id, bool)
{
    juce::StringArray a;
    if (id == "rate")
        for (auto* n : kRateNames)
            a.add (n);
    return a;
}

const LocEntry* findLoc (const juce::String& id)
{
    for (auto& e : kLoc)
        if (id == e.id)
            return &e;
    return nullptr;
}

void drawPanelFrame (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent, const juce::String& title, bool ja)
{
    deck::drawMetalPanel (g, r, accent, title, ja);
}

// ノイズの絵 (決まった形のギザギザ)
float pseudoNoise (int i)
{
    unsigned int x = (unsigned int) i * 2654435761u;
    x ^= x >> 13;
    x *= 0x5bd1e995u;
    x ^= x >> 15;
    return (float) (x & 0xffff) / 32767.5f - 1.0f;
}

// パターンごとの色 (タイムライン)
juce::Colour patternColour (int pattern)
{
    if (deck::white())
        return pattern >= ScratchProcessor::numPresetPatterns ? deck::screenPurple : deck::screenTeal;
    static const juce::Colour c[ScratchProcessor::numPatterns] = {
        orange, teal, violet, juce::Colour (0xffff6b8b), blue, juce::Colour (0xffffd166),
        juce::Colour (0xff9be564), juce::Colour (0xffe07bff), juce::Colour (0xff7fe3ff), juce::Colour (0xffff4d4d),
        juce::Colour (0xffc8a27a), juce::Colour (0xffb8f2c8), juce::Colour (0xffff8c5a), juce::Colour (0xff6ee7a8),
        juce::Colour (0xffeeeeee), juce::Colour (0xffffb3d9), juce::Colour (0xff8ab4ff), juce::Colour (0xffd9ff66) // ユーザー波形
    };
    return c[juce::jlimit (0, ScratchProcessor::numPatterns - 1, pattern)];
}

// メニュー用の、パターンの小さな絵 (動きのカーブ)
std::unique_ptr<juce::Drawable> patternIcon (const ScratchProcessor& proc, int pattern)
{
    juce::Path p;
    const int steps = 40;
    for (int i = 0; i <= steps; ++i)
    {
        const float phi = (float) i / (float) steps;
        const juce::Point<float> pt (32.0f * phi, 14.0f - 13.0f * proc.shapeD (pattern, juce::jmin (phi, 0.99999f)));
        if (i == 0)
            p.startNewSubPath (pt);
        else
            p.lineTo (pt);
    }
    auto d = std::make_unique<juce::DrawablePath>();
    d->setPath (p);
    d->setFill (juce::FillType (juce::Colours::transparentBlack));
    d->setStrokeFill (juce::FillType (patternColour (pattern)));
    d->setStrokeType (juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    return d;
}

// マス目の名前
juce::String gridName (int ticks)
{
    switch (ticks)
    {
        case 3: return "1/32";
        case 4: return "1/16T";
        case 8: return "1/8T";
        default: return "1/16";
    }
}

const int kGridTicks[] = { 6, 3, 8, 4 }; // マス目の選択肢: 1/16, 1/32, 1/8T, 1/16T

juce::String noteName (int note)
{
    return juce::MidiMessage::getMidiNoteName (note, true, true, 5); // FL Studio と同じく、60 = C5
}
}

//==============================================================================
juce::Point<float> PlatterView::centre() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (22.0f).getCentre();
}

float PlatterView::radius() const
{
    auto b = getLocalBounds().toFloat().withTrimmedBottom (22.0f);
    return juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 6.0f;
}

float PlatterView::angleAt (juce::Point<float> p) const
{
    const auto c = centre();
    return std::atan2 (p.y - c.y, p.x - c.x); // 画面は下向きがy+なので、時計回りが正
}

void PlatterView::mouseDown (const juce::MouseEvent& e)
{
    if (e.position.getDistanceFrom (centre()) > radius() * 1.02f)
        return;
    dragging = true;
    lastAng = angleAt (e.position);
    proc.platterTouched.store (true);
    repaint();
}

void PlatterView::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    if (e.position.getDistanceFrom (centre()) < 16.0f) // 中心の近くは、角度が不安定なので無視
        return;

    const float a = angleAt (e.position);
    float d = a - lastAng;
    while (d > kPi) d -= 2.0f * kPi;
    while (d < -kPi) d += 2.0f * kPi;
    lastAng = a;
    proc.platterAngle.store (proc.platterAngle.load() + (double) d);
}

void PlatterView::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;
    dragging = false;
    proc.platterTouched.store (false);
    repaint();
}

void PlatterView::paint (juce::Graphics& g)
{
    // CDJ のジョグホイール風: クロームの縁、回るローレット、光るリング、削り出しの天板、中央の液晶
    const auto c = centre();
    const float R = radius();
    const float ang = proc.uiAngle.load();
    const bool active = proc.uiActive.load();
    const int note = proc.uiNote.load();
    const auto ring = dragging || active ? deck::ledAmber : deck::ledCyan;
    const float twoPi = juce::MathConstants<float>::twoPi;
    auto disc = [&] (float rr) { return juce::Rectangle<float> (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f); };

    if (deck::white())
    {
        // CDJ のジョグ: 金属の縁 → LED の溝 → 一段高いガラスの盤 → 奥まった中央の液晶
        const bool live = dragging || active;
        const float halfPi = juce::MathConstants<float>::halfPi;
        auto pt = [&] (float a, float rr) { return juce::Point<float> (c.x + std::cos (a) * rr, c.y + std::sin (a) * rr); };

        // 金属の縁 (左上から光)
        juce::Path rim;
        rim.addEllipse (disc (R));
        juce::DropShadow (juce::Colours::black.withAlpha (0.28f), 22, { 0, 10 }).drawForPath (g, rim);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffcfdfe), c.x - R * 0.7f, c.y - R * 0.7f,
                                                 juce::Colour (0xff98a0ab), c.x + R * 0.7f, c.y + R * 0.8f, false));
        g.fillPath (rim);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawEllipse (disc (R - 0.8f), 1.0f);

        // LED の溝 (へこんでいる: 上の縁が暗く、下の縁が明るい)
        const float chR = R * 0.935f;
        g.setColour (juce::Colour (0xff101215));
        g.fillEllipse (disc (chR));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a515b), c.x, c.y - chR, juce::Colours::white.withAlpha (0.9f), c.x, c.y + chR, false));
        g.drawEllipse (disc (chR), 1.2f);

        // LED: 光の尾が盤の回転について回る。止まっている時は、ゆっくり回って待機を知らせる
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const float head = live ? ang - halfPi : (float) std::fmod (now * 0.8, (double) twoPi) - halfPi;
        const float level = live ? 1.0f : 0.55f;
        constexpr int leds = 72;
        const float ledR = R * 0.902f, dot = juce::jmax (1.3f, R * 0.016f);
        for (int i = 0; i < leds; ++i)
        {
            const float a = (float) i * twoPi / (float) leds - halfPi;
            float behind = std::fmod (head - a + twoPi * 4.0f, twoPi); // 光の先頭から、後ろへ何ラジアンか
            float lv = behind < 1.7f ? 1.0f - behind / 1.7f : 0.0f;
            if (behind > twoPi - 0.08f)
                lv = 1.0f; // 先頭のすぐ前
            lv = lv * lv * level;
            const auto p = pt (a, ledR);
            g.setColour (deck::ringBlue.withAlpha (0.10f));
            g.fillEllipse (p.x - dot, p.y - dot, dot * 2.0f, dot * 2.0f);
            if (lv > 0.01f)
            {
                g.setColour (deck::ringBlue.withAlpha (0.22f * lv));
                g.fillEllipse (p.x - dot * 2.8f, p.y - dot * 2.8f, dot * 5.6f, dot * 5.6f);
                g.setColour (deck::ringBlue.withAlpha (lv).interpolatedWith (juce::Colours::white, 0.35f * lv));
                g.fillEllipse (p.x - dot, p.y - dot, dot * 2.0f, dot * 2.0f);
            }
        }

        // 盤: 側面 (厚み) を下にずらして見せ、その上にガラスの天面
        const float topR = R * 0.855f;
        juce::Path top;
        top.addEllipse (disc (topR));
        juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 10, { 0, 4 }).drawForPath (g, top);
        g.setColour (juce::Colour (0xff050506));
        g.fillEllipse (disc (topR).translated (0.0f, 6.0f));
        g.setColour (juce::Colour (0xff4a505a));
        g.drawEllipse (disc (topR).translated (0.0f, 6.0f), 1.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2f37), c.x - topR * 0.3f, c.y - topR * 0.45f,
                                                 juce::Colour (0xff07080a), c.x + topR, c.y + topR * 0.2f, true));
        g.fillPath (top);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (top);
            // ガラスの下の溝 (薄く) と、回転が分かる刻み
            for (float rr = topR * 0.45f; rr < topR * 0.97f; rr += 2.4f)
            {
                g.setColour (juce::Colours::white.withAlpha (std::fmod (rr, 4.8f) < 2.4f ? 0.03f : 0.012f));
                g.drawEllipse (disc (rr), 0.7f);
            }
            for (int i = 0; i < 48; ++i)
            {
                const float a = ang + (float) i * twoPi / 48.0f;
                g.setColour (juce::Colours::white.withAlpha (i % 12 == 0 ? 0.28f : 0.07f));
                g.drawLine ({ pt (a, topR * 0.9f), pt (a, topR * 0.965f) }, i % 12 == 0 ? 1.6f : 1.0f);
            }
            // ガラスの映り込み: 左上の大きな柔らかい光と、縁の細い光
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), c.x - topR * 0.35f, c.y - topR * 0.6f,
                                                     juce::Colours::transparentWhite, c.x - topR * 0.35f + topR * 0.9f, c.y - topR * 0.6f, true));
            g.fillEllipse (disc (topR));
            // 三日月形の映り込み (盤の円から、右下にずらした円を抜く)
            juce::Path crescent;
            crescent.addEllipse (disc (topR));
            crescent.addEllipse (disc (topR * 0.97f).translated (topR * 0.07f, topR * 0.10f));
            crescent.setUsingNonZeroWinding (false);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), c.x - topR * 0.6f, c.y - topR * 0.7f,
                                                     juce::Colours::transparentWhite, c.x + topR * 0.2f, c.y - topR * 0.1f, false));
            g.fillPath (crescent);
        }
        juce::Path edgeLit, edgeDim;
        edgeLit.addCentredArc (c.x, c.y, topR - 0.8f, topR - 0.8f, 0.0f, -1.9f, 0.2f, true);
        edgeDim.addCentredArc (c.x, c.y, topR - 0.8f, topR - 0.8f, 0.0f, 1.6f, 3.6f, true);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.strokePath (edgeLit, juce::PathStrokeType (1.2f));
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.strokePath (edgeDim, juce::PathStrokeType (1.0f));

        // 中央の液晶: 金属の縁 → 深い井戸 → 画面。外側を回る目盛りで位置を示す
        const float wellR = R * 0.39f, lcdR = R * 0.335f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe9edf2), c.x - wellR, c.y - wellR, juce::Colour (0xff5d646e), c.x + wellR, c.y + wellR, false));
        g.fillEllipse (disc (wellR));
        g.setColour (juce::Colour (0xff020304));
        g.fillEllipse (disc (wellR - 2.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0d1b2e), c.x, c.y - lcdR, juce::Colour (0xff04080d), c.x, c.y + lcdR, false));
        g.fillEllipse (disc (lcdR).translated (0.0f, 1.5f));
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.75f), c.x, c.y - lcdR, juce::Colours::transparentBlack, c.x, c.y - lcdR * 0.55f, false));
        g.fillEllipse (disc (lcdR).translated (0.0f, 1.5f)); // 縁の影で、奥まって見せる

        constexpr int segs = 40;
        float pa = std::fmod (ang, twoPi);
        if (pa < 0.0f)
            pa += twoPi;
        const int cur = (int) (pa / twoPi * (float) segs) % segs;
        for (int i = 0; i < segs; ++i)
        {
            const float a = (float) i * twoPi / (float) segs - halfPi;
            const int dist = (cur - i + segs) % segs; // 後ろへ尾を引く
            const float lv = dist == 0 ? 1.0f : (dist < 6 ? 0.55f - 0.08f * (float) dist : 0.1f);
            g.setColour (deck::ringBlue.withAlpha (lv));
            g.drawLine ({ pt (a, lcdR * 0.80f).translated (0.0f, 1.5f), pt (a, lcdR * 0.93f).translated (0.0f, 1.5f) }, dist == 0 ? 3.0f : 2.0f);
        }
        g.setColour (juce::Colour (0xffe8f3ff));
        g.setFont (fontOf (lcdR * 0.52f, true));
        g.drawText (note >= 0 ? noteName (note) : juce::String ("--"), disc (lcdR * 0.7f).translated (0.0f, -lcdR * 0.08f), juce::Justification::centred);
        g.setColour (deck::ringBlue.withAlpha (live ? 1.0f : 0.6f));
        g.setFont (fontOf (lcdR * 0.22f, true, 0.12f));
        g.drawText (dragging ? "SCRATCH" : (active ? "PLAY" : "CUE"), disc (lcdR * 0.7f).translated (0.0f, lcdR * 0.34f), juce::Justification::centred);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (juce::Rectangle<float> (c.x - wellR, c.y - wellR, wellR * 2.0f, wellR * 0.95f).toNearestInt());
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), c.x, c.y - lcdR, juce::Colours::transparentWhite, c.x, c.y, false));
            g.fillEllipse (disc (lcdR));
        }

        g.setFont (fontOf (japanese ? 11.5f : 10.5f, true, 0.08f));
        g.setColour (dragging || note >= 0 ? deck::whiteBlue : deck::whiteMuted);
        juce::String text;
        if (dragging)
            text = u8 ("◀  ▶");
        else if (note >= 0)
            text = u8 ("♪ ") + noteName (note);
        else
            text = u8 (japanese ? "ノートを置くか、レコードをドラッグ" : "PLAY NOTES OR DRAG THE RECORD");
        g.drawText (text, getLocalBounds().toFloat().removeFromBottom (20.0f), juce::Justification::centred);
        return;
    }

    // 落ち影
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (disc (R + 2.0f).translated (0.0f, 6.0f));

    // クロームの縁 (左上から光)
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe6e9ee), c.x - R * 0.7f, c.y - R * 0.7f,
                                             juce::Colour (0xff2a2d33), c.x + R * 0.7f, c.y + R * 0.8f, false));
    g.fillEllipse (disc (R));
    {
        juce::Path hl;
        hl.addCentredArc (c.x, c.y, R * 0.985f, R * 0.985f, 0.0f, -2.6f, -0.9f, true);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.strokePath (hl, juce::PathStrokeType (1.4f));
        // 刻印の目盛り (5度ごと、30度ごとに長く)
        for (int i = 0; i < 72; ++i)
        {
            const float a = (float) i * twoPi / 72.0f;
            const bool major = i % 6 == 0;
            g.setColour (juce::Colours::black.withAlpha (major ? 0.55f : 0.3f));
            g.drawLine (c.x + std::cos (a) * R * (major ? 0.958f : 0.97f), c.y + std::sin (a) * R * (major ? 0.958f : 0.97f),
                        c.x + std::cos (a) * R * 0.993f, c.y + std::sin (a) * R * 0.993f, major ? 1.1f : 0.6f);
        }
    }

    // 側面のローレット (レコードと一緒に回る)
    const float knR = R * 0.955f;
    g.setColour (juce::Colour (0xff17191d));
    g.fillEllipse (disc (knR));
    for (int i = 0; i < 120; ++i)
    {
        const float a = ang + (float) i * twoPi / 120.0f;
        const float lit = 0.5f + 0.5f * std::cos (a + 2.3f); // 左上が明るい
        g.setColour (juce::Colours::white.withAlpha (0.05f + 0.12f * lit));
        g.drawLine (c.x + std::cos (a) * knR * 0.92f, c.y + std::sin (a) * knR * 0.92f,
                    c.x + std::cos (a) * knR * 0.995f, c.y + std::sin (a) * knR * 0.995f, 1.0f);
    }

    // 光るリング
    const float ringR = R * 0.895f;
    g.setColour (ring.withAlpha (0.08f));
    g.drawEllipse (disc (ringR), 16.0f);
    g.setColour (ring.withAlpha (0.18f));
    g.drawEllipse (disc (ringR), 7.0f);
    g.setColour (ring.withAlpha (0.45f));
    g.drawEllipse (disc (ringR), 3.5f);
    g.setColour (ring.brighter (0.4f));
    g.drawEllipse (disc (ringR), 1.4f);

    // 天板: 削り出しの金属 (同心円の筋 + 止まっている光の反射)
    const float topR = R * 0.86f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4f58), c.x, c.y - topR, juce::Colour (0xff1a1c21), c.x, c.y + topR, false));
    g.fillEllipse (disc (topR));
    for (float rr = topR * 0.42f; rr < topR; rr += 1.3f)
    {
        g.setColour ((((int) (rr * 2.1f)) % 3 == 0 ? juce::Colours::white : juce::Colours::black).withAlpha (0.045f));
        g.drawEllipse (disc (rr), 0.7f);
    }
    for (int k = 0; k < 2; ++k)
    {
        juce::Path w;
        const float a0 = -2.35f + (float) k * juce::MathConstants<float>::pi;
        w.addPieSegment (disc (topR), a0, a0 + 0.55f, 0.45f);
        g.setColour (juce::Colours::white.withAlpha (k == 0 ? 0.09f : 0.04f));
        g.fillPath (w);
    }
    // 回っていることが分かる、くぼみの点
    for (int k = 0; k < 4; ++k)
    {
        const float a = ang + (float) k * twoPi / 4.0f - juce::MathConstants<float>::halfPi;
        const juce::Point<float> p (c.x + std::cos (a) * topR * 0.7f, c.y + std::sin (a) * topR * 0.7f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (p.x - 3.0f, p.y - 2.2f, 6.0f, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillEllipse (p.x - 3.0f, p.y - 3.4f, 6.0f, 5.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (disc (topR), 1.2f);

    // 中央の液晶: 位置を示す目盛り (今の位置だけ光る)
    const float lcdR = R * 0.4f;
    g.setColour (ring.withAlpha (0.12f));
    g.drawEllipse (disc (lcdR + 4.0f), 6.0f);
    g.setColour (ring.withAlpha (0.7f));
    g.drawEllipse (disc (lcdR + 3.0f), 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillEllipse (disc (lcdR + 2.0f).translated (0.0f, 1.0f));
    g.setColour (juce::Colour (0xff050608));
    g.fillEllipse (disc (lcdR + 1.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e1520), c.x, c.y - lcdR, juce::Colour (0xff05070a), c.x, c.y + lcdR, false));
    g.fillEllipse (disc (lcdR));
    const int segs = 36;
    float pa = std::fmod (ang, twoPi);
    if (pa < 0.0f)
        pa += twoPi;
    const int cur = (int) (pa / twoPi * (float) segs) % segs;
    for (int i = 0; i < segs; ++i)
    {
        const float a = (float) i * twoPi / (float) segs - juce::MathConstants<float>::halfPi;
        const int dist = std::min ((i - cur + segs) % segs, (cur - i + segs) % segs);
        const float lv = dist == 0 ? 1.0f : (dist == 1 ? 0.45f : 0.12f);
        g.setColour (ring.withAlpha (lv));
        g.drawLine (c.x + std::cos (a) * lcdR * 0.78f, c.y + std::sin (a) * lcdR * 0.78f,
                    c.x + std::cos (a) * lcdR * 0.93f, c.y + std::sin (a) * lcdR * 0.93f, dist == 0 ? 3.0f : 2.0f);
    }
    g.setFont (fontOf (lcdR * 0.32f, true, 0.05f));
    g.setColour (ring.brighter (0.3f));
    g.drawText (note >= 0 ? noteName (note) : juce::String ("--"), disc (lcdR * 0.7f).translated (0.0f, -lcdR * 0.08f), juce::Justification::centred);
    g.setFont (fontOf (lcdR * 0.16f, true, 0.15f));
    g.setColour (ring.withAlpha (0.6f));
    g.drawText (dragging ? "SCRATCH" : (active ? "PLAY" : "CUE"), disc (lcdR * 0.7f).translated (0.0f, lcdR * 0.3f), juce::Justification::centred);
    {
        juce::Path gl;
        gl.addPieSegment (disc (lcdR), -1.9f, 1.9f, 0.0f);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (juce::Rectangle<float> (c.x - lcdR, c.y - lcdR, lcdR * 2.0f, lcdR * 0.9f).toNearestInt());
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.08f), c.x, c.y - lcdR, juce::Colours::transparentWhite, c.x, c.y, false));
        g.fillEllipse (disc (lcdR));
    }

    // 説明
    g.setFont (fontOf (japanese ? 11.5f : 10.5f, true, 0.12f));
    g.setColour (dragging || note >= 0 ? deck::ledAmber : mutedCol);
    auto cap = getLocalBounds().toFloat().removeFromBottom (20.0f);
    juce::String text;
    if (dragging)
        text = u8 ("◀  ▶");
    else if (note >= 0)
        text = u8 ("♪ ") + noteName (note);
    else
        text = u8 (japanese ? "ノートを置くか、レコードをドラッグ" : "PLAY NOTES OR DRAG THE RECORD");
    g.drawText (text, cap, juce::Justification::centred);
}

//==============================================================================
void Tile::paint (juce::Graphics& g)
{
    const bool sel = getSelected && getSelected() == idx;
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    deck::drawPad (g, r, sel, hover, false);
    r = r.reduced (1.0f, 2.0f);

    auto labelArea = r.removeFromBottom (17.0f);
    if (drawIcon)
        drawIcon (g, r.reduced (10.0f, 6.0f), idx, sel);

    g.setFont (fontOf (hasWide (label) ? 11.0f : 10.0f, true, 0.1f));
    g.setColour (sel ? deck::pick (juce::Colour (0xffffe2b8), deck::whiteTeal.darker (0.25f))
                     : deck::pick (juce::Colour (0xffaeb4bf), deck::whiteInk.withAlpha (0.8f)));
    g.drawText (label, labelArea.translated (0.0f, -2.0f), juce::Justification::centred);
}

//==============================================================================
bool SourceView::sampleShown() const
{
    return (int) proc.apvts.getRawParameterValue ("src")->load() == ScratchProcessor::srcSample && proc.getSampleSeconds() > 0.0;
}

juce::Rectangle<float> SourceView::stripArea() const
{
    return getLocalBounds().toFloat().reduced (4.0f, 2.0f).removeFromBottom (10.0f);
}

void SourceView::setView (double start, double length)
{
    const double seconds = proc.getSampleSeconds();
    const double shortest = seconds > 0.0 ? juce::jmin (1.0, 0.1 / seconds) : 1.0; // 一番拡大して 0.1 秒
    viewLength = juce::jlimit (shortest, 1.0, length);
    viewStart = juce::jlimit (0.0, 1.0 - viewLength, start);
    repaint();
}

void SourceView::setFrom (const juce::MouseEvent& e)
{
    if (! sampleShown())
        return;
    const auto plot = getLocalBounds().toFloat().reduced (4.0f, 0.0f);
    const double x = juce::jlimit (0.0, 1.0, (double) (e.position.x - plot.getX()) / plot.getWidth());
    if (onSetStart)
        onSetStart ((float) (viewStart + x * viewLength));
}

void SourceView::panFrom (const juce::MouseEvent& e)
{
    const auto strip = stripArea();
    setView ((double) (e.position.x - strip.getX()) / strip.getWidth() - viewLength * 0.5, viewLength);
}

void SourceView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        setView (0.0, 1.0);
        return;
    }
    draggingStrip = sampleShown() && zoomed() && stripArea().expanded (0.0f, 2.0f).contains (e.position);
    if (draggingStrip)
        panFrom (e);
    else
        setFrom (e);
}

void SourceView::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    if (draggingStrip)
        panFrom (e);
    else
        setFrom (e);
}

void SourceView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! sampleShown())
        return;
    const auto plot = plotArea();
    const double x = juce::jlimit (0.0, 1.0, (double) (e.position.x - plot.getX()) / plot.getWidth());
    const double at = viewStart + x * viewLength;                // マウスの下の位置は動かさない
    const double delta = (double) (wheel.isReversed ? -wheel.deltaY : wheel.deltaY);
    const double length = juce::jlimit (1.0e-4, 1.0, viewLength * std::exp (-delta * 1.5));
    const double shortest = juce::jmin (1.0, 0.1 / proc.getSampleSeconds());
    const double next = juce::jmax (length, shortest);
    setView (at - x * next, next);
}

void SourceView::refreshPaths (juce::Rectangle<float> plot, juce::Rectangle<float> strip, float pixelScale)
{
    auto latest = proc.getWaveOverview();
    if (latest != wave)
    {
        if (wave != nullptr) // 別のサンプルに変わったら、全体表示に戻す
        {
            viewStart = 0.0;
            viewLength = 1.0;
        }
        wave = std::move (latest);
        pathStart = -1.0;
    }
    if (wave == nullptr || (pathStart == viewStart && pathLength == viewLength && pathPlot == plot && pathStrip == strip && pathScale == pixelScale))
        return;

    // 上下に別々の線を引いて、間を塗る (実際の波形の上下の違いも見える)
    auto build = [this] (juce::Path& peak, juce::Path* rms, juce::Rectangle<float> area, double start, double length, int columns)
    {
        std::vector<float> top ((size_t) columns), bottom ((size_t) columns), rmsSize ((size_t) columns);
        const float centre = area.getCentreY(), half = area.getHeight() * 0.5f;
        const float minHalf = 0.5f;
        float prevLo = 0.0f, prevHi = 0.0f;
        for (int i = 0; i < columns; ++i)
        {
            const double a = start + length * (double) i / (double) columns;
            const double b = start + length * (double) (i + 1) / (double) columns;
            auto c = wave->column (a, b);
            if (i > 0) // 拡大した時に、線が途切れないように隣とつなぐ
            {
                c.lo = juce::jmin (c.lo, prevHi);
                c.hi = juce::jmax (c.hi, prevLo);
            }
            prevLo = c.lo;
            prevHi = c.hi;
            top[(size_t) i] = centre - juce::jmax (minHalf, c.hi * half);
            bottom[(size_t) i] = centre + juce::jmax (minHalf, -c.lo * half);
            rmsSize[(size_t) i] = juce::jmax (minHalf, juce::jmin (c.rms, juce::jmax (c.hi, -c.lo)) * half);
        }

        const auto xAt = [&] (int i) { return area.getX() + area.getWidth() * (float) i / (float) (columns - 1); };
        auto outline = [&] (juce::Path& p, auto upper, auto lower)
        {
            p.clear();
            p.preallocateSpace (columns * 6 + 8);
            p.startNewSubPath (xAt (0), upper (0));
            for (int i = 1; i < columns; ++i)
                p.lineTo (xAt (i), upper (i));
            for (int i = columns - 1; i >= 0; --i)
                p.lineTo (xAt (i), lower (i));
            p.closeSubPath();
        };
        outline (peak, [&] (int i) { return top[(size_t) i]; }, [&] (int i) { return bottom[(size_t) i]; });
        if (rms != nullptr)
            outline (*rms, [&] (int i) { return centre - rmsSize[(size_t) i]; }, [&] (int i) { return centre + rmsSize[(size_t) i]; });
    };

    build (peakPath, &rmsPath, plot, viewStart, viewLength, juce::jmax (2, juce::roundToInt (plot.getWidth() * pixelScale)));
    build (stripPath, nullptr, strip, 0.0, 1.0, juce::jmax (2, juce::roundToInt (strip.getWidth() * pixelScale)));

    pathStart = viewStart;
    pathLength = viewLength;
    pathPlot = plot;
    pathStrip = strip;
    pathScale = pixelScale;
}

void SourceView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    deck::drawLcd (g, r);

    const int src = (int) proc.apvts.getRawParameterValue ("src")->load();
    const bool active = proc.uiActive.load();
    const double pos = proc.uiPos.load();
    auto plot = plotArea();
    const auto waveInk = deck::lcdBlue;
    const auto playInk = deck::pick (orange, deck::screenPurple);

    g.setFont (fontOf (japanese ? 11.0f : 10.0f, true, 0.08f));

    if (src == ScratchProcessor::srcSample)
    {
        const double len = proc.getSampleSeconds();
        if (len <= 0.0)
        {
            g.setColour (mutedCol);
            g.drawText (u8 (japanese ? "サンプルをここにドロップ、または「読み込む」" : "DROP A SAMPLE HERE, OR USE LOAD"), r, juce::Justification::centred);
            return;
        }

        const auto strip = stripArea();
        refreshPaths (plot, strip, juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor()));

        const auto xOf = [&] (double fraction) { return plot.getX() + plot.getWidth() * (float) ((fraction - viewStart) / viewLength); };
        const float start = proc.apvts.getRawParameterValue ("start")->load();
        const double depthSec = 0.001 * proc.apvts.getRawParameterValue ("depth")->load();
        const float x0 = xOf ((double) start);
        const float x1 = xOf ((double) start + depthSec / len);
        const auto range = juce::Rectangle<float>::leftTopRightBottom (juce::jmax (plot.getX(), x0), plot.getY(),
                                                                       juce::jmin (plot.getRight(), juce::jmax (x1, x0 + 2.0f)), plot.getBottom());

        g.setColour (waveInk.withAlpha (0.12f));
        g.fillRect (plot.getX(), plot.getCentreY() - 0.5f, plot.getWidth(), 1.0f);
        g.setColour (waveInk.withAlpha (0.38f));
        g.fillPath (peakPath);
        g.setColour (waveInk.withAlpha (0.82f));
        g.fillPath (rmsPath);

        if (! range.isEmpty())
        {
            g.setColour (playInk.withAlpha (0.12f));
            g.fillRect (range);

            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (range.getSmallestIntegerContainer());
            g.setColour (playInk.withAlpha (0.5f));
            g.fillPath (peakPath);
            g.setColour (playInk);
            g.fillPath (rmsPath);
        }

        // 頭出し位置 (拡大して外に出ている時は描かない)
        if (x0 >= plot.getX() - 1.0f && x0 <= plot.getRight() + 1.0f)
        {
            g.setColour (juce::Colours::white);
            g.fillRect (x0 - 1.0f, r.getY() + 3.0f, 2.0f, r.getHeight() - 6.0f);
            juce::Path tri;
            tri.addTriangle (x0 - 5.0f, r.getY() + 3.0f, x0 + 5.0f, r.getY() + 3.0f, x0, r.getY() + 10.0f);
            g.fillPath (tri);
        }

        if (active)
        {
            const float xp = xOf (juce::jlimit (0.0, 1.0, pos / len));
            if (xp >= plot.getX() && xp <= plot.getRight())
            {
                g.setColour (playInk);
                g.fillRect (xp - 1.0f, plot.getY() - 4.0f, 2.0f, plot.getHeight() + 8.0f);
            }
        }

        g.setColour (textCol);
        g.drawText (proc.getSampleName(), r.reduced (10.0f, 1.0f).removeFromTop (13.0f), juce::Justification::centredLeft);
        g.setColour (mutedCol);
        const auto zoomText = zoomed() ? juce::String ((1.0 / viewLength), viewLength > 0.1 ? 1 : 0) + "x   " : juce::String();
        g.drawText (zoomText + juce::String (len * viewLength, 2) + " s", r.reduced (10.0f, 1.0f).removeFromTop (13.0f), juce::Justification::centredRight);

        if (zoomed())
        {
            // 全体の中で、どこを見ているか
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRect (strip);
            g.setColour (waveInk.withAlpha (0.45f));
            g.fillPath (stripPath);

            const auto seen = juce::Rectangle<float> (strip.getX() + strip.getWidth() * (float) viewStart, strip.getY(),
                                                      juce::jmax (3.0f, strip.getWidth() * (float) viewLength), strip.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.14f));
            g.fillRect (seen);
            g.setColour (juce::Colours::white.withAlpha (0.65f));
            g.drawRect (seen, 1.0f);
            g.setColour (playInk);
            g.fillRect (strip.getX() + strip.getWidth() * start - 0.5f, strip.getY(), 1.0f, strip.getHeight());
        }
        else
        {
            g.drawText (u8 (japanese ? "クリックで頭出し ・ ホイールで拡大" : "CLICK TO SET CUE  /  WHEEL TO ZOOM"),
                        r.reduced (10.0f, 1.0f).removeFromBottom (13.0f), juce::Justification::centredRight);
        }
        return;
    }

    // 波形の音源: 形を4周期ぶん描き、レコードの位置を示す
    juce::Path p;
    const int steps = (int) plot.getWidth();
    for (int i = 0; i <= steps; ++i)
    {
        const double cyc = 4.0 * (double) i / (double) steps;
        const float v = src == ScratchProcessor::srcNoise ? pseudoNoise (i / 2) * 0.85f : ScratchProcessor::shapeValue (src, cyc) * 0.9f;
        const float x = plot.getX() + (float) i;
        const float y = plot.getCentreY() - v * plot.getHeight() * 0.5f;
        if (i == 0)
            p.startNewSubPath (x, y);
        else
            p.lineTo (x, y);
    }
    g.setColour (waveInk.withAlpha (0.25f));
    g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (waveInk);
    g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (active)
    {
        const double cyc = pos * ScratchProcessor::baseFreq;
        const double frac = (cyc / 4.0) - std::floor (cyc / 4.0);
        const float xp = plot.getX() + plot.getWidth() * (float) frac;
        g.setColour (playInk);
        g.fillRect (xp - 1.0f, plot.getY() - 4.0f, 2.0f, plot.getHeight() + 8.0f);
    }

    g.setColour (mutedCol);
    g.drawText (u8 (japanese ? "C5 で元の高さ (261.6 Hz)" : "C5 = ORIGINAL PITCH (261.6 Hz)"), r.reduced (10.0f, 1.0f).removeFromBottom (13.0f),
                juce::Justification::centredRight);
}

//==============================================================================
juce::Rectangle<float> FillTimeline::laneArea() const
{
    return getLocalBounds().toFloat().reduced (8.0f, 4.0f).withTrimmedTop (16.0f);
}

int FillTimeline::totalTicks() const
{
    return proc.getPhrase().bars * ScratchProcessor::ticksPerBar;
}

float FillTimeline::tickWidth() const
{
    return laneArea().getWidth() / (float) totalTicks();
}

float FillTimeline::tickAtX (float x) const
{
    return (x - laneArea().getX()) / tickWidth();
}

int FillTimeline::eventAt (juce::Point<float> pt) const
{
    const auto& ph = proc.getPhrase();
    if (! laneArea().contains (pt))
        return -1;
    const float t = tickAtX (pt.x);
    for (int i = 0; i < (int) ph.events.size(); ++i)
    {
        const auto& e = ph.events[(size_t) i];
        if (t >= (float) e.start && t < (float) (e.start + e.len))
            return i;
    }
    return -1;
}

int FillTimeline::numCells() const
{
    return juce::jmax (1, totalTicks() / gridTicks);
}

int FillTimeline::cellAtX (float x) const
{
    return juce::jlimit (0, numCells() - 1, (int) std::floor (tickAtX (x) / (float) gridTicks));
}

bool FillTimeline::cellFree (int cell) const
{
    const int a = cell * gridTicks, b = a + gridTicks;
    for (auto& e : proc.getPhrase().events)
        if (a < e.start + e.len && e.start < b)
            return false;
    return true;
}

bool FillTimeline::onGateEdge (int eventIndex, juce::Point<float> pt) const
{
    const auto& ph = proc.getPhrase();
    if (muteTool || eventIndex < 0 || eventIndex >= (int) ph.events.size())
        return false;
    const auto& e = ph.events[(size_t) eventIndex];
    const float tw = tickWidth();
    const float x0 = laneArea().getX() + (float) e.start * tw;
    const float edge = x0 + (float) e.len * tw * e.gate;
    const float zone = juce::jmin (9.0f, 0.4f * (float) e.len * tw * e.gate);
    return pt.x >= edge - zone - 1.0f && pt.x <= edge + 3.0f;
}

bool FillTimeline::onStartEdge (int eventIndex, juce::Point<float> pt) const
{
    const auto& ph = proc.getPhrase();
    if (muteTool || eventIndex < 0 || eventIndex >= (int) ph.events.size())
        return false;
    const auto& e = ph.events[(size_t) eventIndex];
    const float tw = tickWidth();
    const float x0 = laneArea().getX() + (float) e.start * tw;
    const float zone = juce::jmin (9.0f, 0.3f * (float) e.len * tw * e.gate);
    return pt.x >= x0 - 3.0f && pt.x <= x0 + zone;
}

FillTimeline::BlockGeom FillTimeline::blockGeom (int i) const
{
    BlockGeom gm;
    const auto& ph = proc.getPhrase();
    if (i < 0 || i >= (int) ph.events.size())
        return gm;
    const auto& e = ph.events[(size_t) i];
    const auto lane = laneArea();
    const float tw = tickWidth();
    const float x0 = lane.getX() + (float) e.start * tw + 1.0f;
    const float slotW = juce::jmax (3.0f, (float) e.len * tw - 2.0f);
    const float audW = juce::jmax (3.0f, slotW * e.gate);
    const auto box = juce::Rectangle<float> (x0, lane.getY() + 2.0f, audW, lane.getHeight() - 4.0f);
    gm.area = box.reduced (3.0f, 5.0f).withTrimmedBottom (12.0f);
    const double cycles = (double) e.len * (double) e.gate / (double) ScratchProcessor::ticksPerBeat / ScratchProcessor::rateBeats (e.rate);
    gm.cycleW = cycles > 0.0 ? gm.area.getWidth() / (float) cycles : 0.0f;
    gm.depthScale = e.depth / 1.5f;
    gm.apex = proc.patternApex (e.pattern);
    gm.at = e.apexAt >= 0.0f ? e.apexAt : gm.apex;
    gm.ok = gm.cycleW >= 36.0f && gm.area.getWidth() >= 30.0f && gm.area.getHeight() >= 20.0f;
    return gm;
}

juce::Point<float> FillTimeline::curvePoint (int i, const BlockGeom& gm, float phi) const
{
    const auto& e = proc.getPhrase().events[(size_t) i];
    const float w = ScratchProcessor::warpEvent (e, phi, gm.apex);
    return { gm.area.getX() + phi * gm.cycleW, gm.area.getBottom() - gm.area.getHeight() * proc.shapeD (e.pattern, juce::jmin (w, 0.99999f)) * gm.depthScale };
}

float FillTimeline::audibleEnd (int i, const BlockGeom& gm, float tail) const
{
    const auto& e = proc.getPhrase().events[(size_t) i];
    if (tail < 1.0f && tail > proc.naturalEnd (e))
        return tail; // 伸ばした
    float last = 0.0f;
    for (int k = 0; k < 200; ++k)
    {
        const float phi = ((float) k + 0.5f) / 200.0f;
        if (phi < tail && proc.shapeGate (e.pattern, ScratchProcessor::warpEvent (e, phi, gm.apex)))
            last = (float) (k + 1) / 200.0f;
    }
    return juce::jmin (last, tail);
}

int FillTimeline::handleAt (int i, juce::Point<float> pt) const
{
    if (muteTool || i < 0 || i >= (int) proc.getPhrase().events.size())
        return 0;
    const auto gm = blockGeom (i);
    if (! gm.ok)
        return 0;
    const auto& e = proc.getPhrase().events[(size_t) i];
    const float phis[4] = { gm.at * 0.5f, gm.at + (1.0f - gm.at) * 0.5f, audibleEnd (i, gm, e.tail), gm.at };
    for (int k = 0; k < 4; ++k)
    {
        const auto h = curvePoint (i, gm, phis[k]);
        if (h.x <= gm.area.getRight() + 1.0f && h.getDistanceFrom (pt) < 7.0f)
            return k + 1;
    }
    return 0;
}

void FillTimeline::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::none;
    dragEvent = eventAt (e.position);
    dragMoved = false;
    editStarted = false;

    if (! laneArea().contains (e.position))
    {
        dragEvent = -1;
        return;
    }

    // 右クリック: ブロックのメニュー / 空いているマスなら、追加のメニュー
    if (e.mods.isPopupMenu())
    {
        const int ev = dragEvent;
        dragEvent = -1;
        if (ev >= 0)
            showBlockMenu (ev);
        else
            showAddMenu (cellAtX (e.position.x), cellAtX (e.position.x));
        return;
    }

    // ミュートツール: ブロックをなぞって、フェーダーを閉じる (ミュートした所から始めると、元に戻す)
    if (muteTool)
    {
        if (dragEvent < 0)
            return;
        const auto& ev = proc.getPhrase().events[(size_t) dragEvent];
        muteBase = ev.mute;
        muteAnchorTick = tickAtX (e.position.x) - (float) ev.start;
        const int rel = juce::jlimit (0, ev.len - 1, (int) std::floor (muteAnchorTick));
        mutePaint = ! ev.mute[(size_t) rel];
        drag = Drag::mute;
        proc.beginEdit();
        editStarted = true;
        paintMute (e.position.x, e.mods.isShiftDown());
        return;
    }

    // 空いているマス: 範囲選択を始める
    if (dragEvent < 0)
    {
        const int c = cellAtX (e.position.x);
        if (! cellFree (c))
            return;
        drag = Drag::select;
        selAnchor = selLo = selHi = c;
        hoverCell = -1;
        repaint();
        return;
    }

    // ブロックの上のつまみ: カーブの曲がり具合 (◇) / 鳴らす終わり (▼) / 頂点の位置 (●)
    if (const int hh = handleAt (dragEvent, e.position); hh > 0)
    {
        const auto& ev = proc.getPhrase().events[(size_t) dragEvent];
        drag = hh == 1 ? Drag::bendRise : (hh == 2 ? Drag::bendFall : (hh == 3 ? Drag::tail : Drag::apex));
        dragStartY = e.position.y;
        dragStartBend = hh == 1 ? ev.bendRise : ev.bendFall;
        return;
    }

    drag = onGateEdge (dragEvent, e.position) ? Drag::gate : (onStartEdge (dragEvent, e.position) ? Drag::start : Drag::pending);
    dragStartY = e.position.y;
    dragStartX = e.position.x;
    grabOffset = tickAtX (e.position.x) - (float) proc.getPhrase().events[(size_t) dragEvent].start;
    dragStartDepth = proc.getPhrase().events[(size_t) dragEvent].depth;
}

// ミュートのドラッグ: なぞり始めから今の位置までを、マス目単位で塗る (Shift でティック単位)
void FillTimeline::paintMute (float x, bool fine)
{
    const auto& ph = proc.getPhrase();
    if (dragEvent < 0 || dragEvent >= (int) ph.events.size())
        return;
    const auto& ev = ph.events[(size_t) dragEvent];
    const float t = tickAtX (x) - (float) ev.start;
    const float a = juce::jmax (0.0f, juce::jmin (muteAnchorTick, t));
    const float b = juce::jmax (0.0f, juce::jmax (muteAnchorTick, t));
    int lo, hi;
    if (fine)
    {
        lo = (int) std::floor (a);
        hi = (int) std::floor (b) + 1;
    }
    else
    {
        const int g = gridTicks;
        const int absA = ev.start + (int) std::floor (a), absB = ev.start + (int) std::floor (b);
        lo = (absA / g) * g - ev.start;
        hi = (absB / g + 1) * g - ev.start;
    }
    lo = juce::jlimit (0, ev.len, lo);
    hi = juce::jlimit (0, ev.len, hi);
    auto m = muteBase;
    for (int i = lo; i < hi; ++i)
        m.set ((size_t) i, mutePaint);
    if (m != ev.mute)
        proc.setEventMuteMask (dragEvent, m);
    repaint();
}

void FillTimeline::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::mute)
    {
        paintMute (e.position.x, e.mods.isShiftDown());
        return;
    }

    if (drag == Drag::select)
    {
        // 空いているマスだけを、ブロックに当たるまで広げる
        const int t = cellAtX (e.position.x);
        int lo = selAnchor, hi = selAnchor;
        while (hi < t && cellFree (hi + 1))
            ++hi;
        while (lo > t && cellFree (lo - 1))
            --lo;
        if (lo != selLo || hi != selHi)
        {
            selLo = lo;
            selHi = hi;
            repaint();
        }
        return;
    }

    if (dragEvent < 0 || dragEvent >= (int) proc.getPhrase().events.size() || drag == Drag::none)
        return;

    // 押しただけの状態から、動かした向きで決める: 横 = 移動、縦 = 振り幅
    if (drag == Drag::pending)
    {
        const float dx = e.position.x - dragStartX, dy = e.position.y - dragStartY;
        if (std::abs (dx) < 4.0f && std::abs (dy) < 4.0f)
            return; // まだ「クリック」の範囲
        drag = std::abs (dx) > std::abs (dy) ? Drag::move : Drag::depth;
        if (drag == Drag::move)
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    const auto& ph = proc.getPhrase();
    const auto& ev = ph.events[(size_t) dragEvent];
    const int g = gridTicks;
    auto startEdit = [this]
    {
        if (! editStarted)
        {
            proc.beginEdit();
            editStarted = true;
        }
    };

    if (drag == Drag::bendRise || drag == Drag::bendFall)
    {
        // 上にドラッグすると、カーブがふくらむ (上り: 早く上がる / 下り: ゆっくり下がる)
        const float up = (dragStartY - e.position.y) / 60.0f;
        float rise = ev.bendRise, fall = ev.bendFall;
        if (drag == Drag::bendRise)
            rise = dragStartBend - up;
        else
            fall = dragStartBend + up;
        if (std::abs (rise - ev.bendRise) > 0.002f || std::abs (fall - ev.bendFall) > 0.002f)
        {
            startEdit();
            proc.setEventBend (dragEvent, rise, fall);
        }
        repaint();
        return;
    }

    if (drag == Drag::apex)
    {
        // 左右へ: 頂点を置く位置。パターンの元の位置と、真ん中 (50%) の近くでは吸い付く
        const auto gm = blockGeom (dragEvent);
        if (gm.cycleW <= 0.0f)
            return;
        float at = juce::jlimit (0.05f, 0.95f, (e.position.x - gm.area.getX()) / gm.cycleW);
        if (std::abs (at - gm.apex) < 0.02f)
            at = -1.0f;
        else if (std::abs (at - 0.5f) < 0.02f)
            at = 0.5f;
        if (std::abs (at - ev.apexAt) > 0.002f)
        {
            startEdit();
            proc.setEventApex (dragEvent, at);
        }
        repaint();
        return;
    }

    if (drag == Drag::tail)
    {
        // 左へ: 鳴らす終わりを短く / 右へ: 伸ばす。元の終わりの近くでは、元のまま (吸い付く)
        const auto gm = blockGeom (dragEvent);
        if (gm.cycleW <= 0.0f)
            return;
        const float phi = (e.position.x - gm.area.getX()) / gm.cycleW;
        const float orig = proc.naturalEnd (ev);
        const bool same = std::abs (phi - orig) < 0.012f || (phi > orig && orig > 0.985f);
        const float t = same ? 1.0f : juce::jlimit (0.03f, 0.99f, phi);
        if (std::abs (t - ev.tail) > 0.002f)
        {
            startEdit();
            proc.setEventTail (dragEvent, t);
        }
        repaint();
        return;
    }

    if (drag == Drag::move)
    {
        // マス目に合わせて動かす
        const int target = juce::roundToInt ((tickAtX (e.position.x) - grabOffset) / (float) g) * g;
        if (target != ev.start && proc.canPlace (dragEvent, target))
        {
            dragMoved = true;
            startEdit();
            dragEvent = proc.moveEvent (dragEvent, target);
            hoverEvent = dragEvent;
            repaint();
        }
        return;
    }

    if (drag == Drag::start)
    {
        const int target = juce::roundToInt (tickAtX (e.position.x) / (float) g) * g;
        if (target != ev.start)
        {
            dragMoved = true;
            startEdit();
            proc.setEventStart (dragEvent, target);
            repaint();
        }
        return;
    }

    if (drag == Drag::gate)
    {
        // 鳴らす長さ = ブロックの長さ。縮めると後ろが空き、空きへは伸ばせる。マス目の線の近くでは、線に吸着
        float absEnd = tickAtX (e.position.x);
        const float nearest = std::round (absEnd / (float) g) * (float) g;
        if (nearest > (float) ev.start && std::abs (absEnd - nearest) < 0.25f * (float) g)
            absEnd = nearest;
        const float audible = juce::jmax (1.5f, absEnd - (float) ev.start);
        if (std::abs (audible - (float) ev.len * ev.gate) < 0.02f)
            return;
        dragMoved = true;
        startEdit();
        proc.setEventEnd (dragEvent, audible);
    }
    else // 振り幅
    {
        dragMoved = true;
        const float depth = juce::jlimit (0.2f, 1.5f, dragStartDepth + (dragStartY - e.position.y) * 0.011f);
        if (std::abs (depth - ev.depth) < 0.002f)
            return;
        startEdit();
        proc.setEventDepth (dragEvent, depth);
    }
    repaint();
}

void FillTimeline::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int i = eventAt (e.position);
    if (i < 0 || muteTool)
        return;
    wheelAccum += w.deltaY;
    if (std::abs (wheelAccum) < 0.12f)
        return;
    proc.cycleEventPattern (i, wheelAccum > 0.0f ? -1 : 1); // 上 = 前のパターン、下 = 次
    wheelAccum = 0.0f;
    hoverEvent = i;
    repaint();
}

void FillTimeline::mouseUp (const juce::MouseEvent& e)
{
    // 範囲を選び終えたら、追加するパターンを選ぶメニュー
    if (drag == Drag::select)
    {
        drag = Drag::none;
        showAddMenu (selLo, selHi);
        return;
    }

    // 動かさずに離したら、クリック: パターンを切り替える
    if (drag == Drag::pending && dragEvent >= 0)
        proc.cycleEventPattern (dragEvent, e.mods.isShiftDown() ? -1 : 1); // Shift+クリック = 前のパターン
    if (drag == Drag::move)
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    drag = Drag::none;
    dragEvent = -1;
    repaint();
}

void FillTimeline::mouseMove (const juce::MouseEvent& e)
{
    const int i = eventAt (e.position);
    const int hh = i >= 0 ? handleAt (i, e.position) : 0;
    const bool edge = i >= 0 && hh == 0 && onGateEdge (i, e.position);
    const bool startEdge = i >= 0 && hh == 0 && ! edge && onStartEdge (i, e.position);
    int c = -1;
    if (i < 0 && ! muteTool && laneArea().contains (e.position))
    {
        c = cellAtX (e.position.x);
        if (! cellFree (c))
            c = -1;
    }
    if (i != hoverEvent || edge != hoverEdge || startEdge != hoverStartEdge || c != hoverCell || hh != hoverHandle)
    {
        hoverEvent = i;
        hoverEdge = edge;
        hoverStartEdge = startEdge;
        hoverCell = c;
        hoverHandle = hh;
        if (muteTool)
            setMouseCursor (i >= 0 ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
        else if (hh > 0)
            setMouseCursor (hh >= 3 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
        else
            setMouseCursor (edge || startEdge ? juce::MouseCursor::LeftRightResizeCursor
                                              : (i >= 0 ? juce::MouseCursor::PointingHandCursor
                                                        : (c >= 0 ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor)));
        repaint();
    }
}

void FillTimeline::showAddMenu (int lo, int hi)
{
    if (lo < 0 || hi < lo || ! cellFree (lo))
    {
        selLo = selHi = -1;
        repaint();
        return;
    }
    selLo = lo;
    selHi = hi;
    repaint();

    const int start = lo * gridTicks;
    const int len = (hi - lo + 1) * gridTicks;
    const int rate = newRate >= 0 ? newRate : ScratchProcessor::autoRate (len);
    const int version = proc.phraseChanged.load();
    const int n = hi - lo + 1;

    juce::PopupMenu m;
    m.setLookAndFeel (&getLookAndFeel());
    m.addSectionHeader (u8 (japanese ? "追加するパターン (" : "ADD A PATTERN (") + juce::String (n) + " × " + gridName (gridTicks) + ")");
    for (int p = 0; p < ScratchProcessor::numPresetPatterns; ++p)
    {
        juce::PopupMenu::Item it (u8 (kPatternNames[p][japanese ? 1 : 0]));
        it.itemID = 1 + p;
        it.image = patternIcon (proc, p);
        m.addItem (std::move (it));
    }
    m.addColumnBreak();
    m.addSectionHeader (u8 (japanese ? "ユーザー波形" : "USER"));
    for (int p = ScratchProcessor::numPresetPatterns; p < ScratchProcessor::numPatterns; ++p)
    {
        juce::PopupMenu::Item it (u8 (kPatternNames[p][japanese ? 1 : 0]));
        it.itemID = 1 + p;
        it.image = patternIcon (proc, p);
        m.addItem (std::move (it));
    }
    m.addColumnBreak();
    m.addSectionHeader (u8 (japanese ? "速さ" : "RATE"));
    m.addItem (100, u8 (japanese ? "自動 (" : "AUTO (") + kRateNames[ScratchProcessor::autoRate (len)] + ")", true, newRate < 0);
    for (int r = 0; r < ScratchProcessor::numRates; ++r)
    {
        if (r == 5)
            m.addSeparator();
        m.addItem (101 + r, kRateNames[r], true, newRate == r);
    }

    juce::Component::SafePointer<FillTimeline> safe (this);
    const auto mouse = getMouseXYRelative();
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> (mouse, mouse).expanded (1))),
                     [safe, lo, hi, start, len, rate, version] (int result)
                     {
                         if (safe == nullptr)
                             return;
                         auto& self = *safe;
                         if (result >= 100)
                         {
                             // 速さを選んだら、そのままもう一度メニューを出す (続けてパターンを選べる)
                             self.newRate = result == 100 ? -1 : result - 101;
                             juce::MessageManager::callAsync ([safe, lo, hi]
                                                              {
                                                                  if (safe != nullptr)
                                                                      safe->showAddMenu (lo, hi);
                                                              });
                             return;
                         }
                         if (result >= 1 && self.proc.phraseChanged.load() == version)
                             self.proc.addEvent (start, len, result - 1, rate);
                         self.selLo = self.selHi = -1;
                         self.repaint();
                     });
}

void FillTimeline::showBlockMenu (int eventIndex)
{
    const auto& ph = proc.getPhrase();
    if (eventIndex < 0 || eventIndex >= (int) ph.events.size())
        return;
    const auto ev = ph.events[(size_t) eventIndex];
    const int version = proc.phraseChanged.load();
    const bool isUser = ev.pattern >= ScratchProcessor::numPresetPatterns;
    hoverEvent = eventIndex;
    repaint();

    juce::PopupMenu pat, rate, pitch, copyTo;
    for (int p = 0; p < ScratchProcessor::numPatterns; ++p)
    {
        if (p == ScratchProcessor::numPresetPatterns)
            pat.addSeparator();
        juce::PopupMenu::Item it (u8 (kPatternNames[p][japanese ? 1 : 0]));
        it.itemID = 1 + p;
        it.isTicked = ev.pattern == p;
        it.image = patternIcon (proc, p);
        pat.addItem (std::move (it));
    }
    for (int r = 0; r < ScratchProcessor::numRates; ++r)
    {
        if (r == 5)
            rate.addSeparator();
        rate.addItem (21 + r, kRateNames[r], true, ev.rate == r);
    }
    static const int pitches[] = { 12, 7, 5, 3, 2, 0, -2, -3, -5, -7, -12 };
    for (int k = 0; k < (int) std::size (pitches); ++k)
        pitch.addItem (41 + k, (pitches[k] > 0 ? "+" : "") + juce::String (pitches[k]), true, ev.pitch == pitches[k]);
    for (int s = 0; s < ScratchProcessor::numUserPatterns; ++s)
        copyTo.addItem (81 + s, u8 (kPatternNames[ScratchProcessor::numPresetPatterns + s][japanese ? 1 : 0]) + u8 (japanese ? " に写して編集" : ": copy here and edit"));

    juce::PopupMenu m;
    m.setLookAndFeel (&getLookAndFeel());
    m.addSectionHeader (u8 (kPatternNames[juce::jlimit (0, ScratchProcessor::numPatterns - 1, ev.pattern)][japanese ? 1 : 0]));
    m.addSubMenu (u8 (japanese ? "パターン" : "Pattern"), pat);
    m.addSubMenu (u8 (japanese ? "速さ" : "Rate"), rate);
    m.addSubMenu (u8 (japanese ? "高さ (半音)" : "Pitch (semitones)"), pitch);
    m.addItem (61, u8 (japanese ? "振り幅・長さを 100% に戻す" : "Reset depth and length to 100%"), ev.depth != 1.0f || ev.gate != 1.0f);
    m.addItem (62, u8 (japanese ? "ミュートを全部戻す" : "Clear all mutes"), ev.mute.any());
    m.addItem (63, u8 (japanese ? "カーブ・頂点・終わりを元に戻す" : "Reset this block's curve, peak and end"),
               ScratchProcessor::isWarped (ev) || ev.tail < 1.0f);
    m.addSeparator();
    m.addItem (90, u8 (japanese ? "この波形を編集… (このパターンを使う全部のブロックに効く)" : "Edit this wave… (affects every block using it)"));
    m.addSubMenu (u8 (japanese ? "ユーザー波形に写して編集…" : "Copy to a user wave and edit…"), copyTo);
    juce::ignoreUnused (isUser);
    m.addSeparator();
    m.addItem (71, u8 (japanese ? "このブロックを削除" : "Delete this block"));

    juce::Component::SafePointer<FillTimeline> safe (this);
    const auto mouse = getMouseXYRelative();
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> (mouse, mouse).expanded (1))),
                     [safe, eventIndex, version, ev] (int result)
                     {
                         if (safe == nullptr)
                             return;
                         auto& self = *safe;
                         auto& pr = self.proc;
                         // メニューを開いている間にフレーズが変わったら、何もしない (別のブロックを変えてしまわないように)
                         if (result > 0 && pr.phraseChanged.load() == version)
                         {
                             if (result <= ScratchProcessor::numPatterns)
                                 pr.setEventPattern (eventIndex, result - 1);
                             else if (result >= 21 && result < 21 + ScratchProcessor::numRates)
                                 pr.setEventRate (eventIndex, result - 21);
                             else if (result >= 41 && result < 41 + (int) std::size (pitches))
                                 pr.setEventPitch (eventIndex, pitches[result - 41]);
                             else if (result == 61)
                                 pr.resetEventShape (eventIndex);
                             else if (result == 63)
                                 pr.resetEventCurve (eventIndex);
                             else if (result == 62)
                                 pr.clearEventMutes (eventIndex);
                             else if (result == 71)
                                 pr.removeEvent (eventIndex);
                             else if (result >= 81 && result < 81 + ScratchProcessor::numUserPatterns)
                             {
                                 // 決まった形のパターンを、ユーザー波形に写して、このブロックで使う
                                 const int slot = result - 81;
                                 pr.copyPresetToUser (slot, ev.pattern);
                                 pr.setEventPattern (eventIndex, ScratchProcessor::numPresetPatterns + slot);
                                 if (self.onEditPattern)
                                     self.onEditPattern (ScratchProcessor::numPresetPatterns + slot);
                             }
                             else if (result == 90 && self.onEditPattern)
                                 self.onEditPattern (ev.pattern);
                         }
                         self.hoverEvent = -1;
                         self.repaint();
                     });
}

void FillTimeline::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    deck::drawLcd (g, r);

    const auto& ph = proc.getPhrase();
    const int total = totalTicks();
    const auto lane = laneArea();
    const float tw = lane.getWidth() / (float) total;
    constexpr int beat = ScratchProcessor::ticksPerBeat;

    // マス目 (空いている所をクリック/ドラッグして、ブロックを追加できる)
    for (int t = gridTicks; t < total; t += gridTicks)
        if (t % beat != 0)
        {
            g.setColour (juce::Colour (0xff1c212b));
            g.fillRect (lane.getX() + (float) t * tw - 0.5f, lane.getY() + 4.0f, 1.0f, lane.getHeight() - 8.0f);
        }

    // 何も置いていないときの案内 (起動時は空なので、鳴らない理由が分かるように)
    if (proc.getPhrase().events.empty() && selLo < 0 && hoverCell < 0)
    {
        g.setColour (mutedCol);
        g.setFont (fontOf (japanese ? 13.0f : 12.0f, false, 0.05f));
        g.drawText (u8 (japanese ? "マスをクリックしてパターンを置くか、上の一覧からテンプレートを選んでください"
                                 : "CLICK A STEP TO PLACE A PATTERN, OR PICK A TEMPLATE ABOVE"),
                    lane, juce::Justification::centred);
    }

    // マウスの下の空いているマス / 選んでいる範囲
    const float cw = (float) gridTicks * tw;
    if (selLo >= 0 && selHi >= selLo)
    {
        const auto sel = juce::Rectangle<float> (lane.getX() + (float) selLo * cw + 1.0f, lane.getY() + 2.0f,
                                                 (float) (selHi - selLo + 1) * cw - 2.0f, lane.getHeight() - 4.0f);
        const auto hot = deck::pick (orange, deck::screenTeal);
        g.setColour (hot.withAlpha (0.16f));
        g.fillRoundedRectangle (sel, 4.0f);
        g.setColour (hot);
        g.drawRoundedRectangle (sel, 4.0f, 1.5f);
        g.setFont (fontOf (14.0f, true));
        g.drawText ("+", sel, juce::Justification::centred);
    }
    else if (hoverCell >= 0)
    {
        const auto cell = juce::Rectangle<float> (lane.getX() + (float) hoverCell * cw + 1.0f, lane.getY() + 2.0f, cw - 2.0f, lane.getHeight() - 4.0f);
        const auto hot = deck::pick (orange, deck::screenTeal);
        g.setColour (hot.withAlpha (0.08f));
        g.fillRoundedRectangle (cell, 3.0f);
        g.setColour (hot.withAlpha (0.55f));
        g.drawRoundedRectangle (cell, 3.0f, 1.0f);
        g.setFont (fontOf (13.0f, true));
        g.drawText ("+", cell, juce::Justification::centred);
    }

    // 拍と小節の線、小節番号
    for (int t = 0; t <= total; t += beat)
    {
        const float x = lane.getX() + (float) t * tw;
        const bool bar = t % ScratchProcessor::ticksPerBar == 0;
        g.setColour (bar ? juce::Colour (0xff3a4152) : juce::Colour (0xff232834));
        g.fillRect (x - (bar ? 1.0f : 0.5f), lane.getY(), bar ? 2.0f : 1.0f, lane.getHeight());
        if (bar && t < total)
        {
            g.setFont (fontOf (9.5f, true, 0.1f));
            g.setColour (mutedCol);
            g.drawText (juce::String (t / ScratchProcessor::ticksPerBar + 1), (int) x + 4, (int) r.getY() + 2, 30, 12, juce::Justification::centredLeft);
        }
    }

    // イベント: 薄い枠 = ブロック全体、濃い部分 = 実際に鳴らす長さ。絵の高さ = 振り幅。斜線 = ミュートした所
    for (int i = 0; i < (int) ph.events.size(); ++i)
    {
        const auto& e = ph.events[(size_t) i];
        const float x0 = lane.getX() + (float) e.start * tw + 1.0f;
        const float slotW = juce::jmax (3.0f, (float) e.len * tw - 2.0f);
        const float audW = juce::jmax (3.0f, slotW * e.gate);
        const auto slot = juce::Rectangle<float> (x0, lane.getY() + 2.0f, slotW, lane.getHeight() - 4.0f);
        const auto box = slot.withWidth (audW);
        const auto col = patternColour (e.pattern);
        const bool hov = i == hoverEvent || (drag != Drag::none && i == dragEvent);

        // 切った部分 (ミュートされる所)
        if (e.gate < 0.995f)
        {
            g.setColour (col.withAlpha (0.06f));
            g.fillRoundedRectangle (slot, 4.0f);
            g.setColour (col.withAlpha (0.22f));
            g.drawRoundedRectangle (slot, 4.0f, 1.0f);
        }

        if (deck::white())
        {
            g.setGradientFill (juce::ColourGradient (col.withAlpha (hov ? 0.38f : 0.26f), 0.0f, box.getY(),
                                                     col.withAlpha (0.04f), 0.0f, box.getBottom(), false));
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (juce::Colours::white.withAlpha (hov ? 0.35f : 0.2f));
            g.fillRoundedRectangle (box.getX() + 3.0f, box.getY() + 1.0f, box.getWidth() - 6.0f, 1.0f, 0.5f);
        }
        else
        {
            g.setColour (col.withAlpha (hov ? 0.30f : 0.18f));
            g.fillRoundedRectangle (box, 4.0f);
        }

        // ミュートした所: 暗くして、斜線
        if (e.mute.any())
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (box.toNearestInt());
            int t = 0;
            while (t < e.len)
            {
                if (! e.mute[(size_t) t])
                {
                    ++t;
                    continue;
                }
                int u = t;
                while (u < e.len && e.mute[(size_t) u])
                    ++u;
                const auto mr = juce::Rectangle<float> (lane.getX() + (float) (e.start + t) * tw, box.getY(), (float) (u - t) * tw, box.getHeight());
                g.setColour (juce::Colour (0xcc0e1117));
                g.fillRect (mr);
                g.setColour (col.withAlpha (0.35f));
                for (float hx = mr.getX() - mr.getHeight(); hx < mr.getRight(); hx += 6.0f)
                    g.drawLine (hx, mr.getBottom(), hx + mr.getHeight(), mr.getY(), 1.0f);
                t = u;
            }
        }

        g.setColour (col.withAlpha (hov ? 1.0f : 0.7f));
        g.drawRoundedRectangle (box, 4.0f, hov ? 2.0f : 1.0f);

        // 動きのカーブ。高さは振り幅 (0.2〜1.5 の全範囲が、枠いっぱいに収まる)
        auto curveArea = box.reduced (3.0f, 5.0f).withTrimmedBottom (12.0f);
        const double audTicks = (double) e.len * (double) e.gate;
        const double cycles = audTicks / (double) beat / ScratchProcessor::rateBeats (e.rate);
        const int n = juce::jmax (8, (int) curveArea.getWidth());
        const float depthScale = e.depth / 1.5f;
        const bool warped = ScratchProcessor::isWarped (e);
        const float apexE = warped ? proc.patternApex (e.pattern) : 0.5f;
        const float natE = e.tail < 1.0f ? proc.naturalEnd (e) : 1.0f; // 鳴らす終わりを伸ばした時は、元の終わりから明るく
        juce::Point<float> prev;
        juce::Path glow; // White: 鳴る線の光
        bool wasOpen = false;
        for (int k = 0; k <= n; ++k)
        {
            const double c = cycles * (double) k / (double) n;
            const float phiRaw = (float) (c - std::floor (c));
            const float phi = warped ? ScratchProcessor::warpEvent (e, phiRaw, apexE) : phiRaw;
            const float d = proc.shapeD (e.pattern, juce::jmin (phi, 0.99999f)) * depthScale;
            const juce::Point<float> pt (curveArea.getX() + curveArea.getWidth() * (float) k / (float) n, curveArea.getBottom() - curveArea.getHeight() * d);
            if (k > 0)
            {
                const int tick = juce::jlimit (0, ScratchProcessor::maxEventTicks - 1, (int) (audTicks * ((double) k - 0.5) / (double) n));
                const double cm = cycles * ((double) k - 0.5) / (double) n;
                const float phiMid = (float) (cm - std::floor (cm));
                const bool held = e.tail > natE && phiMid >= natE;
                const bool open = (held || proc.shapeGate (e.pattern, phi)) && ! e.mute[(size_t) tick] && phiMid < e.tail;
                g.setColour (open ? col : mutedCol.withAlpha (0.3f));
                g.drawLine (prev.x, prev.y, pt.x, pt.y, open ? 1.6f : 1.0f);
                if (open)
                {
                    if (! wasOpen)
                        glow.startNewSubPath (prev);
                    glow.lineTo (pt);
                }
                wasOpen = open;
            }
            prev = pt;
        }
        if (deck::white())
        {
            g.setColour (col.withAlpha (0.22f));
            g.strokePath (glow, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ブロックの上のつまみ (1周目): 始点の印、● = 頂点の位置、◇ = 曲がり具合、▼ = 鳴らす終わり
        if (hov && ! muteTool && drag != Drag::move)
        {
            const auto gm = blockGeom (i);
            if (gm.ok)
            {
                auto marker = [&] (float phi, bool hot)
                {
                    const auto p = curvePoint (i, gm, phi);
                    if (p.x > gm.area.getRight() + 1.0f)
                        return;
                    const float rr = hot ? 4.5f : 3.5f;
                    g.setColour (juce::Colour (0xff0b0e12));
                    g.fillEllipse (p.x - rr, p.y - rr, rr * 2.0f, rr * 2.0f);
                    g.setColour (hot ? juce::Colours::white : col);
                    g.fillEllipse (p.x - rr + 1.0f, p.y - rr + 1.0f, rr * 2.0f - 2.0f, rr * 2.0f - 2.0f);
                };
                auto diamond = [&] (float phi, bool hot)
                {
                    const auto p = curvePoint (i, gm, phi);
                    if (p.x > gm.area.getRight() + 1.0f)
                        return;
                    juce::Path dia;
                    dia.addQuadrilateral (p.x, p.y - 5.0f, p.x + 5.0f, p.y, p.x, p.y + 5.0f, p.x - 5.0f, p.y);
                    g.setColour (juce::Colour (0xff0b0e12));
                    g.fillPath (dia);
                    g.setColour (hot ? juce::Colours::white : col);
                    g.strokePath (dia, juce::PathStrokeType (1.3f));
                };
                marker (0.0f, false);
                marker (gm.at, hoverHandle == 4 || (drag == Drag::apex && i == dragEvent));
                diamond (gm.at * 0.5f, hoverHandle == 1 || (drag == Drag::bendRise && i == dragEvent));
                diamond (gm.at + (1.0f - gm.at) * 0.5f, hoverHandle == 2 || (drag == Drag::bendFall && i == dragEvent));
                const auto tp = curvePoint (i, gm, audibleEnd (i, gm, e.tail));
                if (tp.x <= gm.area.getRight() + 1.0f)
                {
                    const bool hot = hoverHandle == 3 || (drag == Drag::tail && i == dragEvent);
                    juce::Path tri;
                    tri.addTriangle (tp.x - 5.0f, tp.y - 9.0f, tp.x + 5.0f, tp.y - 9.0f, tp.x, tp.y - 1.0f);
                    g.setColour (hot ? juce::Colours::white : deck::pick (deck::ledAmber, deck::screenTeal));
                    g.fillPath (tri);
                    g.fillRect (tp.x - 0.75f, tp.y - 1.0f, 1.5f, 6.0f);
                }
            }
        }

        // 左端のつまみ (頭の位置)
        if (audW > 22.0f && ! muteTool)
        {
            g.setColour (hov && hoverStartEdge ? juce::Colours::white : col.withAlpha (hov ? 0.6f : 0.0f));
            g.fillRoundedRectangle (box.getX() + 3.0f, box.getCentreY() - 7.0f, 2.0f, 14.0f, 1.0f);
        }

        // 右端のつまみ (鳴らす長さ)
        if (audW > 14.0f && ! muteTool)
        {
            const float gx = box.getRight() - 4.0f;
            g.setColour (hov && hoverEdge ? juce::Colours::white : col.withAlpha (hov ? 0.9f : 0.45f));
            g.fillRoundedRectangle (gx - 1.0f, box.getCentreY() - 7.0f, 2.0f, 14.0f, 1.0f);
        }

        if (box.getWidth() > 26.0f)
        {
            g.setFont (fontOf (8.5f, true, 0.05f));
            g.setColour (textCol.withAlpha (0.85f));
            g.drawText (kPatternShort[juce::jlimit (0, ScratchProcessor::numPatterns - 1, e.pattern)], box.reduced (4.0f, 2.0f).removeFromBottom (11.0f), juce::Justification::centredLeft);
            juce::String top;
            if (e.rate >= 5)
                top << kRateNames[e.rate] << " ";
            if (e.pitch != 0)
                top << (e.pitch > 0 ? "+" : "") << e.pitch;
            if (top.isNotEmpty())
                g.drawText (top.trim(), box.reduced (4.0f, 2.0f).removeFromTop (11.0f), juce::Justification::centredRight);
        }

        // ホバー/ドラッグ中は、振り幅・長さ・速さを数字で表示 (ミュートツールでは使い方)
        if (hov)
        {
            juce::String txt;
            if (muteTool)
                txt = u8 (japanese ? "なぞった所を消す (Shift で細かく)・消した所をなぞると戻る" : "DRAG TO MUTE (SHIFT = FINE) / DRAG A MUTED PART TO UNMUTE");
            else if (hoverHandle == 1 || hoverHandle == 2 || drag == Drag::bendRise || drag == Drag::bendFall)
            {
                const bool rise = hoverHandle == 1 || drag == Drag::bendRise;
                const float v = rise ? -e.bendRise : e.bendFall;
                txt = u8 (rise ? (japanese ? "上りのカーブ " : "RISE BEND ") : (japanese ? "下りのカーブ " : "FALL BEND "))
                      + (v >= 0.0f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + u8 (japanese ? "  (上下にドラッグ)" : "  (DRAG UP/DOWN)");
            }
            else if (hoverHandle == 4 || drag == Drag::apex)
            {
                const bool own = e.apexAt < 0.0f;
                const float at = own ? proc.patternApex (e.pattern) : e.apexAt;
                txt = u8 (japanese ? "頂点の位置 " : "PEAK AT ") + juce::String (juce::roundToInt (at * 100.0f))
                      + u8 (own ? (japanese ? "% 元のまま" : "% ORIGINAL") : "%") + u8 (japanese ? "  (左右にドラッグ)" : "  (DRAG LEFT/RIGHT)");
            }
            else if (hoverHandle == 3 || drag == Drag::tail)
            {
                const bool longer = e.tail < 1.0f && e.tail > proc.naturalEnd (e);
                txt = u8 (japanese ? "鳴らす終わり " : "SOUND END ") + juce::String (juce::roundToInt (e.tail * 100.0f))
                      + u8 (longer ? (japanese ? "% 伸ばした" : "% EXTENDED") : "%") + u8 (japanese ? "  (左右にドラッグ)" : "  (DRAG LEFT/RIGHT)");
            }
            else
            {
                const float lenSteps = (float) e.len * e.gate / (float) ScratchProcessor::ticksPerStep;
                const bool whole = std::abs (lenSteps - std::round (lenSteps)) < 0.01f;
                txt = u8 (japanese ? "振り幅 " : "DEPTH ") + juce::String (juce::roundToInt (e.depth * 100.0f)) + u8 ("%   ")
                      + u8 (japanese ? "長さ " : "LENGTH ") + (whole ? juce::String (juce::roundToInt (lenSteps)) : juce::String (lenSteps, 2))
                      + u8 (" × 1/16   ") + u8 (japanese ? "速さ " : "RATE ") + kRateNames[juce::jlimit (0, ScratchProcessor::numRates - 1, e.rate)];
            }
            g.setFont (fontOf (japanese ? 10.5f : 9.5f, true, 0.05f));
            const float tagW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), txt) + 14.0f;
            auto tag = juce::Rectangle<float> (tagW, 16.0f).withCentre ({ juce::jlimit (lane.getX() + tagW * 0.5f, lane.getRight() - tagW * 0.5f, slot.getCentreX()), r.getY() + 12.0f });
            g.setColour (juce::Colour (0xee222735));
            g.fillRoundedRectangle (tag, 5.0f);
            g.setColour (col);
            g.drawRoundedRectangle (tag, 5.0f, 1.0f);
            g.setColour (textCol);
            g.drawText (txt, tag, juce::Justification::centred);
        }
    }

    // 空いているマスの案内
    if (hoverEvent < 0 && (hoverCell >= 0 || selLo >= 0))
    {
        const int n = selLo >= 0 ? selHi - selLo + 1 : 1;
        const juce::String txt = selLo >= 0
            ? (u8 (japanese ? "選択: " : "SELECTED: ") + juce::String (n) + " × " + gridName (gridTicks))
            : u8 (japanese ? "クリック/ドラッグで範囲を選んで、パターンを追加" : "CLICK OR DRAG TO ADD A PATTERN HERE");
        g.setFont (fontOf (japanese ? 10.5f : 9.5f, true, 0.05f));
        const float tagW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), txt) + 14.0f;
        const int c0 = selLo >= 0 ? selLo : hoverCell;
        const float cx = lane.getX() + ((float) c0 + (float) n * 0.5f) * cw;
        auto tag = juce::Rectangle<float> (tagW, 16.0f).withCentre ({ juce::jlimit (lane.getX() + tagW * 0.5f, lane.getRight() - tagW * 0.5f, cx), r.getY() + 12.0f });
        g.setColour (juce::Colour (0xee222735));
        g.fillRoundedRectangle (tag, 5.0f);
        g.setColour (deck::pick (orange, deck::screenTeal));
        g.drawRoundedRectangle (tag, 5.0f, 1.0f);
        g.setColour (textCol);
        g.drawText (txt, tag, juce::Justification::centred);
    }

    // 再生位置
    const float playBeat = proc.uiFillBeat.load();
    if (playBeat >= 0.0f)
    {
        const float x = lane.getX() + playBeat * (float) beat * tw;
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillRect (x - 3.0f, lane.getY(), 6.0f, lane.getHeight());
        g.setColour (juce::Colours::white);
        g.fillRect (x - 1.0f, lane.getY() - 2.0f, 2.0f, lane.getHeight() + 4.0f);
    }
}

//==============================================================================
PatternEditorView::PatternEditorView (ScratchProcessor& p) : proc (p)
{
    patternBox.setColour (juce::ComboBox::outlineColourId, orange);
    patternBox.onChange = [this]
    {
        const int id = patternBox.getSelectedId();
        if (id >= 1)
        {
            pattern = id - 1;
            repaint();
        }
    };
    addAndMakeVisible (patternBox);
    resetButton.onClick = [this]
    {
        proc.resetShape (pattern);
        refreshList();
        repaint();
    };
    addAndMakeVisible (resetButton);
    refreshList();
}

void PatternEditorView::setPattern (int p)
{
    pattern = juce::jlimit (0, ScratchProcessor::numPatterns - 1, p);
    refreshList();
    repaint();
}

void PatternEditorView::refreshList()
{
    patternBox.clear (juce::dontSendNotification);
    patternBox.addSectionHeading (u8 (japanese ? "決まった形 (* = 編集済み)" : "PRESETS (* = EDITED)"));
    for (int i = 0; i < ScratchProcessor::numPatterns; ++i)
    {
        if (i == ScratchProcessor::numPresetPatterns)
            patternBox.addSectionHeading (u8 (japanese ? "ユーザー波形" : "USER"));
        juce::String name = u8 (kPatternNames[i][japanese ? 1 : 0]);
        if (i < ScratchProcessor::numPresetPatterns && proc.isShapeEdited (i))
            name << " *";
        patternBox.addItem (name, i + 1);
    }
    patternBox.setSelectedId (pattern + 1, juce::dontSendNotification);
    resetButton.setButtonText (u8 (japanese ? "初期に戻す" : "RESET SHAPE"));
    resetButton.setTooltip (u8 (japanese ? "この波形を、初期の形に戻します (決まった形のパターンは元の数式の形に)。「戻る」で取り消せます。"
                                         : "Reset this wave to its original shape. BACK undoes it."));
    patternBox.setTooltip (u8 (japanese ? "編集する波形を選びます。決まった形のパターンを編集すると、そのパターンを使っているブロック全部に効きます。"
                                        : "Choose the wave to edit. Editing a preset changes every block that uses it."));
}

void PatternEditorView::resized()
{
    auto col = getLocalBounds().removeFromLeft (174);
    patternBox.setBounds (col.removeFromTop (26));
    resetButton.setBounds (col.removeFromBottom (26));
}

juce::Rectangle<float> PatternEditorView::thumbArea() const
{
    return getLocalBounds().toFloat().removeFromLeft (174.0f).withTrimmedTop (32.0f).withTrimmedBottom (32.0f);
}

juce::Rectangle<float> PatternEditorView::gateArea() const
{
    auto b = getLocalBounds().toFloat().withTrimmedLeft (174.0f + 14.0f);
    return b.removeFromBottom (20.0f).reduced (10.0f, 0.0f);
}

juce::Rectangle<float> PatternEditorView::canvasArea() const
{
    auto b = getLocalBounds().toFloat().withTrimmedLeft (174.0f + 14.0f);
    b.removeFromBottom (26.0f);
    return b.reduced (10.0f, 0.0f).withTrimmedTop (16.0f).withTrimmedBottom (6.0f);
}

juce::Point<float> PatternEditorView::toScreen (juce::Point<float> p) const
{
    const auto c = canvasArea();
    return { c.getX() + p.x * c.getWidth(), c.getBottom() - p.y * c.getHeight() };
}

juce::Point<float> PatternEditorView::fromScreen (juce::Point<float> s) const
{
    const auto c = canvasArea();
    return { juce::jlimit (0.0f, 1.0f, (s.x - c.getX()) / c.getWidth()), juce::jlimit (0.0f, 1.0f, (c.getBottom() - s.y) / c.getHeight()) };
}

int PatternEditorView::pointAt (juce::Point<float> s) const
{
    const auto& pts = proc.getShape (pattern).points;
    int best = -1;
    float bestD = 9.0f;
    for (int i = 0; i < (int) pts.size(); ++i)
    {
        const float d = toScreen (pts[(size_t) i]).getDistanceFrom (s);
        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

juce::Point<float> PatternEditorView::handlePos (int seg) const
{
    // 曲がり具合のつまみ: 区間の真ん中 (S字は 1/4 の所。曲がり具合がよく見える)
    const auto& u = proc.getShape (pattern);
    const auto p0 = u.points[(size_t) seg], p1 = u.points[(size_t) seg + 1];
    const bool sType = seg < (int) u.curve.size() && u.curve[(size_t) seg] == ScratchProcessor::curveS;
    const float x = p0.x + (p1.x - p0.x) * (sType ? 0.25f : 0.5f);
    return toScreen ({ x, proc.shapeD (pattern, juce::jmin (x, 0.99999f)) });
}

int PatternEditorView::handleAt (juce::Point<float> s) const
{
    const auto& u = proc.getShape (pattern);
    for (int k = 0; k + 1 < (int) u.points.size(); ++k)
    {
        if (toScreen (u.points[(size_t) k + 1]).x - toScreen (u.points[(size_t) k]).x < 14.0f)
            continue; // 狭い区間は、つまみを出さない
        if (handlePos (k).getDistanceFrom (s) < 7.0f)
            return k;
    }
    return -1;
}

std::vector<int> PatternEditorView::gateEdges() const
{
    // 鳴る区間の端 (開け閉めが切り替わる所)
    std::vector<int> e;
    const auto& g = proc.getShape (pattern).gate;
    for (int k = 1; k < ScratchProcessor::userGateSegments; ++k)
        if (g[(size_t) k] != g[(size_t) k - 1])
            e.push_back (k);
    return e;
}

float PatternEditorView::edgeX (int k) const
{
    const auto c = canvasArea();
    return c.getX() + c.getWidth() * (float) k / (float) ScratchProcessor::userGateSegments;
}

int PatternEditorView::gateEdgeAt (juce::Point<float> s) const
{
    const auto c = canvasArea();
    const auto ga = gateArea();
    const bool inTop = s.y >= c.getY() - 4.0f && s.y <= c.getY() + 12.0f;
    const bool inStrip = s.y >= ga.getY() - 8.0f && s.y <= ga.getBottom() + 2.0f;
    if (! inTop && ! inStrip)
        return -1;
    for (int k : gateEdges())
        if (std::abs (s.x - edgeX (k)) < 5.0f)
            return k;
    return -1;
}

int PatternEditorView::segmentAtX (float x) const
{
    const auto ga = gateArea();
    return juce::jlimit (0, ScratchProcessor::userGateSegments - 1, (int) std::floor ((x - ga.getX()) / ga.getWidth() * (float) ScratchProcessor::userGateSegments));
}

void PatternEditorView::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.position;
    drag = Drag::none;

    // 鳴る区間の端: 左右にドラッグで、伸び縮み
    if (const int k = gateEdgeAt (pos); k >= 0)
    {
        proc.beginUserEdit();
        drag = Drag::gateEdge;
        dragEdge = k;
        gateAtDown = proc.getShape (pattern).gate;
        return;
    }

    // 下の帯: フェーダーの開け閉めを塗る (押した所の逆の値で塗る)
    if (gateArea().expanded (0.0f, 3.0f).contains (pos))
    {
        const int seg = segmentAtX (pos.x);
        proc.beginUserEdit();
        gatePaintValue = ! proc.getShape (pattern).gate[(size_t) seg];
        proc.setShapeGate (pattern, seg, gatePaintValue);
        drag = Drag::gatePaint;
        lastSegment = seg;
        repaint();
        return;
    }

    if (! canvasArea().expanded (8.0f).contains (pos))
        return;

    const int pt = pointAt (pos);
    const int n = (int) proc.getShape (pattern).points.size();
    const int hs = pt < 0 ? handleAt (pos) : -1;

    if (e.mods.isPopupMenu())
    {
        if (pt > 0 && pt < n - 1)
        {
            proc.beginUserEdit();
            proc.removeShapePoint (pattern, pt);
            hoverPoint = -1;
            repaint();
        }
        else if (hs >= 0)
            showCurveMenu (hs);
        return;
    }
    if (pt >= 0)
    {
        if (pt == 0 || pt == n - 1)
            return; // 両端は動かせない (1往復でつながるように)
        proc.beginUserEdit();
        drag = Drag::point;
        dragPoint = pt;
        return;
    }
    if (hs >= 0)
    {
        // 曲がり具合: 上下にドラッグ (「なめらか(自動)」の区間は、S字カーブにしてから)
        proc.beginUserEdit();
        const auto& u = proc.getShape (pattern);
        if (hs >= (int) u.curve.size() || u.curve[(size_t) hs] == ScratchProcessor::curveAuto)
            proc.setShapeCurve (pattern, hs, ScratchProcessor::curveS);
        drag = Drag::tension;
        dragSeg = hs;
        dragStartY = pos.y;
        dragStartTension = proc.getShape (pattern).tension[(size_t) hs];
        return;
    }
    // 空いた所: 点を追加して、そのまま動かせる
    proc.beginUserEdit();
    dragPoint = proc.addShapePoint (pattern, fromScreen (pos));
    drag = dragPoint > 0 ? Drag::point : Drag::none;
    repaint();
}

void PatternEditorView::mouseDrag (const juce::MouseEvent& e)
{
    switch (drag)
    {
        case Drag::gatePaint:
        {
            const int seg = segmentAtX (e.position.x);
            for (int s = juce::jmin (seg, lastSegment); s <= juce::jmax (seg, lastSegment); ++s)
                proc.setShapeGate (pattern, s, gatePaintValue);
            lastSegment = seg;
            break;
        }
        case Drag::gateEdge:
        {
            // 押した時の開け閉めを元に、端を動かした分だけ、開ける/閉じる
            const auto c = canvasArea();
            const int k = dragEdge;
            const int b = juce::jlimit (0, ScratchProcessor::userGateSegments,
                                        juce::roundToInt ((e.position.x - c.getX()) / c.getWidth() * (float) ScratchProcessor::userGateSegments));
            const bool openRight = gateAtDown[(size_t) k];
            for (int i = 0; i < ScratchProcessor::userGateSegments; ++i)
            {
                bool v = gateAtDown[(size_t) i];
                if (openRight)
                {
                    if (b < k && i >= b && i < k) v = true;   // 始まりを前へ
                    if (b > k && i >= k && i < b) v = false;  // 始まりを後ろへ
                }
                else
                {
                    if (b > k && i >= k && i < b) v = true;   // 終わりを後ろへ (伸ばす)
                    if (b < k && i >= b && i < k) v = false;  // 終わりを前へ (縮める)
                }
                proc.setShapeGate (pattern, i, v);
            }
            break;
        }
        case Drag::point:
            proc.setShapePoint (pattern, dragPoint, fromScreen (e.position));
            hoverPoint = dragPoint;
            break;
        case Drag::tension:
        {
            const auto& u = proc.getShape (pattern);
            const float up = (dragStartY - e.position.y) / 70.0f;
            float t = dragStartTension;
            if (u.curve[(size_t) dragSeg] == ScratchProcessor::curveS)
                t += up; // 上 = 真ん中が鋭く
            else
                t += u.points[(size_t) dragSeg + 1].y > u.points[(size_t) dragSeg].y ? -up : up; // カーブが、ドラッグした向きにしなる
            proc.setShapeTension (pattern, dragSeg, t);
            break;
        }
        case Drag::none:
            break;
    }
    repaint();
}

void PatternEditorView::mouseUp (const juce::MouseEvent&)
{
    if (drag != Drag::none)
        refreshList(); // 編集済みの印
    drag = Drag::none;
    dragPoint = dragSeg = dragEdge = -1;
    repaint();
}

void PatternEditorView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int pt = pointAt (e.position);
    const int n = (int) proc.getShape (pattern).points.size();
    if (pt > 0 && pt < n - 1)
    {
        proc.beginUserEdit();
        proc.removeShapePoint (pattern, pt);
        dragPoint = hoverPoint = -1;
        drag = Drag::none;
        repaint();
    }
}

void PatternEditorView::mouseMove (const juce::MouseEvent& e)
{
    const bool inCanvas = canvasArea().expanded (8.0f).contains (e.position);
    const int edge = gateEdgeAt (e.position);
    const int pt = inCanvas && edge < 0 ? pointAt (e.position) : -1;
    const int hs = inCanvas && edge < 0 && pt < 0 ? handleAt (e.position) : -1;
    if (pt != hoverPoint || hs != hoverSeg || edge != hoverEdge)
    {
        hoverPoint = pt;
        hoverSeg = hs;
        hoverEdge = edge;
        repaint();
    }
    setMouseCursor (edge >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                    : pt >= 0 ? juce::MouseCursor::DraggingHandCursor
                    : hs >= 0 ? juce::MouseCursor::UpDownResizeCursor
                    : inCanvas ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
}

void PatternEditorView::showCurveMenu (int seg)
{
    const auto& u = proc.getShape (pattern);
    const int cur = seg < (int) u.curve.size() ? u.curve[(size_t) seg] : 0;
    juce::PopupMenu m;
    m.setLookAndFeel (&getLookAndFeel());
    m.addSectionHeader (u8 (japanese ? "この区間のカーブ" : "CURVE OF THIS SEGMENT"));
    m.addItem (1, u8 (japanese ? "S字カーブ (動き出しと止まりがなめらか)" : "S-curve (smooth start and stop)"), true, cur == ScratchProcessor::curveS);
    m.addItem (2, u8 (japanese ? "片側カーブ (片方に寄せる)" : "Single curve (bend to one side)"), true, cur == ScratchProcessor::curveSingle);
    m.addItem (3, u8 (japanese ? "なめらか (自動)" : "Smooth (auto)"), true, cur == ScratchProcessor::curveAuto);
    m.addItem (4, u8 (japanese ? "直線" : "Straight line"));
    m.addSeparator();
    m.addItem (5, u8 (japanese ? "曲がり具合を 0 に戻す" : "Reset the bend"));
    juce::Component::SafePointer<PatternEditorView> safe (this);
    const int pat = pattern;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, seg, pat] (int r)
    {
        if (safe == nullptr || r <= 0)
            return;
        auto& pr = safe->proc;
        pr.beginUserEdit();
        if (r == 1) pr.setShapeCurve (pat, seg, ScratchProcessor::curveS);
        if (r == 2) pr.setShapeCurve (pat, seg, ScratchProcessor::curveSingle);
        if (r == 3) pr.setShapeCurve (pat, seg, ScratchProcessor::curveAuto);
        if (r == 4) { pr.setShapeCurve (pat, seg, ScratchProcessor::curveSingle); pr.setShapeTension (pat, seg, 0.0f); }
        if (r == 5) pr.setShapeTension (pat, seg, 0.0f);
        safe->refreshList();
        safe->repaint();
    });
}

void PatternEditorView::paint (juce::Graphics& g)
{
    const auto col = patternColour (pattern);
    const auto& up = proc.getShape (pattern);
    const bool edited = pattern >= ScratchProcessor::numPresetPatterns || proc.isShapeEdited (pattern);

    // 左: 今の波形の小さな絵と、状態
    {
        const auto t = thumbArea().reduced (0.0f, 2.0f);
        deck::drawLcd (g, t, 5.0f);
        auto icon = t.reduced (12.0f, 10.0f).withTrimmedBottom (16.0f);
        juce::Point<float> prev;
        for (int k = 0; k <= 64; ++k)
        {
            const float phi = (float) k / 64.0f;
            const juce::Point<float> p (icon.getX() + icon.getWidth() * phi, icon.getBottom() - icon.getHeight() * proc.shapeD (pattern, juce::jmin (phi, 0.99999f)));
            if (k > 0)
            {
                const bool open = proc.shapeGate (pattern, phi - 0.5f / 64.0f);
                g.setColour (open ? col : mutedCol.withAlpha (0.35f));
                g.drawLine (prev.x, prev.y, p.x, p.y, open ? 1.8f : 1.0f);
            }
            prev = p;
        }
        g.setFont (fontOf (japanese ? 10.0f : 9.0f, true, 0.06f));
        g.setColour (edited ? deck::pick (deck::ledAmber, deck::screenTeal) : mutedCol);
        g.drawText (u8 (pattern >= ScratchProcessor::numPresetPatterns ? (japanese ? "ユーザー波形" : "USER WAVE")
                        : edited ? (japanese ? "編集済み" : "EDITED") : (japanese ? "初期の形" : "ORIGINAL")),
                    t.reduced (8.0f, 4.0f).removeFromBottom (14.0f), juce::Justification::centred);
        deck::drawLcdGloss (g, t, 5.0f);
    }

    // キャンバス
    const auto c = canvasArea();
    const auto frame = c.expanded (8.0f, 6.0f).withTrimmedTop (-10.0f);
    deck::drawLcd (g, frame);

    // フェーダーが閉じている所 (暗く)
    const float segW = c.getWidth() / (float) ScratchProcessor::userGateSegments;
    for (int s = 0; s < ScratchProcessor::userGateSegments; ++s)
        if (! up.gate[(size_t) s])
        {
            g.setColour (juce::Colour (0xcc06080b));
            g.fillRect (c.getX() + (float) s * segW, c.getY(), segW + 0.5f, c.getHeight());
        }

    // 目盛り: 1往復を8つに
    for (int k = 0; k <= 8; ++k)
    {
        const float x = c.getX() + c.getWidth() * (float) k / 8.0f;
        g.setColour (k % 4 == 0 ? juce::Colour (0xff3a4152) : juce::Colour (0xff232834));
        g.fillRect (x - 0.5f, c.getY(), 1.0f, c.getHeight());
    }
    g.setColour (juce::Colour (0xff232834));
    g.fillRect (c.getX(), c.getCentreY() - 0.5f, c.getWidth(), 1.0f);

    // 鳴る区間の端: 縦の点線と、上の ▼ つまみ
    for (int k : gateEdges())
    {
        const float x = edgeX (k);
        const bool hot = k == hoverEdge || (drag == Drag::gateEdge && k == dragEdge);
        for (float yy = c.getY(); yy < c.getBottom(); yy += 6.0f)
        {
            g.setColour (deck::pick (deck::ledAmber, deck::screenTeal).withAlpha (hot ? 0.8f : 0.4f));
            g.fillRect (x - 0.5f, yy, 1.0f, 3.0f);
        }
        juce::Path tri;
        tri.addTriangle (x - 5.0f, c.getY() - 2.0f, x + 5.0f, c.getY() - 2.0f, x, c.getY() + 6.0f);
        g.setColour (hot ? juce::Colours::white : deck::pick (deck::ledAmber, deck::screenTeal));
        g.fillPath (tri);
    }

    // 動きのカーブ (聞こえる所は明るく)
    {
        const int n = juce::jmax (16, (int) c.getWidth());
        juce::Point<float> prev;
        for (int k = 0; k <= n; ++k)
        {
            const float phi = (float) k / (float) n;
            const auto p = toScreen ({ phi, proc.shapeD (pattern, juce::jmin (phi, 0.99999f)) });
            if (k > 0)
            {
                const bool open = proc.shapeGate (pattern, phi - 0.5f / (float) n);
                if (open)
                {
                    g.setColour (col.withAlpha (0.25f));
                    g.drawLine (prev.x, prev.y, p.x, p.y, 5.0f);
                }
                g.setColour (open ? col : mutedCol.withAlpha (0.45f));
                g.drawLine (prev.x, prev.y, p.x, p.y, open ? 2.2f : 1.2f);
            }
            prev = p;
        }
    }

    // 曲がり具合のつまみ (◇)
    for (int k = 0; k + 1 < (int) up.points.size(); ++k)
    {
        if (toScreen (up.points[(size_t) k + 1]).x - toScreen (up.points[(size_t) k]).x < 14.0f)
            continue;
        const auto h = handlePos (k);
        const bool hot = k == hoverSeg || (drag == Drag::tension && k == dragSeg);
        juce::Path dia;
        dia.addQuadrilateral (h.x, h.y - 5.0f, h.x + 5.0f, h.y, h.x, h.y + 5.0f, h.x - 5.0f, h.y);
        g.setColour (juce::Colour (0xff0b0e12));
        g.fillPath (dia);
        g.setColour (hot ? juce::Colours::white : col.withAlpha (0.8f));
        g.strokePath (dia, juce::PathStrokeType (1.3f));
    }

    // 点 (両端は固定)
    for (int i = 0; i < (int) up.points.size(); ++i)
    {
        const auto p = toScreen (up.points[(size_t) i]);
        const bool end = i == 0 || i == (int) up.points.size() - 1;
        const bool hot = i == hoverPoint || i == dragPoint;
        if (end)
        {
            g.setColour (mutedCol);
            g.fillRect (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p));
            continue;
        }
        const float rad = hot ? 6.0f : 4.5f;
        if (hot)
        {
            g.setColour (col.withAlpha (0.3f));
            g.fillEllipse (p.x - rad * 1.8f, p.y - rad * 1.8f, rad * 3.6f, rad * 3.6f);
        }
        g.setColour (hot ? juce::Colours::white : col);
        g.fillEllipse (p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f);
        g.setColour (juce::Colour (0xff0e1117));
        g.drawEllipse (p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f, 1.2f);
    }
    deck::drawLcdGloss (g, frame);

    // 見出しと使い方
    g.setFont (fontOf (japanese ? 10.5f : 9.5f, true, 0.06f));
    g.setColour (col);
    g.drawText (u8 (kPatternNames[pattern][japanese ? 1 : 0]) + u8 (japanese ? "  動きのカーブ" : "  MOTION CURVE"),
                juce::Rectangle<float> (c.getX(), c.getY() - 16.0f, 220.0f, 14.0f), juce::Justification::centredLeft);
    g.setFont (fontOf (japanese ? 9.5f : 8.5f, false));
    g.setColour (mutedCol);
    g.drawText (u8 (japanese ? "点: ドラッグ / クリックで追加 ・ ◇: 上下で曲げる (右クリックで種類) ・ ▼: 鳴る区間の端"
                             : "POINT: DRAG / CLICK TO ADD - ◇: BEND (RIGHT-CLICK: TYPE) - ▼: SOUND EDGE"),
                juce::Rectangle<float> (c.getX() + 190.0f, c.getY() - 16.0f, c.getWidth() - 190.0f, 14.0f), juce::Justification::centredRight);

    // フェーダーの帯
    const auto ga = gateArea();
    const float gw = ga.getWidth() / (float) ScratchProcessor::userGateSegments;
    for (int s = 0; s < ScratchProcessor::userGateSegments; ++s)
    {
        const auto cell = juce::Rectangle<float> (ga.getX() + (float) s * gw, ga.getY(), gw, ga.getHeight()).reduced (0.5f, 0.0f);
        g.setColour (up.gate[(size_t) s] ? col.withAlpha (0.75f) : juce::Colour (0xff262b37));
        g.fillRect (cell);
    }
    g.setColour (panelEdge);
    g.drawRect (ga, 1.0f);
    g.setFont (fontOf (japanese ? 9.5f : 8.5f, true, 0.05f));
    g.setColour (textCol.withAlpha (0.9f));
    g.drawText (u8 (japanese ? "フェーダー (クリック/ドラッグで開け閉め)" : "FADER (CLICK / DRAG TO OPEN OR CLOSE)"), ga.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
}

//==============================================================================
ScratchEditor::Knob& ScratchEditor::addKnob (const juce::String& id, juce::Colour colour)
{
    auto k = std::make_unique<Knob>();
    k->id = id;
    k->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    k->slider.setColour (juce::Slider::textBoxTextColourId, textCol);
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 18);
    k->slider.setTextBoxIsEditable (true);
    k->attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

ScratchEditor::Combo& ScratchEditor::addCombo (const juce::String& id, juce::Colour colour)
{
    auto c = std::make_unique<Combo>();
    c->id = id;
    c->box.addItemList (choiceItems (id, false), 1);
    c->box.setColour (juce::ComboBox::outlineColourId, colour);
    c->attach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, c->box);
    addAndMakeVisible (c->box);
    addAndMakeVisible (c->label);
    combos.push_back (std::move (c));
    return *combos.back();
}

ScratchEditor::ScratchEditor (ScratchProcessor& p)
    : AudioProcessorEditor (&p), proc (p), platter (p), sourceView (p, [this] (float v) { setFloat ("start", v); }), timeline (p), patternEditor (p)
{
    deck::loadSkin();
    setLookAndFeel (&laf);

    rateCombo = &addCombo ("rate", orange);

    addAndMakeVisible (platter);
    addAndMakeVisible (sourceView);

    // 音源のタイル
    auto sourceIcon = [this] (juce::Graphics& g, juce::Rectangle<float> a, int idx, bool sel)
    {
        const auto col = deck::accent (sel ? orange : teal);
        if (idx == ScratchProcessor::srcSample)
        {
            const bool loaded = proc.getSampleSeconds() > 0.0;
            const int bars = 18;
            const float bw = a.getWidth() / (float) bars;
            for (int i = 0; i < bars; ++i)
            {
                const float v = loaded ? proc.uiWave[(size_t) (i * ScratchProcessor::numWaveBins / bars)].load()
                                       : 0.25f + 0.6f * std::abs (pseudoNoise (i * 7));
                const float h = juce::jmax (2.0f, v * a.getHeight());
                g.setColour (col.withAlpha (loaded ? 1.0f : 0.5f));
                g.fillRoundedRectangle (a.getX() + (float) i * bw + 1.0f, a.getCentreY() - h * 0.5f, juce::jmax (1.0f, bw - 2.0f), h, 1.0f);
            }
            return;
        }
        juce::Path p;
        const int steps = 48;
        for (int i = 0; i <= steps; ++i)
        {
            const float v = idx == ScratchProcessor::srcNoise ? pseudoNoise (i) * 0.9f
                                                              : ScratchProcessor::shapeValue (idx, 2.0 * (double) i / (double) steps) * 0.85f;
            const float x = a.getX() + a.getWidth() * (float) i / (float) steps;
            const float y = a.getCentreY() - v * a.getHeight() * 0.5f;
            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
        g.setColour (col);
        g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    for (int i = 0; i < ScratchProcessor::numSources; ++i)
    {
        sourceTiles[(size_t) i] = std::make_unique<Tile> (i, [this] (int idx) { setChoice ("src", idx); }, [this] { return (int) param ("src"); }, sourceIcon);
        addAndMakeVisible (*sourceTiles[(size_t) i]);
    }

    // パターンのタイル (動きのカーブ。聞こえる部分は明るく、ミュートされる部分は暗く)
    auto tileIcon = [this] (juce::Graphics& g, juce::Rectangle<float> a, int idx, bool sel)
    {
        const auto col = deck::accent (sel ? orange : teal);
        const int steps = 72;
        juce::Point<float> prev;
        for (int i = 0; i <= steps; ++i)
        {
            const float phi = (float) i / (float) steps;
            const float d = proc.shapeD (idx, juce::jmin (phi, 0.99999f));
            const juce::Point<float> p (a.getX() + a.getWidth() * phi, a.getBottom() - a.getHeight() * d);
            if (i > 0)
            {
                const bool open = proc.shapeGate (idx, phi - 0.5f / (float) steps);
                g.setColour (open ? col : mutedCol.withAlpha (0.35f));
                g.drawLine (prev.x, prev.y, p.x, p.y, open ? 2.4f : 1.4f);
            }
            prev = p;
        }
    };
    for (int i = 0; i < ScratchProcessor::numPatterns; ++i)
    {
        patternTiles[(size_t) i] = std::make_unique<Tile> (i, [this] (int idx) { setChoice ("pattern", idx); }, [this] { return (int) param ("pattern"); }, tileIcon);
        addAndMakeVisible (*patternTiles[(size_t) i]);
    }

    loadButton.onClick = [this] { openLoadDialog(); };
    addAndMakeVisible (loadButton);
    presetBox.setColour (juce::ComboBox::outlineColourId, teal);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id >= 1 && proc.loadPresetSample (id - 1))
        {
            setChoice ("src", ScratchProcessor::srcSample);
            shownPreset = id - 1;
        }
    };
    addAndMakeVisible (presetBox);

    // フィルの操作
    addAndMakeVisible (timeline);
    addChildComponent (patternEditor);
    timeline.onEditPattern = [this] (int slot) { showPatternEditor (slot); };
    modeFill.onClick = [this] { editorView = false; setChoice ("mode", 0); timerCallback(); };
    modeSingle.onClick = [this] { editorView = false; setChoice ("mode", 1); timerCallback(); };
    modeEdit.onClick = [this] { showPatternEditor (patternEditor.pattern); };

    // マス目の細かさ (保存済みの設定があれば、それを使う)
    for (int i = 0; i < 4; ++i)
        gridBox.addItem (gridName (kGridTicks[i]), i + 1);
    {
        const int saved = (int) proc.apvts.state.getProperty ("grid", 6);
        int id = 1;
        for (int i = 0; i < 4; ++i)
            if (kGridTicks[i] == saved)
                id = i + 1;
        gridBox.setSelectedId (id, juce::dontSendNotification);
        timeline.gridTicks = kGridTicks[id - 1];
    }
    gridBox.setColour (juce::ComboBox::outlineColourId, orange);
    gridBox.onChange = [this]
    {
        const int id = gridBox.getSelectedId();
        if (id < 1)
            return;
        timeline.gridTicks = kGridTicks[id - 1];
        proc.apvts.state.setProperty ("grid", timeline.gridTicks, nullptr);
        timeline.repaint();
    };
    gridLabel.setJustificationType (juce::Justification::centredRight);
    gridLabel.setColour (juce::Label::textColourId, mutedCol);
    addAndMakeVisible (gridLabel);
    addAndMakeVisible (gridBox);
    muteButton.setClickingTogglesState (true);
    muteButton.onClick = [this] { timeline.muteTool = muteButton.getToggleState(); timeline.repaint(); };
    addAndMakeVisible (muteButton);

    // 波形エディタ: コピー元 (選ぶと、今のユーザー波形に写す)
    copyBox.setColour (juce::ComboBox::outlineColourId, orange);
    copyBox.onChange = [this]
    {
        const int id = copyBox.getSelectedId();
        if (id < 1)
            return;
        proc.copyShape (patternEditor.pattern, id - 1);
        patternEditor.refreshList();
        copyBox.setSelectedId (0, juce::dontSendNotification);
        patternEditor.repaint();
    };
    addChildComponent (copyBox);
    tmplPrev.setButtonText (u8 ("◀"));
    tmplNext.setButtonText (u8 ("▶"));
    tmplPrev.onClick = [this] { stepTemplate (-1); };
    tmplNext.onClick = [this] { stepTemplate (1); };
    tmplBox.setColour (juce::ComboBox::outlineColourId, orange);
    tmplBox.onChange = [this]
    {
        const int id = tmplBox.getSelectedId();
        if (id > kMyPatternId && id - kMyPatternId <= myNames.size())
        {
            if (! proc.loadMyPattern (myNames[id - kMyPatternId - 1]))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Cue Burn",
                                                        u8 (japanese ? "このマイパターンは読み込めませんでした: " : "Could not load this pattern: ") + myNames[id - kMyPatternId - 1]);
            patternEditor.refreshList();
        }
        else if (id > 0 && id <= ScratchProcessor::emptyTemplate + ScratchProcessor::numFactoryPatterns + 1)
            proc.loadTemplate (id - 1);
    };
    tmplBox.beforePopup = [this]
    {
        rebuildTemplateList();
        refreshFillControls();
    };
    myButton.onClick = [this] { showMyPatternMenu(); };
    addAndMakeVisible (myButton);
    for (int i = 0; i < 3; ++i)
    {
        const int bars = i == 0 ? 1 : (i == 1 ? 2 : 4);
        barButtons[(size_t) i].onClick = [this, bars] { proc.setBars (bars); };
    }
    randomButton.onClick = [this] { proc.randomize(); };
    varyButton.onClick = [this] { proc.vary(); };
    backButton.onClick = [this]
    {
        if (editorView)
            proc.backUser();
        else
            proc.back();
        patternEditor.repaint();
    };
    previewButton.setClickingTogglesState (true);
    previewButton.onClick = [this] { proc.previewOn.store (previewButton.getToggleState()); };
    for (auto* b : { &modeFill, &modeSingle, &modeEdit, &tmplPrev, &tmplNext, &randomButton, &varyButton, &backButton, &previewButton })
        addAndMakeVisible (b);
    addAndMakeVisible (tmplBox);
    for (auto& b : barButtons)
        addAndMakeVisible (b);
    addChildComponent (prompt);

    Panel motion { {}, 0, orange, { &addKnob ("depth", orange), &addKnob ("cut", orange), &addKnob ("feel", orange), &addKnob ("human", orange) } };
    startKnob = &addKnob ("start", teal);
    Panel sound { {}, 1, teal, { startKnob, &addKnob ("tone", teal), &addKnob ("vinyl", teal) } };
    Panel out { {}, 2, blue, { &addKnob ("level", blue) } };
    panels = { motion, sound, out };

    // 言語切り替え。保存済みの設定があればそれを、なければOSの言語で決める
    const auto saved = proc.apvts.state.getProperty ("lang").toString();
    japanese = saved.isEmpty() ? juce::SystemStats::getUserLanguage().startsWithIgnoreCase ("ja") : saved == "ja";

    enButton.setButtonText ("EN");
    jaButton.setButtonText (u8 ("日本語"));
    addAndMakeVisible (enButton);
    addAndMakeVisible (jaButton);
    enButton.onClick = [this] { setLanguage (false); };
    jaButton.onClick = [this] { setLanguage (true); };
    enButton.setTooltip ("Language");
    jaButton.setTooltip (u8 ("言語"));

    addAndMakeVisible (skinButton);
    skinButton.onClick = [this]
    {
        deck::setSkin (deck::white() ? deck::Skin::deck : deck::Skin::white);
        deck::saveSkin();
        applySkin();
    };
    applySkin();

    applyLanguage();

    setSize (940, 742);
    startTimerHz (30);
}

ScratchEditor::~ScratchEditor()
{
    // ウィンドウを閉じるときに、レコードを掴んだまま・試聴したままにならないように
    proc.platterTouched.store (false);
    proc.previewOn.store (false);
    proc.previewPattern.store (-1);
    setLookAndFeel (nullptr);
}

void ScratchEditor::setChoice (const char* id, int index)
{
    if (auto* prm = proc.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) index));
        prm->endChangeGesture();
    }
    repaint();
}

void ScratchEditor::setFloat (const char* id, float value)
{
    if (auto* prm = proc.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
}

void ScratchEditor::loadFile (const juce::File& file)
{
    if (proc.loadSample (file))
    {
        setChoice ("src", ScratchProcessor::srcSample);
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Cue Burn",
                                                u8 (japanese ? "このファイルは読み込めませんでした: " : "Could not load this file: ") + file.getFileName());
    }
    repaint();
}

void ScratchEditor::openLoadDialog()
{
    chooser = std::make_unique<juce::FileChooser> (u8 (japanese ? "サンプルを選ぶ" : "Choose a sample"), juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                                  loadFile (f);
                          });
}

bool ScratchEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (proc.canLoad (f))
            return true;
    return false;
}

void ScratchEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    for (auto& f : files)
        if (proc.canLoad (f))
        {
            loadFile (juce::File (f));
            break;
        }
    repaint();
}

void ScratchEditor::setLanguage (bool jp)
{
    japanese = jp;
    proc.apvts.state.setProperty ("lang", jp ? "ja" : "en", nullptr);
    applyLanguage();
}

void ScratchEditor::applySkin()
{
    shownSkin = deck::currentSkin();
    laf.refreshSkin();
    for (auto& p : panels)
        for (auto* k : p.knobs)
        {
            k->slider.setColour (juce::Slider::rotarySliderFillColourId, deck::accent (p.colour));
            k->slider.setColour (juce::Slider::textBoxTextColourId, deck::pick (textCol, deck::whiteMuted));
        }
    gridLabel.setColour (juce::Label::textColourId, deck::pick (mutedCol, deck::whiteMuted));
    sendLookAndFeelChange();
    applyLanguage(); // 文字の書体も変わるので、ラベルの文字を作り直す
    resized();
    repaint();
}

void ScratchEditor::applyLanguage()
{
    skinButton.setButtonText (u8 (japanese ? "見た目" : "LOOK"));
    skinButton.setTooltip (u8 (japanese ? "黒い CDJ 風の見た目と、白い見た目を切り替えます。どのプロジェクトでも同じ見た目になります。"
                                        : "Switches between the dark deck look and the white look. The choice applies to all projects."));
    enButton.setToggleState (! japanese, juce::dontSendNotification);
    jaButton.setToggleState (japanese, juce::dontSendNotification);
    platter.japanese = japanese;
    sourceView.japanese = japanese;
    loadButton.setButtonText (u8 (japanese ? "読み込む…" : "LOAD…"));
    {
        presetBox.clear (juce::dontSendNotification);
        int group = -1;
        for (int i = 0; i < kNumPresetSamples; ++i)
        {
            if (kPresetSamples[i].group != group)
            {
                group = kPresetSamples[i].group;
                presetBox.addSectionHeading (u8 (kPresetGroups[group][japanese ? 1 : 0]));
            }
            presetBox.addItem (u8 (japanese ? kPresetSamples[i].ja : kPresetSamples[i].en), i + 1);
        }
        presetBox.setTextWhenNothingSelected (u8 (japanese ? "プリセット…" : "PRESETS…"));
        presetBox.setTooltip (u8 (japanese ? "スクラッチ向きの音 (声・ホーン・オーケストラヒット・シンセ・808・ギター) を選んで、すぐ鳴らせます。"
                                           : "Pick a scratch-ready sound (voices, horns, orchestra hits, synths, 808s, guitars) and play it right away."));
        shownPreset = -2; // 表示を更新させる
    }
    loadButton.setTooltip (u8 (japanese ? "オーディオファイル(WAV / AIFF / FLAC / OGG / MP3)を読み込みます。ウィンドウにドロップしても読み込めます。"
                                        : "Load an audio file (WAV / AIFF / FLAC / OGG / MP3). You can also drop a file onto the window."));

    for (auto& k : knobs)
        if (auto* e = findLoc (k->id))
        {
            k->label.setLabel (u8 (japanese ? e->jaLabel : e->enLabel), juce::Justification::centred);
            k->label.setTooltip (u8 (japanese ? e->jaHelp : e->enHelp));
        }

    for (auto& c : combos)
        if (auto* e = findLoc (c->id))
        {
            c->label.setLabel (u8 (japanese ? e->jaLabel : e->enLabel), juce::Justification::centredRight);
            c->label.setTooltip (u8 (japanese ? e->jaHelp : e->enHelp));
        }

    for (int i = 0; i < ScratchProcessor::numSources; ++i)
    {
        sourceTiles[(size_t) i]->setLabel (u8 (kSourceNames[i][japanese ? 1 : 0]));
        sourceTiles[(size_t) i]->setTooltip (u8 (kSourceHelp[i][japanese ? 1 : 0]));
    }
    for (int i = 0; i < ScratchProcessor::numPatterns; ++i)
    {
        patternTiles[(size_t) i]->setLabel (u8 (kPatternNames[i][japanese ? 1 : 0]));
        patternTiles[(size_t) i]->setTooltip (u8 (kPatternHelp[i][japanese ? 1 : 0]));
    }

    timeline.japanese = japanese;
    patternEditor.japanese = japanese;
    patternEditor.refreshList();
    modeEdit.setButtonText (u8 (japanese ? "波形エディタ" : "WAVE EDITOR"));
    modeEdit.setTooltip (u8 (japanese ? "ユーザー波形(4つ)の動きのカーブと、フェーダーの開け閉めを、自分で描けます。描いた波形は、フィルのブロックや単発で使えます。"
                                      : "Draw your own patterns (4 user slots): the motion curve and the fader. Use them in fill blocks or in SINGLE mode."));
    gridLabel.setText (u8 (japanese ? "マス目" : "GRID"), juce::dontSendNotification);
    gridLabel.setFont (fontOf (japanese ? 11.5f : 10.5f, true, 0.1f));
    gridBox.setTooltip (u8 (japanese ? "タイムラインのマス目の細かさ。1/16・1/32 と、3連符 (1/8T = 3連8分、1/16T = 3連16分) から選べます。"
                                     : "Grid size of the timeline: 1/16, 1/32, or triplets (1/8T, 1/16T)."));
    muteButton.setButtonText (u8 (japanese ? "ミュート" : "MUTE"));
    muteButton.setTooltip (u8 (japanese ? "オンにすると、ブロックをなぞった所の音を消せます (レコードは動いたまま、フェーダーだけ閉じます)。消した所をなぞると戻ります。Shift を押しながらで、マス目より細かく。"
                                        : "When on, drag over a block to mute that part (the record keeps moving, only the fader closes). Drag a muted part to unmute. Hold Shift for finer than the grid."));
    {
        copyBox.clear (juce::dontSendNotification);
        for (int i = 0; i < ScratchProcessor::numPatterns; ++i)
        {
            if (i == ScratchProcessor::numPresetPatterns)
                copyBox.addSeparator();
            copyBox.addItem (u8 (kPatternNames[i][japanese ? 1 : 0]), i + 1);
        }
        copyBox.setTextWhenNothingSelected (u8 (japanese ? "コピー元を選んで写す…" : "Copy from a pattern…"));
        copyBox.setTooltip (u8 (japanese ? "選んだパターンの形を、いま編集しているユーザー波形に写します。編集の出発点に。「戻る」で取り消せます。"
                                         : "Copy the shape of a pattern into the user wave you are editing. BACK undoes it."));
    }
    patternEditor.setTooltip (u8 (japanese ? "上: 動きのカーブ (レコードをどこまで押し出すか)。点をドラッグ、空いた所をクリックで追加、ダブルクリック/右クリックで削除。\n下の帯: フェーダーの開け閉め (明るい所だけ聞こえます)。\n左: 編集するユーザー波形を選びます。"
                                         : "Top: motion curve (how far the record is pushed). Drag points, click to add, double- or right-click to delete.\nBottom strip: the fader (only the lit parts are heard).\nLeft: choose the user wave to edit."));
    modeFill.setButtonText (u8 (japanese ? "フィル" : "FILL"));
    modeSingle.setButtonText (u8 (japanese ? "単発" : "SINGLE"));
    modeFill.setTooltip (u8 (japanese ? "フィル: ノートを1つ置くだけで、下のフレーズ(1〜4小節)が丸ごと鳴ります。ノートが長ければループします。"
                                      : "FILL: one note plays the whole phrase below (1-4 bars). Longer notes loop it."));
    modeSingle.setTooltip (u8 (japanese ? "単発: ノートの間、選んだ1つのパターンをくり返します。"
                                        : "SINGLE: the selected pattern repeats while the note is held."));
    tmplPrev.setTooltip (u8 (japanese ? "前のテンプレート" : "Previous template"));
    tmplNext.setTooltip (u8 (japanese ? "次のテンプレート" : "Next template"));
    tmplBox.setTooltip (u8 (japanese ? "テンプレートを選びます。2小節・4小節にすると、最後の小節に締めのロールが付きます。"
                                     : "Choose a template. With 2 or 4 bars, the last bar gets a closing roll."));
    {
        const juce::String bars[3] = { u8 (japanese ? "1小節" : "1 BAR"), u8 (japanese ? "2小節" : "2 BARS"), u8 (japanese ? "4小節" : "4 BARS") };
        for (int i = 0; i < 3; ++i)
        {
            barButtons[(size_t) i].setButtonText (bars[i]);
            barButtons[(size_t) i].setTooltip (u8 (japanese ? "フレーズの長さ" : "Phrase length"));
        }
    }
    myButton.setButtonText (u8 (japanese ? "マイパターン" : "MY PATTERNS"));
    myButton.setTooltip (u8 (japanese ? "今のブロックの並びを、名前を付けて保存します。テンプレートの一覧や ◀▶ から、どのプロジェクトでも呼び出せます。音源とノブは変わりません。"
                                      : "Save the current blocks under a name. Recall them from the template list or ◀▶ in any project. The source and knobs stay as they are."));
    randomButton.setButtonText (u8 (japanese ? "ランダム" : "RANDOM"));
    randomButton.setTooltip (u8 (japanese ? "新しいフィルを、ランダムに作ります。1拍目から入り、くり返しと変化をつけ、最後で盛り上げます。"
                                          : "Make a new fill at random: it starts on the downbeat, repeats with changes, and builds up at the end."));
    varyButton.setButtonText (u8 (japanese ? "バリエーション" : "VARIATION"));
    varyButton.setTooltip (u8 (japanese ? "リズムはそのままで、パターンや高さを少し変えます。"
                                        : "Keep the rhythm, change some patterns and pitches."));
    backButton.setButtonText (u8 (japanese ? "◀ 戻る" : "◀ BACK"));
    backButton.setTooltip (u8 (japanese ? "ひとつ前のフレーズに戻ります。" : "Go back to the previous phrase."));
    previewButton.setButtonText (u8 (japanese ? "▶ 試聴" : "▶ PREVIEW"));
    previewButton.setTooltip (u8 (japanese ? "ノートを置かなくても、フレーズを鳴らして確かめられます。もう一度押すと止まります。"
                                           : "Play the phrase without placing notes. Click again to stop."));
    timeline.setTooltip (u8 (japanese ? "ブロックをクリック: 次のパターン (Shift+クリック: 前のパターン、ホイールでも切り替え)\nブロックの上の ●: 左右で頂点の位置 / ◇: 上下でカーブを曲げる / ▼: 左右で鳴らす終わり (短く・長く)\n横にドラッグ: ブロックを移動\n上下にドラッグ: そのブロックの振り幅\n右端・左端をドラッグ: そのブロックの長さ(縮めるとマスが空き、空きマスへは伸ばせます)\n右クリック: パターン・速さ・高さの変更、波形の編集、削除\n空いているマス: クリック/ドラッグで範囲を選んで、パターンを追加\n「ミュート」をオン: なぞった所の音を消す"
                                       : "Click a block: next pattern (Shift+click: previous, mouse wheel also works)\nOn a block: drag ● left/right to move the peak, ◇ up/down to bend the curve, ▼ left/right for the sound end\nDrag sideways: move the block\nDrag up/down: depth of that block\nDrag the right or left edge: block length (shortening frees steps; you can also stretch into empty steps)\nRight-click: change pattern, rate, pitch, edit the wave, or delete\nEmpty steps: click or drag to select, then add a pattern\nMUTE on: drag to mute parts of a block"));
    rebuildTemplateList();
    shownPhraseVersion = -1; // 表示を更新させる

    platter.setTooltip (u8 (japanese ? "レコードをDJのようにドラッグして、スクラッチできます。時計回りで前へ、反時計回りで後ろへ進みます。ノートが無くても鳴らせます。"
                                     : "Drag the record like a DJ. Clockwise plays forward, counter-clockwise plays backward. Works even without notes."));
    sourceView.setTooltip (u8 (japanese ? "サンプルのときは、クリックした位置からスクラッチを始めます(頭出し)。色の付いた範囲が、押し出す範囲です。ホイールで拡大、右クリックで全体に戻します。"
                                        : "For samples, click to set where the scratch starts (the cue). The highlighted area is how far the record is pushed. Use the wheel to zoom, right-click to see the whole sample."));
    repaint();
}

void ScratchEditor::stepTemplate (int delta)
{
    proc.stepTemplate (delta);
    patternEditor.refreshList(); // マイパターンは、編集した波形も一緒に戻る
}

void ScratchEditor::rebuildTemplateList()
{
    const int sel = tmplBox.getSelectedId();
    tmplBox.clear (juce::dontSendNotification);
    tmplBox.addItem (u8 (kTemplateNames[ScratchProcessor::emptyTemplate][japanese ? 1 : 0]), ScratchProcessor::emptyTemplate + 1);
    tmplBox.addSectionHeading ("Sound Artz");
    for (int i = 1; i <= ScratchProcessor::numFactoryPatterns; ++i)
        tmplBox.addItem (u8 (kTemplateNames[ScratchProcessor::emptyTemplate + i][japanese ? 1 : 0]), ScratchProcessor::emptyTemplate + i + 1);
    tmplBox.addSectionHeading (u8 (japanese ? "基本" : "BASIC"));
    for (int i = 0; i < ScratchProcessor::numTemplates; ++i)
    {
        if (i == ScratchProcessor::firstGenreTemplate)
            tmplBox.addSectionHeading (u8 (japanese ? "ジャンル別" : "GENRES"));
        tmplBox.addItem (u8 (kTemplateNames[i][japanese ? 1 : 0]), i + 1);
    }
    myNames = proc.getMyPatternNames();
    if (! myNames.isEmpty())
        tmplBox.addSectionHeading (u8 (japanese ? "マイパターン" : "MY PATTERNS"));
    for (int i = 0; i < myNames.size(); ++i)
        tmplBox.addItem (myNames[i], kMyPatternId + i + 1);
    if (sel > 0)
        tmplBox.setSelectedId (sel, juce::dontSendNotification);
}

void ScratchEditor::showMyPatternMenu()
{
    const auto& ph = proc.getPhrase();
    const auto current = proc.myPatternExists (ph.myName) ? ph.myName : juce::String();
    juce::PopupMenu m;
    m.addItem (1, u8 (japanese ? "今のパターンを保存..." : "Save current pattern..."));
    if (current.isNotEmpty())
    {
        m.addItem (2, u8 (japanese ? "「" : "Overwrite \"") + current + u8 (japanese ? "」に上書き保存" : "\""), ph.edited || ph.isRandom);
        m.addItem (3, u8 (japanese ? "「" : "Delete \"") + current + u8 (japanese ? "」を削除..." : "\"..."));
    }
    m.addSeparator();
    m.addItem (4, u8 (japanese ? "保存先のフォルダを開く" : "Open folder"));
    juce::Component::SafePointer<ScratchEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (myButton), [safe, current] (int r)
    {
        if (safe == nullptr)
            return;
        if (r == 1)
            safe->askSavePattern();
        else if (r == 2)
            safe->savePattern (current);
        else if (r == 3)
            safe->askDeletePattern (current);
        else if (r == 4)
        {
            const auto dir = safe->proc.getMyPatternDir();
            dir.createDirectory();
            dir.startAsProcess();
        }
    });
}

void ScratchEditor::askSavePattern()
{
    const auto& ph = proc.getPhrase();
    const auto name = ph.isRandom ? u8 (japanese ? "ランダム " : "Random ") + juce::String ((int) (ph.seed % 10000))
                                  : (ph.myName.isNotEmpty() ? ph.myName : u8 (kTemplateNames[ph.templateIndex][japanese ? 1 : 0]));
    prompt.open (u8 (japanese ? "マイパターン" : "MY PATTERNS"),
                 u8 (japanese ? "名前を付けて保存します。" : "Save the current blocks under a name."), true, name,
                 u8 (japanese ? "保存" : "SAVE"), u8 (japanese ? "キャンセル" : "CANCEL"),
                 [this] (juce::String typed)
                 {
                     const auto clean = ScratchProcessor::cleanPatternName (typed);
                     if (clean.isEmpty())
                         return;
                     if (! proc.myPatternExists (clean))
                     {
                         savePattern (clean);
                         return;
                     }
                     prompt.open (u8 (japanese ? "マイパターン" : "MY PATTERNS"),
                                  u8 (japanese ? "「" : "\"") + clean + u8 (japanese ? "」はもうあります。上書きしますか?" : "\" already exists. Overwrite it?"), false, {},
                                  u8 (japanese ? "上書き" : "OVERWRITE"), u8 (japanese ? "キャンセル" : "CANCEL"),
                                  [this, clean] (juce::String) { savePattern (clean); });
                 });
}

void ScratchEditor::savePattern (const juce::String& name)
{
    if (! proc.saveMyPattern (name))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Cue Burn",
                                                u8 (japanese ? "保存できませんでした: " : "Could not save: ") + name);
    rebuildTemplateList();
    refreshFillControls();
}

void ScratchEditor::askDeletePattern (const juce::String& name)
{
    prompt.open (u8 (japanese ? "マイパターン" : "MY PATTERNS"),
                 u8 (japanese ? "「" : "Delete \"") + name + u8 (japanese ? "」を削除しますか? (ごみ箱に移します)" : "\"? (It goes to the Recycle Bin)"), false, {},
                 u8 (japanese ? "削除" : "DELETE"), u8 (japanese ? "キャンセル" : "CANCEL"),
                 [this, name] (juce::String)
                 {
                     if (! proc.deleteMyPattern (name))
                         juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Cue Burn",
                                                                 u8 (japanese ? "削除できませんでした: " : "Could not delete: ") + name);
                     rebuildTemplateList();
                     refreshFillControls();
                 });
}

//==============================================================================
PatternPrompt::PatternPrompt()
{
    field.setFont (fontOf (15.0f, true));
    field.setInputRestrictions (60);
    field.onReturnKey = [this] { accept(); };
    field.onEscapeKey = [this] { setVisible (false); };
    okButton.onClick = [this] { accept(); };
    cancelButton.onClick = [this] { setVisible (false); };
    addAndMakeVisible (field);
    addAndMakeVisible (okButton);
    addAndMakeVisible (cancelButton);
    setWantsKeyboardFocus (true);
    setVisible (false);
}

void PatternPrompt::open (const juce::String& newTitle, const juce::String& newMessage, bool askName, const juce::String& name,
                          const juce::String& okText, const juce::String& cancelText, std::function<void (juce::String)> onOk)
{
    field.setColour (juce::TextEditor::backgroundColourId, deck::pick (juce::Colour (0xff0b0e12), juce::Colours::white));
    field.setColour (juce::TextEditor::textColourId, deck::pick (deck::lcdBlue, deck::whiteInk));
    field.setColour (juce::TextEditor::outlineColourId, deck::pick (juce::Colour (0xff3a4048), juce::Colour (0xffcbd1d9)));
    field.setColour (juce::TextEditor::focusedOutlineColourId, deck::pick (deck::ledAmber, deck::whiteTeal));
    field.setColour (juce::CaretComponent::caretColourId, deck::pick (deck::lcdBlue, deck::whiteInk));
    title = newTitle;
    message = newMessage;
    action = std::move (onOk);
    okButton.setButtonText (okText);
    cancelButton.setButtonText (cancelText);
    field.setVisible (askName);
    field.setText (name, juce::dontSendNotification);
    setVisible (true);
    toFront (true);
    resized();
    repaint();
    if (askName)
    {
        field.grabKeyboardFocus();
        field.selectAll();
    }
    else
        grabKeyboardFocus();
}

juce::Rectangle<int> PatternPrompt::boxBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (400, 158);
}

void PatternPrompt::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (deck::white() ? 0.28f : 0.6f));
    const auto box = boxBounds();
    deck::drawMetalPanel (g, box.toFloat(), deck::ledAmber, title, hasWide (title));
    g.setColour (deck::pick (juce::Colours::white.withAlpha (0.85f), deck::whiteInk));
    g.setFont (fontOf (13.0f, false));
    g.drawFittedText (message, box.reduced (20, 0).withTrimmedTop (38).withHeight (24), juce::Justification::centredLeft, 2);
}

void PatternPrompt::resized()
{
    auto box = boxBounds().reduced (20, 0).withTrimmedTop (66);
    field.setBounds (box.removeFromTop (30));
    box = boxBounds().reduced (20, 0).withTrimmedTop (108).withHeight (32);
    cancelButton.setBounds (box.removeFromRight (110));
    box.removeFromRight (8);
    okButton.setBounds (box.removeFromRight (110));
}

bool PatternPrompt::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::returnKey)
        accept();
    else if (k == juce::KeyPress::escapeKey)
        setVisible (false);
    else
        return false;
    return true;
}

void PatternPrompt::accept()
{
    const auto f = action; // open() で、次の確認に置き換わることがある
    const auto text = field.getText();
    setVisible (false);
    if (f != nullptr)
        f (text);
}

void ScratchEditor::refreshFillControls()
{
    const auto& ph = proc.getPhrase();
    const auto edited = u8 (japanese ? " (編集済み)" : " (edited)");
    if (ph.isRandom)
        tmplBox.setText (u8 (japanese ? "ランダム #" : "Random #") + juce::String ((int) (ph.seed % 10000)), juce::dontSendNotification);
    else if (ph.myName.isNotEmpty())
        tmplBox.setText (ph.myName + (ph.edited ? edited : juce::String()), juce::dontSendNotification);
    else if (ph.edited)
        tmplBox.setText (u8 (kTemplateNames[ph.templateIndex][japanese ? 1 : 0]) + edited, juce::dontSendNotification);
    else
        tmplBox.setSelectedId (ph.templateIndex + 1, juce::dontSendNotification);

    const int barsIdx = ph.bars == 1 ? 0 : (ph.bars == 2 ? 1 : 2);
    for (int i = 0; i < 3; ++i)
        barButtons[(size_t) i].setToggleState (i == barsIdx, juce::dontSendNotification);
    timeline.repaint();
    patternEditor.repaint();
    for (auto& t : patternTiles)
        t->repaint();
}

void ScratchEditor::showPatternEditor (int pattern)
{
    editorView = true;
    patternEditor.setPattern (pattern);
    timerCallback();
    repaint();
}

void ScratchEditor::timerCallback()
{
    const bool fillMode = (int) param ("mode") == 0;
    modeFill.setToggleState (! editorView && fillMode, juce::dontSendNotification);
    modeSingle.setToggleState (! editorView && ! fillMode, juce::dontSendNotification);
    modeEdit.setToggleState (editorView, juce::dontSendNotification);

    // フィルの操作 (波形エディタでは隠す。単発では薄く)
    for (juce::Component* c : { (juce::Component*) &tmplPrev, (juce::Component*) &tmplNext, (juce::Component*) &tmplBox, (juce::Component*) &myButton,
                                (juce::Component*) &randomButton, (juce::Component*) &varyButton,
                                (juce::Component*) &barButtons[0], (juce::Component*) &barButtons[1], (juce::Component*) &barButtons[2],
                                (juce::Component*) &gridBox, (juce::Component*) &gridLabel, (juce::Component*) &muteButton })
    {
        c->setVisible (! editorView);
        c->setAlpha (fillMode ? 1.0f : 0.3f);
    }
    backButton.setAlpha (fillMode || editorView ? 1.0f : 0.3f);
    backButton.setEnabled (editorView ? proc.canGoBackUser() : proc.canGoBack());
    copyBox.setVisible (editorView);
    timeline.setVisible (! editorView && fillMode);
    patternEditor.setVisible (editorView);
    for (auto& t : patternTiles)
        t->setVisible (! editorView && ! fillMode);
    const bool rateUsed = ! fillMode || editorView; // 速さのコンボは、単発と波形エディタの試聴で使う
    rateCombo->box.setAlpha (rateUsed ? 1.0f : 0.32f);
    rateCombo->label.setAlpha (rateUsed ? 1.0f : 0.32f);
    proc.previewPattern.store (editorView ? patternEditor.pattern : -1);
    previewButton.setToggleState (proc.previewOn.load(), juce::dontSendNotification);

    const int v = proc.phraseChanged.load();
    if (v != shownPhraseVersion)
    {
        shownPhraseVersion = v;
        refreshFillControls();
    }
    if (proc.uiFillBeat.load() >= 0.0f || timeline.isMouseOver())
        timeline.repaint();

    shownOut = proc.uiOutDb.load();

    // プリセットの表示 (自分で読み込んだ音なら、何も選ばない)
    if (const int pi = proc.getPresetIndex(); pi != shownPreset)
    {
        shownPreset = pi;
        presetBox.setSelectedId (pi >= 0 ? pi + 1 : 0, juce::dontSendNotification);
    }

    const bool sampleMode = (int) param ("src") == ScratchProcessor::srcSample;
    startKnob->slider.setAlpha (sampleMode ? 1.0f : 0.32f);
    startKnob->label.setAlpha (sampleMode ? 1.0f : 0.32f);

    if (shownSkin != deck::currentSkin()) // 別のトラックの画面で切り替えた時
        applySkin();

    repaint (header);
    platter.repaint();
    sourceView.repaint();
    sourceTiles[(size_t) ScratchProcessor::srcSample]->repaint();
}

void ScratchEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    header = area.removeFromTop (44);
    {
        auto langArea = header.withSizeKeepingCentre (header.getWidth(), 24).removeFromRight (90);
        enButton.setBounds (langArea.removeFromLeft (38));
        langArea.removeFromLeft (6);
        jaButton.setBounds (langArea);
        skinButton.setBounds (header.withSizeKeepingCentre (header.getWidth(), 24).removeFromRight (398).removeFromLeft (92));
    }
    area.removeFromTop (6);

    auto top = area.removeFromTop (250);
    platterArea = top.removeFromLeft (300);
    top.removeFromLeft (10);
    sourceArea = top;
    area.removeFromTop (10);
    fillArea = area.removeFromTop (232);
    area.removeFromTop (10);

    platter.setBounds (platterArea.withTrimmedTop (26).reduced (8, 2));

    // 音源: タイル5つ + 読み込むボタン、その下に表示
    {
        auto inner = sourceArea.reduced (12, 8);
        inner.removeFromTop (20);
        auto row = inner.removeFromTop (52);
        {
            auto col = row.removeFromRight (168);
            presetBox.setBounds (col.removeFromTop (25));
            col.removeFromTop (3);
            loadButton.setBounds (col.removeFromTop (24));
        }
        row.removeFromRight (8);
        const int gap = 6;
        const int w = (row.getWidth() - gap * (ScratchProcessor::numSources - 1)) / ScratchProcessor::numSources;
        for (int i = 0; i < ScratchProcessor::numSources; ++i)
            sourceTiles[(size_t) i]->setBounds (row.getX() + i * (w + gap), row.getY(), w, row.getHeight());
        inner.removeFromTop (8);
        sourceView.setBounds (inner);
    }

    // フィル: 操作2段 + タイムライン (単発モードでは、パターンのタイル)
    {
        auto inner = fillArea.reduced (12, 8);
        inner.removeFromTop (20);

        auto rowA = inner.removeFromTop (28);
        modeFill.setBounds (rowA.removeFromLeft (72));
        rowA.removeFromLeft (4);
        modeSingle.setBounds (rowA.removeFromLeft (72));
        rowA.removeFromLeft (4);
        modeEdit.setBounds (rowA.removeFromLeft (112));
        rowA.removeFromLeft (16);
        copyBox.setBounds (rowA.withWidth (260));
        tmplPrev.setBounds (rowA.removeFromLeft (30));
        rowA.removeFromLeft (4);
        tmplBox.setBounds (rowA.removeFromLeft (200));
        rowA.removeFromLeft (4);
        tmplNext.setBounds (rowA.removeFromLeft (30));
        rowA.removeFromLeft (6);
        myButton.setBounds (rowA.removeFromLeft (104));
        for (int i = 2; i >= 0; --i)
        {
            barButtons[(size_t) i].setBounds (rowA.removeFromRight (70));
            rowA.removeFromRight (4);
        }
        inner.removeFromTop (6);

        auto rowB = inner.removeFromTop (28);
        randomButton.setBounds (rowB.removeFromLeft (110));
        rowB.removeFromLeft (6);
        varyButton.setBounds (rowB.removeFromLeft (130));
        rowB.removeFromLeft (6);
        backButton.setBounds (rowB.removeFromLeft (90));
        previewButton.setBounds (rowB.removeFromRight (120));
        rowB.removeFromRight (10);
        muteButton.setBounds (rowB.removeFromRight (100));
        rowB.removeFromRight (10);
        gridBox.setBounds (rowB.removeFromRight (96));
        gridLabel.setBounds (rowB.removeFromRight (64));
        inner.removeFromTop (8);

        prompt.setBounds (getLocalBounds());
        fillLowerArea = inner;
        timeline.setBounds (inner);
        patternEditor.setBounds (inner);

        // 単発のパターン: 2段 x 9 (右の4つはユーザー波形)
        const int gap = 8;
        const int cols = (ScratchProcessor::numPatterns + 1) / 2;
        const int w = (inner.getWidth() - gap * (cols - 1)) / cols;
        const int h = (inner.getHeight() - gap) / 2;
        for (int i = 0; i < ScratchProcessor::numPatterns; ++i)
            patternTiles[(size_t) i]->setBounds (inner.getX() + (i % cols) * (w + gap), inner.getY() + (i / cols) * (h + gap), w, h);
    }

    auto row = area;
    const int gap = 10;
    const int w = row.getWidth() - gap * 2;
    panels[0].area = row.removeFromLeft (w * 44 / 100);
    row.removeFromLeft (gap);
    panels[1].area = row.removeFromLeft (w * 34 / 100);
    row.removeFromLeft (gap);
    panels[2].area = row;

    // 動き: 速さのコンボ(単発モード用) + ノブ2つ
    {
        auto inner = panels[0].area.reduced (12, 8);
        inner.removeFromTop (18);
        auto row1 = inner.removeFromTop (26);
        rateCombo->label.setBounds (row1.removeFromLeft (60));
        rateCombo->box.setBounds (row1.removeFromLeft (110).reduced (4, 0));
        const int kw = inner.getWidth() / (int) panels[0].knobs.size();
        for (auto* k : panels[0].knobs)
        {
            auto cell = inner.removeFromLeft (kw);
            k->label.setBounds (cell.removeFromTop (14));
            k->slider.setBounds (cell);
        }
    }

    for (int i = 1; i < 3; ++i)
    {
        auto inner = panels[(size_t) i].area.reduced (12, 8);
        inner.removeFromTop (18);
        inner.removeFromTop (26); // 「動き」と高さを揃える
        const int kw = inner.getWidth() / (int) panels[(size_t) i].knobs.size();
        for (auto* k : panels[(size_t) i].knobs)
        {
            auto cell = inner.removeFromLeft (kw);
            k->label.setBounds (cell.removeFromTop (14));
            k->slider.setBounds (cell);
        }
    }
}

//==============================================================================
void ScratchEditor::paint (juce::Graphics& g)
{
    deck::drawChassis (g, getLocalBounds().toFloat());

    paintHeader (g);

    drawPanelFrame (g, platterArea.toFloat(), teal, u8 (kRecordTitle[japanese ? 1 : 0]), japanese);
    drawPanelFrame (g, sourceArea.toFloat(), teal, u8 (kSourceTitle[japanese ? 1 : 0]), japanese);
    drawPanelFrame (g, fillArea.toFloat(), orange, editorView ? u8 (japanese ? "波形エディタ" : "WAVE EDITOR")
                                                               : u8 ((int) param ("mode") == 0 ? kFillTitle[japanese ? 1 : 0] : kPatternTitle[japanese ? 1 : 0]), japanese);

    for (auto& p : panels)
        drawPanelFrame (g, p.area.toFloat(), p.colour, u8 (kPanelTitles[p.titleIndex][japanese ? 1 : 0]), japanese);
}

void ScratchEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragHover)
        return;
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    g.setColour (juce::Colour (0xcc0e1117));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (orange);
    g.drawRoundedRectangle (r, 12.0f, 3.0f);
    g.setFont (fontOf (20.0f, true, 0.1f));
    g.drawText (u8 (japanese ? "ドロップして、サンプルを読み込む" : "DROP TO LOAD THE SAMPLE"), r, juce::Justification::centred);
}

void ScratchEditor::paintHeader (juce::Graphics& g)
{
    auto h = header.toFloat();

    if (deck::white())
    {
        // ロゴ: グラフィティの書体 (Sedgwick Ave Display)
        g.setFont (deck::logoFont (36.0f));
        g.setColour (deck::whiteInk);
        g.drawText ("Cue Burn", h.withWidth (220.0f).translated (0.0f, -1.0f), juce::Justification::centredLeft);
        const float lw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), "Cue Burn");
        g.setFont (fontOf (10.0f, true, 0.3f));
        g.setColour (deck::whiteTeal);
        g.drawText ("DIGITAL SCRATCH DECK", (int) (h.getX() + lw + 16.0f), (int) h.getY() + 4, 240, (int) h.getHeight(), juce::Justification::centredLeft);
    }
    else
    {
        // ロゴ: シルク印刷 (影で少し浮かせる)
        deck::drawChromeText (g, "CUE BURN", h.withWidth (200.0f), 28.0f, deck::ledAmber);
        g.setFont (fontOf (10.0f, true, 0.3f));
        g.setColour (deck::ledAmber.withAlpha (0.85f));
        g.drawText ("DIGITAL SCRATCH DECK", (int) h.getX() + 184, (int) h.getY() + 6, 240, (int) h.getHeight(), juce::Justification::centredLeft);
    }

    // 出力メーター: LED の列
    auto right = h;
    right.removeFromRight (104); // 言語ボタン
    auto box = right.removeFromRight (190);
    g.setFont (fontOf (10.0f, true, 0.15f));
    g.setColour (deck::pick (mutedCol, deck::whiteMuted));
    g.drawText ("OUT", (int) box.getX(), (int) box.getY(), 30, (int) box.getHeight(), juce::Justification::centredLeft);
    auto meter = box.withTrimmedLeft (34).withSizeKeepingCentre (box.getWidth() - 34.0f, 10.0f);
    deck::drawLcd (g, meter.expanded (3.0f, 3.0f), 3.0f);
    const float lv = juce::jlimit (0.0f, 1.0f, (shownOut + 60.0f) / 60.0f);
    constexpr int n = 20;
    const float sw = meter.getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) (i + 1) / (float) n;
        const auto col = i >= n - 2 ? juce::Colour (0xffff4d4d) : (i >= n - 6 ? deck::ledAmber : juce::Colour (0xff58e0a0));
        const bool on = lv >= t - 0.5f / (float) n;
        auto seg = juce::Rectangle<float> (meter.getX() + (float) i * sw + 0.8f, meter.getY(), sw - 1.6f, meter.getHeight());
        g.setColour (on ? col : col.withMultipliedBrightness (0.22f));
        g.fillRoundedRectangle (seg, 1.0f);
        if (on)
        {
            g.setColour (col.withAlpha (0.25f));
            g.fillRoundedRectangle (seg.expanded (1.0f, 1.5f), 1.5f);
        }
    }
}
