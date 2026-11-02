#include "UiKit.h"

namespace ui
{
static juce::Font (*fontOverride) (float, bool) = nullptr;

void setFontOverride (juce::Font (*fn) (float, bool))
{
    fontOverride = fn;
}

juce::Font fontOf (float size, bool bold, float kerning)
{
    if (fontOverride != nullptr)
        return fontOverride (size, bold).withExtraKerningFactor (kerning);

    auto o = juce::FontOptions (size);
    if (bold)
        o = o.withStyle ("Bold");
    return juce::Font (o).withExtraKerningFactor (kerning);
}

juce::String u8 (const char* utf8)
{
    return juce::String::fromUTF8 (utf8);
}

bool hasWide (const juce::String& text)
{
    for (auto c : text)
        if (c > 127)
            return true;
    return false;
}
}

using namespace ui;

namespace
{
juce::TextLayout tooltipLayout (const juce::String& text, juce::Colour ink)
{
    juce::AttributedString a;
    a.setJustification (juce::Justification::centredLeft);
    a.append (text, fontOf (12.5f), ink);
    a.setWordWrap (hasWide (text) ? juce::AttributedString::byChar : juce::AttributedString::byWord);
    juce::TextLayout tl;
    tl.createLayout (a, 300.0f);
    return tl;
}
}

//==============================================================================
PluginLookAndFeel::PluginLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1b1f29));
    setColour (juce::PopupMenu::textColourId, textCol);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2c3342));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ComboBox::textColourId, textCol);
    setColour (juce::Label::textColourId, textCol);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf0222735));
    setColour (juce::TooltipWindow::outlineColourId, juce::Colour (0xff3d4558));
    setColour (juce::TooltipWindow::textColourId, textCol);
}

juce::Font PluginLookAndFeel::getComboBoxFont (juce::ComboBox&) { return fontOf (12.5f); }
juce::Font PluginLookAndFeel::getLabelFont (juce::Label&) { return fontOf (12.0f); }
juce::Font PluginLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return fontOf (11.5f, true, 0.05f); }

void PluginLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool isOver, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setColour (on ? orange : (isOver ? juce::Colour (0xff2a3040) : juce::Colour (0xff1e232e)));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? orange : panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

juce::Rectangle<int> PluginLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    auto tl = tooltipLayout (text, textCol);
    const int w = (int) std::ceil (tl.getWidth()) + 24;
    const int h = (int) std::ceil (tl.getHeight()) + 16;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 20,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 8, w, h)
        .constrainedWithin (parentArea);
}

void PluginLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (findColour (juce::TooltipWindow::outlineColourId));
    g.drawRoundedRectangle (r, 7.0f, 1.0f);
    tooltipLayout (text, findColour (juce::TooltipWindow::textColourId)).draw (g, r.reduced (12.0f, 8.0f));
}

void PluginLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 28, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void PluginLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool isDown, int, int, int, int, juce::ComboBox& box)
{
    auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    const auto col = box.findColour (juce::ComboBox::outlineColourId);
    g.setColour (isDown ? juce::Colour (0xff262c39) : juce::Colour (0xff1e232e));
    g.fillRoundedRectangle (b, 7.0f);
    g.setColour (col.withAlpha (0.55f));
    g.drawRoundedRectangle (b, 7.0f, 1.0f);

    juce::Path arrow;
    const float cx = (float) w - 13.0f, cy = (float) h * 0.5f;
    arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (col);
    g.fillPath (arrow);
}

void PluginLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle,
                                          float endAngle, juce::Slider& s)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float d = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float r = d * 0.5f;
    const auto fill = s.findColour (juce::Slider::rotarySliderFillColourId);
    const float angle = startAngle + pos * (endAngle - startAngle);

    const float arcR = r * 0.9f;
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff262b37));
    g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    float from = startAngle;
    if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0)
        from = startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle);
    juce::Path val;
    val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (fill.withAlpha (0.22f));
    g.strokePath (val, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (fill);
    g.strokePath (val, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float bodyR = r * 0.68f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff363c4a), c.x, c.y - bodyR,
                                             juce::Colour (0xff14171e), c.x, c.y + bodyR, false));
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (juce::Colour (0xff414859));
    g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    juce::Path p;
    p.addRoundedRectangle (-1.6f, -bodyR + 3.5f, 3.2f, bodyR * 0.5f, 1.6f);
    p.applyTransform (juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    g.setColour (juce::Colours::white);
    g.fillPath (p);
}

//==============================================================================
void HelpLabel::setLabel (const juce::String& newText, juce::Justification j)
{
    text = newText;
    just = j;
    repaint();
}

void HelpLabel::paint (juce::Graphics& g)
{
    const auto f = fontOf (hasWide (text) ? 11.5f : 10.5f, true, 0.08f);
    const float tw = juce::GlyphArrangement::getStringWidth (f, text);
    const float icon = 12.0f, gapW = 4.0f;
    const float total = showIcon ? tw + gapW + icon : tw;
    auto b = getLocalBounds().toFloat();
    const float x = just == juce::Justification::centredRight ? b.getRight() - total
                  : (just == juce::Justification::centredLeft ? b.getX() : b.getCentreX() - total * 0.5f);

    g.setFont (f);
    g.setColour (hover ? hoverColour : idleColour);
    g.drawText (text, juce::Rectangle<float> (x, b.getY(), tw + 2.0f, b.getHeight()), juce::Justification::centredLeft, false);
    if (! showIcon)
        return;

    const auto ic = juce::Rectangle<float> (x + tw + gapW, b.getCentreY() - icon * 0.5f, icon, icon);
    g.setColour (hover ? iconHover : idleColour.withAlpha (0.8f));
    g.drawEllipse (ic.reduced (0.5f), 1.0f);
    g.setFont (fontOf (9.0f, true));
    g.drawText ("?", ic.translated (0.0f, -0.5f), juce::Justification::centred, false);
}
