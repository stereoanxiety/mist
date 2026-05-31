#include "PluginEditor.h"

MistAudioProcessorEditor::MistAudioProcessorEditor (MistAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);

    // --- embedded UI assets ---
    bgImage   = juce::ImageCache::getFromMemory (BinaryData::background_png, BinaryData::background_pngSize);
    logoImage = juce::ImageCache::getFromMemory (BinaryData::logo_png,       BinaryData::logo_pngSize);
    // (brand logo is drawn directly in paint(), top-left — see resized()/paint())

    // --- product name, top-right ---
    titleLabel.setText ("Mist", juce::dontSendNotification);
    titleLabel.setComponentID ("title");              // exempt from the LnF's fixed label font
    titleLabel.setFont (lnf.withTitleFace (68.0f));   // Abuget (brush script) — title
    titleLabel.setJustificationType (juce::Justification::centredRight);
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (0xff80868a));   // grey, carved face
    addAndMakeVisible (titleLabel);

    // --- hero Mist knob (wet amount; value drawn in the centre by the LookAndFeel) ---
    mistKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mistKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    mistKnob.textFromValueFunction = [] (double v) { return MistAudioProcessor::mistToWetString (v); };
    addAndMakeVisible (mistKnob);

    mistLabel.setText ("Amount", juce::dontSendNotification);
    mistLabel.setJustificationType (juce::Justification::centred);
    mistLabel.setColour (juce::Label::textColourId, lnf.dim);
    addAndMakeVisible (mistLabel);

    mistAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "mist", mistKnob);

    // --- activity LED (accent = processing signal, mint = near clipping) ---
    led.setImages (juce::ImageCache::getFromMemory (BinaryData::led_teal_png,   BinaryData::led_teal_pngSize),
                   juce::ImageCache::getFromMemory (BinaryData::led_chrome_png, BinaryData::led_chrome_pngSize));
    addAndMakeVisible (led);

    ledLabel.setText ("Signal", juce::dontSendNotification);
    ledLabel.setFont (lnf.withFace (14.0f));
    ledLabel.setJustificationType (juce::Justification::centred);
    ledLabel.setColour (juce::Label::textColourId, lnf.dim);
    addAndMakeVisible (ledLabel);

    // --- On/Off power button (image toggle; drives "bypass": ON = processing) ---
    powerButton.setImages (juce::ImageCache::getFromMemory (BinaryData::button_on_png,  BinaryData::button_on_pngSize),
                           juce::ImageCache::getFromMemory (BinaryData::button_off_png, BinaryData::button_off_pngSize),
                           /*showOnWhenToggled*/ false);   // bypass off (toggle off) -> lit ON
    addAndMakeVisible (powerButton);
    bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "bypass", powerButton);

    powerLabel.setText ("On / Off", juce::dontSendNotification);
    powerLabel.setFont (lnf.withFace (14.0f));
    powerLabel.setJustificationType (juce::Justification::centred);
    powerLabel.setColour (juce::Label::textColourId, lnf.dim);
    addAndMakeVisible (powerLabel);

    // --- Size selector (IDs 1..N match choice indices 0..N-1) ---
    sizeBox.addItemList ({ "Room", "Hall", "Cathedral" }, 1);
    sizeBox.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (sizeBox);

    sizeLabel.setText ("Size", juce::dontSendNotification);
    sizeLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (sizeLabel);

    sizeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, "size", sizeBox);

    setSize (420, 560);   // matches the panel background's 3:4 aspect
    startTimerHz (30);    // poll the output peak for the metering LED
}

