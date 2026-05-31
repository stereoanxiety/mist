#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MistLookAndFeel.h"

// A console-style metering lamp (non-interactive). brightness (0..1) sets how lit the
// glass is and hot (0..1) crossfades the colour accent -> mint near clipping. The editor
// feeds it an envelope-followed output peak so it pulses/blinks in time with the program.
class LedLamp : public juce::Component
{
public:
    LedLamp() { setInterceptsMouseClicks (false, false); }

    void setImages (juce::Image tealImg, juce::Image chromeImg)
    {
        teal = std::move (tealImg); chrome = std::move (chromeImg); repaint();
    }

    void setState (float brightnessIn, float hotIn)
    {
        brightness = brightnessIn; hot = hotIn; repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto fb = getLocalBounds().toFloat();
        if (teal.isValid())  g.drawImage (teal, fb, juce::RectanglePlacement::centred);
        if (chrome.isValid() && hot > 0.001f)
        {
            g.setOpacity (hot);                              // crossfade to chrome when hot
            g.drawImage (chrome, fb, juce::RectanglePlacement::centred);
            g.setOpacity (1.0f);
        }

        const auto glass = fb.reduced (fb.getWidth() * 0.21f);   // inner lens only
        const float dim  = juce::jlimit (0.0f, 1.0f, 1.0f - brightness);
        if (dim > 0.001f)                                   // darken the lens when quiet
        {
            g.setColour (juce::Colours::black.withAlpha (dim * 0.74f));
            g.fillEllipse (glass);
        }
        if (brightness > 0.02f)                             // additive bloom when lit
        {
            const auto c = (hot > 0.5f) ? juce::Colour (0xffcbeae2) : juce::Colour (0xff5cb0a6);
            g.setColour (c.withAlpha (brightness * 0.30f));
            g.fillEllipse (fb.reduced (fb.getWidth() * 0.25f));
        }
    }

private:
    juce::Image teal, chrome;
    float brightness = 0.0f, hot = 0.0f;
};

// A two-state image toggle (latching). Shows onImg when ON, offImg when OFF. Bound to a
// parameter via a normal ButtonAttachment; the toggle state maps to the param directly.
class ImageToggle : public juce::Button
{
public:
    ImageToggle() : juce::Button ("toggle") { setClickingTogglesState (true); }

    // showOnWhenToggled=false: draw the ON image when the toggle is OFF (used to invert,
    // e.g. bound to "bypass" where bypass-off == powered-on).
    void setImages (juce::Image onImage, juce::Image offImage, bool showOnWhenToggled)
    {
        onImg = std::move (onImage); offImg = std::move (offImage);
        invert = ! showOnWhenToggled; repaint();
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const bool showOn = invert ? ! getToggleState() : getToggleState();
        const juce::Image& img = showOn ? onImg : offImg;
        if (img.isValid())
            g.drawImage (img, getLocalBounds().toFloat(), juce::RectanglePlacement::centred);
        if (over || down)
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.10f : 0.05f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced ((float) getWidth() * 0.10f), 8.0f);
        }
    }

private:
    juce::Image onImg, offImg;
    bool invert = false;
};

class MistAudioProcessorEditor : public juce::AudioProcessorEditor,
                                 private juce::Timer
{
public:
    explicit MistAudioProcessorEditor (MistAudioProcessor&);
    ~MistAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;            // drives the activity LED

private:
    MistAudioProcessor& processor;
    MistLookAndFeel     lnf;                   // declared first → outlives the components

    juce::Image          bgImage;             // panel background (BinaryData)
    juce::Image          logoImage;           // brand logo, drawn top-left (BinaryData)
    juce::Rectangle<int> logoBounds;          // where the logo paints
    juce::Slider       mistKnob;
    juce::ComboBox     sizeBox;
    ImageToggle        powerButton;           // On/Off (drives "bypass": on = processing)
    LedLamp            led;                    // console-style metering lamp
    float              ledLevel = 0.0f;        // envelope-followed peak (display ballistics)
    float              lastKnobGlow = -1.0f;   // last level fed to the knob ring (repaint gate)
    float              lastKnobHot  = -1.0f;   // last hot amount fed to the knob ring
    juce::Label        titleLabel, mistLabel, sizeLabel, ledLabel, powerLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   mistAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sizeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   bypassAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MistAudioProcessorEditor)
};
