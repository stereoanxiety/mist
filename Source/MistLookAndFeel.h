#pragma once

#include <JuceHeader.h>

// Mist's visual language: dark, tactile, with a vapor-seafoam accent. The signature
// element is a segmented LED arc around the rotary that lights up seafoam->mint toward the
// top of travel, with a soft glow underneath, a metallic knob body, and restrained
// typography from the Stereo Anxiety Chrome Studio system.
class MistLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // palette — dark panel shared with Dust/Haze; accent retuned for Mist (vapor seafoam).
    const juce::Colour bgTop   { 0xff15191a };
    const juce::Colour bgBot   { 0xff070909 };
    const juce::Colour panel   { 0xff15191a };
    const juce::Colour edge    { 0xff263235 };
    const juce::Colour track    { 0xff263235 };
    const juce::Colour teal    { 0xff5cb0a6 };   // accent (vapor seafoam) — was Dust teal
    const juce::Colour chrome  { 0xffcbeae2 };   // hot/peak (pale mint mist) — was Dust chrome
    const juce::Colour cream    { 0xffeceff4 };
    const juce::Colour dim     { 0xff91a4aa };
    const juce::Colour knobHi  { 0xff263235 };
    const juce::Colour knobLo  { 0xff070909 };

    MistLookAndFeel()
    {
        setColour (juce::ComboBox::backgroundColourId, panel);
        setColour (juce::ComboBox::textColourId,       cream);
        setColour (juce::ComboBox::outlineColourId,    edge);
        setColour (juce::ComboBox::arrowColourId,      teal);
        setColour (juce::PopupMenu::backgroundColourId,            panel);
        setColour (juce::PopupMenu::textColourId,                  cream);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, teal.withAlpha (0.35f));
        setColour (juce::PopupMenu::highlightedTextColourId,       cream);
        setColour (juce::TextButton::buttonColourId,   panel);
        setColour (juce::TextButton::buttonOnColourId, teal);
        setColour (juce::TextButton::textColourOffId,  cream);
        setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff1a1512));
        setColour (juce::Label::textColourId,          dim);
        setColour (juce::ToggleButton::textColourId,         cream);
        setColour (juce::ToggleButton::tickColourId,         teal);
        setColour (juce::ToggleButton::tickDisabledColourId, edge);

        knobImage = juce::ImageCache::getFromMemory (BinaryData::knob_png, BinaryData::knob_pngSize);
        labelFace = juce::Typeface::createSystemTypefaceFor (BinaryData::GeistRegular_ttf,
                                                             BinaryData::GeistRegular_ttfSize);
        titleFace = juce::Typeface::createSystemTypefaceFor (BinaryData::Abuget_ttf,
                                                             BinaryData::Abuget_ttfSize);
    }

    // Audio-reactive knob glow, fed by the editor's metering timer each frame:
    // level (0..1) = envelope-followed output, hot (0..1) = near-clip flash.
    void setKnobGlow (float level, float hotIn) noexcept { glowLevel = level; glowHot = hotIn; }

    // Default any stray text to the embedded Stereo Anxiety UI face.
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
    {
        return labelFace != nullptr ? labelFace : juce::LookAndFeel_V4::getTypefaceForFont (f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle,
                           juce::Slider& s) override
    {
        const bool en = s.isEnabled();
        auto area = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
        const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const auto  c      = area.getCentre();
        const float angle  = startAngle + pos * (endAngle - startAngle);
        const float arcR   = radius - 3.0f;

        // soft static shadow under the knob (the rotating image carries none of its own)
        {
            const float kr = radius * 0.82f;
            juce::Path kp; kp.addEllipse (c.x - kr, c.y - kr + 3.0f, kr * 2.0f, kr * 2.0f);
            juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 14, { 0, 4 }).drawForPath (g, kp);
        }

        // Audio-reactive LED glow. The editor's metering timer feeds setKnobGlow() an
        // envelope-followed output level (glowLevel) and a near-clip amount (glowHot)
        // every frame. Knob position still decides HOW MANY segments are lit (the coloured
        // dots), but their bloom pulses with the program and flares toward lilac on
        // clipping — real LED-indicator behaviour, not a static position glow.
        const float glow = en ? glowLevel : 0.0f;   // 0..1 program level
        const float hot  = en ? glowHot   : 0.0f;   // 0..1 near-clip flash

        // soft ambient bloom under the lit run — widens/brightens with program level
        if (en && pos > 0.001f)
        {
            juce::Path glowPath;
            glowPath.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
            g.setColour (teal.withAlpha (0.03f + 0.08f * glow + 0.07f * hot));
            g.strokePath (glowPath, { 9.0f + 8.0f * glow, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
            g.setColour (teal.withAlpha (0.05f + 0.09f * glow));
            g.strokePath (glowPath, { 7.0f + 3.0f * glow, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }

        // segmented LED arc (greys out entirely when disabled). The crisp core of each lit
        // segment is the always-on coloured dot (position); the halo + inner glow pulse
        // with the program level and flare toward lilac (hot) as the signal nears clip.
        const int   nSeg = 30;
        const float gap  = 0.045f;
        for (int i = 0; i < nSeg; ++i)
        {
            const float t0 = (float) i / (float) nSeg;
            const float t1 = (float) (i + 1) / (float) nSeg;
            const float a0 = startAngle + (t0 + gap * 0.5f) * (endAngle - startAngle);
            const float a1 = startAngle + (t1 - gap * 0.5f) * (endAngle - startAngle);
            const bool  lit = en && (t0 + 0.5f / nSeg) <= pos;

            juce::Path seg;
            seg.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, a0, a1, true);

            if (lit)
            {
                juce::Colour led = teal.interpolatedWith (chrome, t0);
                led = led.interpolatedWith (chrome, hot * 0.7f);                    // flush hot near clip
                g.setColour (led.withAlpha (0.04f + 0.16f * glow + 0.15f * hot));   // outer halo
                g.strokePath (seg, { 5.0f + 4.0f * glow + 3.0f * hot, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
                g.setColour (led.withAlpha (0.14f + 0.22f * glow + 0.14f * hot));   // inner glow
                g.strokePath (seg, { 5.0f + 2.0f * glow,              juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
                g.setColour (led.brighter (0.04f + 0.16f * glow + 0.25f * hot));    // always-on core
                g.strokePath (seg, { 5.0f,                            juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
            }
            else
            {
                g.setColour (track);
                g.strokePath (seg, { 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
            }
        }

        // metallic knob body — rotated image (indicator baked at 12 o'clock)
        const float kr = radius * 0.82f;
        if (knobImage.isValid())
        {
            const float scale = (kr * 2.0f) / (float) knobImage.getWidth();
            const auto t = juce::AffineTransform::scale (scale)
                               .translated (c.x - kr, c.y - kr)
                               .rotated (angle, c.x, c.y);
            if (! en) g.setOpacity (0.5f);
            g.drawImageTransformed (knobImage, t);
            g.setOpacity (1.0f);
        }
        else   // fallback: simple disc + pointer
        {
            g.setGradientFill ({ knobHi, c.x, c.y - kr, knobLo, c.x, c.y + kr, false });
            g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
            juce::Path ptr;
            const float pw = juce::jmax (2.0f, kr * 0.08f);
            ptr.addRoundedRectangle (-pw * 0.5f, -kr * 0.92f, pw, kr * 0.40f, pw * 0.5f);
            g.setColour (en ? cream : dim.withAlpha (0.5f));
            g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
        }

    }

    // Build a Font carrying the embedded typeface explicitly. Typeface-less Fonts get
    // resolved against the host-owned *default* LookAndFeel (not this one), so attaching
    // the face to every Font we draw is the only reliable way to apply an embedded font.
    juce::Font withFace (float height) const
    {
        auto o = juce::FontOptions().withHeight (height);
        if (labelFace != nullptr) o = o.withTypeface (labelFace);
        return juce::Font (o);
    }

    // Title face — Abuget (brush script), used only for the product name "Mist".
    juce::Font withTitleFace (float height) const
    {
        auto o = juce::FontOptions().withHeight (height);
        if (titleFace != nullptr) o = o.withTypeface (titleFace);
        return juce::Font (o);
    }

    juce::Font getLabelFont    (juce::Label& l)  override
    {
        return l.getComponentID() == "title" ? l.getFont()   // title carries its own (Abuget) font
                                             : withFace (17.0f);
    }

    // Carved/engraved title: a dark bevel peeking above each stroke and a light catch
    // below it (lit from the top), with a grey face on top — reads as pressed into the
    // metal. Only the "title" label; everything else uses the default label draw.
    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        if (label.getComponentID() != "title")
            return juce::LookAndFeel_V4::drawLabel (g, label);

        const auto txt  = label.getText();
        const auto just = label.getJustificationType();
        const auto box  = label.getLocalBounds().toFloat();
        g.setFont (label.getFont());

        // No opaque fill — a solid colour reads as printed/raised. The letter is a
        // groove in the metal: the texture shows through a darkened interior, the top
        // wall falls in shadow, the bottom wall catches light.
        g.setColour (juce::Colours::black.withAlpha (0.80f));
        g.drawText (txt, box.translated (0.0f, -0.8f), just, false);   // shadowed top wall
        g.setColour (juce::Colours::white.withAlpha (0.50f));
        g.drawText (txt, box.translated (0.0f,  1.0f), just, false);   // lit bottom wall
        g.setColour (juce::Colours::black.withAlpha (0.32f));
        g.drawText (txt, box, just, false);                            // recessed interior (texture shows)
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return withFace (15.0f); }
    juce::Font getPopupMenuFont()                override { return withFace (15.0f); }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                           bool over, bool down) override
    {
        const float fontSize  = juce::jmin (16.0f, (float) b.getHeight() * 0.95f);
        const float tickWidth = fontSize * 1.1f;
        const auto  font      = withFace (fontSize);
        const float gap       = 8.0f;
        const float textW     = juce::GlyphArrangement::getStringWidth (font, b.getButtonText());
        // centre the [tick + text] group within the button bounds (so it reads centred)
        const float startX    = juce::jmax (0.0f, ((float) b.getWidth() - (tickWidth + gap + textW)) * 0.5f);

        drawTickBox (g, b, startX, ((float) b.getHeight() - tickWidth) * 0.5f,
                     tickWidth, tickWidth, b.getToggleState(), b.isEnabled(), over, down);
        g.setColour (b.findColour (juce::ToggleButton::textColourId));
        g.setFont (font);
        if (! b.isEnabled()) g.setOpacity (0.5f);
        g.drawText (b.getButtonText(),
                    juce::Rectangle<float> (startX + tickWidth + gap, 0.0f, textW + 4.0f, (float) b.getHeight()),
                    juce::Justification::centredLeft);
    }

private:
    juce::Image         knobImage;   // rotating Cutoff knob (BinaryData)
    juce::Typeface::Ptr labelFace;   // Geist Regular — all text
    juce::Typeface::Ptr titleFace;   // Abuget — product-name title only
    float               glowLevel = 0.0f;   // program level driving the lit-segment bloom (0..1)
    float               glowHot   = 0.0f;   // near-clip flash amount (0..1)
};