MistAudioProcessorEditor::~MistAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MistAudioProcessorEditor::timerCallback()
{
    // Peak-meter ballistics: jump to a new peak instantly, decay slowly between frames.
    // The slow release is what makes the lamp "blink" in time with the program.
    const float peak = processor.fetchPeak();              // max output since last frame (always drained)

    float bright = 0.0f, hot = 0.0f;
    // When powered off (bypassed), the lamp and ring are dark regardless of signal.
    if (processor.apvts.getRawParameterValue ("bypass")->load() > 0.5f)
    {
        ledLevel = 0.0f;
    }
    else
    {
        ledLevel = juce::jmax (peak, ledLevel * 0.80f);    // ~300 ms release at 30 Hz
        const float db = ledLevel > 1.0e-5f ? 20.0f * std::log10 (ledLevel) : -100.0f;
        bright = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 42.0f);   // -48..-6 dB
        hot    = juce::jlimit (0.0f, 1.0f, (db + 10.0f) / 10.0f);   // -10..0 dB
    }
    led.setState (bright, hot);

    // Feed the same ballistics to the knob's LED ring so its lit segments pulse/blink
    // with the program and flare toward mint on peaks. Repaint only on a meaningful change,
    // so an idle (silent) editor isn't redrawing the knob 30×/s for nothing.
    if (std::abs (bright - lastKnobGlow) > 0.002f || std::abs (hot - lastKnobHot) > 0.002f)
    {
        lastKnobGlow = bright; lastKnobHot = hot;
        lnf.setKnobGlow (bright, hot);
        mistKnob.repaint();
    }
}

void MistAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // textured metal panel background (falls back to a flat fill if the asset is missing)
    if (bgImage.isValid())
        g.drawImage (bgImage, b, juce::RectanglePlacement::fillDestination);
    else
        g.fillAll (lnf.bgBot);

    // hazy glow behind the Mist knob
    const auto kc = mistKnob.getBounds().toFloat().getCentre();
    const float gr = (float) mistKnob.getWidth() * 0.75f;
    g.setGradientFill ({ lnf.teal.withAlpha (0.14f), kc.x, kc.y,
                         lnf.teal.withAlpha (0.0f),  kc.x + gr, kc.y, true });
    g.fillRect (b);

    // brand logo, top-left (aspect-preserving, left-aligned)
    if (logoImage.isValid())
        g.drawImageWithin (logoImage, logoBounds.getX(), logoBounds.getY(),
                           logoBounds.getWidth(), logoBounds.getHeight(),
                           juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, false);
}

void MistAudioProcessorEditor::resized()
{
    // Stay inside the painted metal frame of the background (thicker on all sides than a
    // flat 18px; the bottom gold line in particular needs clearance).
    auto r = getLocalBounds().reduced (34, 0);
    r.removeFromTop (36);
    r.removeFromBottom (38);

    // top band: brand logo left, product name "Mist" right
    auto logoBand = r.removeFromTop (46);
    const float logoAspect = logoImage.isValid()
        ? (float) logoImage.getWidth() / (float) logoImage.getHeight() : 2.73f;
    logoBounds = logoBand.removeFromLeft (juce::roundToInt (42.0f * logoAspect));
    // give the brush-script title vertical room (its glyphs sit small inside a tall em);
    // small top margin so it clears the painted top frame
    titleLabel.setBounds (logoBand.withHeight (60).withY (logoBand.getY() - 2));
    r.removeFromTop (10);

    // metering LED + caption
    led.setBounds (r.removeFromTop (48).withSizeKeepingCentre (48, 48));
    ledLabel.setBounds (r.removeFromTop (20));
    r.removeFromTop (2);

    // bottom stack, built from the bottom up: Size selector, then the power button +
    // caption. Everything centred horizontally.
    auto bottom = r.removeFromBottom (176);
    sizeBox.setBounds   (bottom.removeFromBottom (24).withSizeKeepingCentre (124, 24));
    sizeLabel.setBounds (bottom.removeFromBottom (20));
    bottom.removeFromBottom (6);
    powerLabel.setBounds (bottom.removeFromBottom (20));
    bottom.removeFromBottom (2);
    // landscape power button (asset aspect ~1.40, includes baked shadow padding)
    const float aspect = 1.3955f;
    int pbh = juce::jmin (bottom.getHeight(), 72);
    int pbw = juce::roundToInt ((float) pbh * aspect);
    if (pbw > bottom.getWidth()) { pbw = bottom.getWidth(); pbh = juce::roundToInt ((float) pbw / aspect); }
    powerButton.setBounds (bottom.withSizeKeepingCentre (pbw, pbh));

    // hero Mist knob fills the remaining centre
    mistLabel.setBounds (r.removeFromBottom (22));
    const int d = juce::jmin (r.getWidth(), r.getHeight());
    mistKnob.setBounds (r.withSizeKeepingCentre (d, d));
}
