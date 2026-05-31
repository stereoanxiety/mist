// Mist test suite — single JUCE-linked exe so one llvm-cov run covers the engine
// (MistDriver.h), the processor (PluginProcessor.cpp) and the editor (PluginEditor.cpp).
// Zero-dependency harness (no Catch2). Tests assert real reverb properties (a tail exists
// and decays to silence, knob-0 is dry, finiteness across the parameter space, bypass
// passes dry through) plus branch reachability, rather than brittle golden samples.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <JuceHeader.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

static int g_fail = 0, g_total = 0;
#define CHECK(cond) do { ++g_total; if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a,b,tol) do { ++g_total; const double _d = std::fabs((double)(a)-(double)(b)); \
    if (_d > (tol)) { ++g_fail; std::printf("FAIL %s:%d  |%g - %g| = %g > %g\n", \
    __FILE__, __LINE__, (double)(a), (double)(b), _d, (double)(tol)); } } while (0)

// =====================================================================
// MIST REVERB ENGINE (MistDriver — FDN reverb)
// =====================================================================

// Peak magnitude of the stereo output over the last `tailFrom`..n window after feeding a
// short burst then silence. Returns {finite, peakInBurst, peakInTail}.
struct TailResult { bool finite; double burstPeak; double tailPeak; };

static TailResult runImpulse (double knob, int sizeMode, double sr = 48000.0, int n = 48000)
{
    MistDriver e; e.setSampleRate (sr);
    e.setSize (sizeMode); e.setKnob (knob); e.reset();

    std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
    // 200-sample burst of noise at the front, silence after
    unsigned int seed = 22222u;
    for (int i = 0; i < 200; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const float s = ((float) (seed >> 9) / 8388608.0f - 1.0f) * 0.5f;
        L[(size_t) i] = s; R[(size_t) i] = s;
    }
    e.processBlock (L.data(), R.data(), n);

    bool finite = true;
    double burstPeak = 0.0, tailPeak = 0.0;
    const int tailStart = (int) (sr * 0.25);   // 250 ms in — well past the 200-sample burst
    for (int i = 0; i < n; ++i)
    {
        finite = finite && std::isfinite (L[(size_t) i]) && std::isfinite (R[(size_t) i]);
        const double a = std::max (std::fabs ((double) L[(size_t) i]), std::fabs ((double) R[(size_t) i]));
        if (i < 400)            burstPeak = std::max (burstPeak, a);
        if (i >= tailStart)     tailPeak  = std::max (tailPeak, a);
    }
    return { finite, burstPeak, tailPeak };
}

static void testMistTailExistsAndDecays()
{
    // A wet reverb produces energy well after the input stops (a tail)...
    const auto wet = runImpulse (9.0, MistDriver::kHall);
    CHECK (wet.finite);
    CHECK (wet.tailPeak > 1.0e-5);            // there is an audible tail at 250 ms

    // ...and that tail decays toward silence by the end of a 1 s window (no runaway).
    MistDriver e; e.setSampleRate (48000.0); e.setSize (MistDriver::kRoom); e.setKnob (9.0); e.reset();
    const int n = 96000;
    std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
    for (int i = 0; i < 100; ++i) { L[(size_t) i] = 0.7f; R[(size_t) i] = -0.7f; }
    e.processBlock (L.data(), R.data(), n);
    double endPeak = 0.0;
    for (int i = n - 4800; i < n; ++i)        // last 100 ms
        endPeak = std::max (endPeak, (double) std::fabs (L[(size_t) i]));
    CHECK (endPeak < 1.0e-3);                  // Room (RT60 0.71s) is quiet after ~2 s
}

static void testMistKnobZeroIsDry()
{
    // wetMix == 0 at knob 0 -> output is the dry input, sample-exact.
    MistDriver e; e.setSampleRate (48000.0); e.setSize (MistDriver::kHall); e.setKnob (0.0); e.reset();
    const int n = 1024;
    std::vector<float> L ((size_t) n), R ((size_t) n), L0 ((size_t) n), R0 ((size_t) n);
    for (int i = 0; i < n; ++i) { L[(size_t) i] = L0[(size_t) i] = (float) std::sin (i * 0.21);
                                   R[(size_t) i] = R0[(size_t) i] = (float) std::sin (i * 0.17); }
    e.processBlock (L.data(), R.data(), n);
    bool same = true;
    for (int i = 0; i < n; ++i) same = same && (L[(size_t) i] == L0[(size_t) i]) && (R[(size_t) i] == R0[(size_t) i]);
    CHECK (same);
}

static void testMistBypassIsDry()
{
    MistDriver e; e.setSampleRate (48000.0); e.setKnob (9.0); e.setBypass (true); e.reset();
    const int n = 512;
    std::vector<float> L ((size_t) n), R ((size_t) n), L0 ((size_t) n), R0 ((size_t) n);
    for (int i = 0; i < n; ++i) { L[(size_t) i] = L0[(size_t) i] = (float) (0.3 * std::sin (i * 0.3));
                                   R[(size_t) i] = R0[(size_t) i] = (float) (0.3 * std::cos (i * 0.25)); }
    e.processBlock (L.data(), R.data(), n);
    bool same = true;
    for (int i = 0; i < n; ++i) same = same && (L[(size_t) i] == L0[(size_t) i]) && (R[(size_t) i] == R0[(size_t) i]);
    CHECK (same);
}

static void testMistFiniteSweep()
{
    // Every SR x size x knob must stay finite and bounded on hot input — the stability net.
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int size = 0; size <= 2; ++size)
            for (double k = 0.0; k <= 10.0001; k += 2.0)
            {
                const auto r = runImpulse (k, size, sr, (int) (sr * 0.5));
                CHECK (r.finite && r.burstPeak < 8.0 && r.tailPeak < 8.0);
            }
}

static void testMistDeterminism()
{
    MistDriver a, b;
    a.setSampleRate (48000.0); b.setSampleRate (48000.0);
    a.setKnob (6.3); b.setKnob (6.3);
    a.setSize (MistDriver::kHall); b.setSize (MistDriver::kHall);
    a.reset(); b.reset();
    const int n = 1024;
    std::vector<float> La ((size_t) n), Ra ((size_t) n), Lb ((size_t) n), Rb ((size_t) n);
    for (int i = 0; i < n; ++i) { La[(size_t) i] = Lb[(size_t) i] = (float) std::sin (i * 0.3);
                                   Ra[(size_t) i] = Rb[(size_t) i] = (float) std::cos (i * 0.3); }
    a.processBlock (La.data(), Ra.data(), n);
    b.processBlock (Lb.data(), Rb.data(), n);
    bool same = true;
    for (int i = 0; i < n; ++i) same = same && (La[(size_t) i] == Lb[(size_t) i]) && (Ra[(size_t) i] == Rb[(size_t) i]);
    CHECK (same);
}

// =====================================================================
// PROCESSOR
// =====================================================================
static void setChoice (MistAudioProcessor& p, const char* id, int idx, int nChoices)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost ((float) idx / (float) (nChoices - 1));
}

static void setParam (MistAudioProcessor& p, const char* id, float norm)
{
    if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (norm);
}

static void testProcessorPaths()
{
    MistAudioProcessor p;
    p.prepareToPlay (48000.0, 512);
    juce::MidiBuffer midi;

    // Every size mode at a wet setting stays finite.
    for (int size = 0; size <= 2; ++size)
    {
        setChoice (p, "size", size, 3);
        setParam  (p, "mist", 0.8f);
        juce::AudioBuffer<float> buf (2, 512);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i) buf.setSample (ch, i, (float) (0.4 * std::sin (i * 0.27)));
        p.processBlock (buf, midi);
        bool ok = true;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 512; ++i) ok = ok && std::isfinite (buf.getSample (ch, i));
        CHECK (ok);
    }

    // mono path (1-channel buffer) stays finite
    juce::AudioBuffer<float> mono (1, 256);
    for (int i = 0; i < 256; ++i) mono.setSample (0, i, (float) (0.4 * std::sin (i * 0.2)));
    p.processBlock (mono, midi);
    bool monoOk = true;
    for (int i = 0; i < 256; ++i) monoOk = monoOk && std::isfinite (mono.getSample (0, i));
    CHECK (monoOk);
}

static void testProcessorBypass()
{
    juce::MidiBuffer midi;

    // bypass on -> output is the dry input, sample-exact.
    MistAudioProcessor p; p.prepareToPlay (48000.0, 512);
    setParam (p, "mist", 0.8f);                                   // make wet != dry
    setParam (p, "bypass", 1.0f);

    juce::AudioBuffer<float> buf (2, 512);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i) buf.setSample (ch, i, (float) (0.3 * std::sin (i * 0.21)));
    juce::AudioBuffer<float> in; in.makeCopyOf (buf);
    p.processBlock (buf, midi);
    bool same = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i) same = same && (buf.getSample (ch, i) == in.getSample (ch, i));
    CHECK (same);

    // bypass off + wet -> output differs from dry (engine actually added reverb)
    MistAudioProcessor q; q.prepareToPlay (48000.0, 512);
    setParam (q, "mist", 0.9f);
    juce::AudioBuffer<float> buf2 (2, 512);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i) buf2.setSample (ch, i, (float) (0.3 * std::sin (i * 0.21)));
    juce::AudioBuffer<float> in2; in2.makeCopyOf (buf2);
    q.processBlock (buf2, midi);
    bool differs = false;
    for (int i = 0; i < 512; ++i) differs = differs || (buf2.getSample (0, i) != in2.getSample (0, i));
    CHECK (differs);
}

static void testProcessorBusLayouts()
{
    MistAudioProcessor p;
    auto mk = [] (const juce::AudioChannelSet& in, const juce::AudioChannelSet& out)
    {
        juce::AudioProcessor::BusesLayout L;
        L.inputBuses .add (in);
        L.outputBuses.add (out);
        return L;
    };
    CHECK ( p.isBusesLayoutSupported (mk (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo())));
    CHECK ( p.isBusesLayoutSupported (mk (juce::AudioChannelSet::mono(),   juce::AudioChannelSet::mono())));
    CHECK (!p.isBusesLayoutSupported (mk (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono())));
    CHECK (!p.isBusesLayoutSupported (mk (juce::AudioChannelSet::create5point1(), juce::AudioChannelSet::create5point1())));
}

static void testProcessorStateRoundTrip()
{
    MistAudioProcessor a;
    setParam  (a, "mist", 0.42f);
    setChoice (a, "size", 2, 3);
    setParam  (a, "bypass", 1.0f);
    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    CHECK (mb.getSize() > 0);

    MistAudioProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK_NEAR (b.apvts.getRawParameterValue ("mist")->load(),
                a.apvts.getRawParameterValue ("mist")->load(), 1e-3);
    CHECK_NEAR (b.apvts.getRawParameterValue ("size")->load(),
                a.apvts.getRawParameterValue ("size")->load(), 1e-3);
    CHECK_NEAR (b.apvts.getRawParameterValue ("bypass")->load(),
                a.apvts.getRawParameterValue ("bypass")->load(), 1e-3);

    b.setStateInformation (nullptr, 0);                               // garbage -> no-op branch
}

static void testProcessorMetaAndEntryPoint()
{
    MistAudioProcessor p;
    CHECK (p.getName() == "Mist");
    CHECK (!p.acceptsMidi());
    CHECK (!p.producesMidi());
    CHECK (!p.isMidiEffect());
    CHECK (p.getTailLengthSeconds() > 1.0);          // reverb reports a real tail
    CHECK (p.getNumPrograms() == 1);
    CHECK (p.getCurrentProgram() == 0);
    p.setCurrentProgram (0);
    CHECK (p.getProgramName (0).isNotEmpty());
    p.changeProgramName (0, "x");
    CHECK (p.hasEditor());
    p.releaseResources();

    // wet-amount display helper: 0 -> "0 %", 10 -> "100 %"
    CHECK (MistAudioProcessor::mistToWetString (0.0).contains ("%"));
    CHECK (MistAudioProcessor::mistToWetString (10.0).contains ("100"));

    juce::AudioProcessor* viaFactory = createPluginFilter();          // plugin entry point
    CHECK (viaFactory != nullptr);
    delete viaFactory;
}

// =====================================================================
// EDITOR (needs the GUI message manager)
// =====================================================================
static void testEditor()
{
    MistAudioProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());   // ctor: setup + setSize -> resized
    CHECK (ed != nullptr);
    CHECK (ed->getWidth() > 0 && ed->getHeight() > 0);

    ed->setSize (400, 520);                                              // trigger resized() again

    juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
    juce::Graphics g (img);
    ed->paint (g);                                                       // cover paint()
    CHECK (img.getWidth() == ed->getWidth());

    // second editor with bypass on -> covers the LED's powered-off branch on next timer tick
    setParam (p, "bypass", 1.0f);
    std::unique_ptr<juce::AudioProcessorEditor> ed2 (p.createEditor());
    CHECK (ed2 != nullptr);
}

// Offscreen UI render -> PNG (README screenshot). Guarded by MIST_RENDER_PNG so it never
// runs in a normal CTest invocation. Renders the WHOLE component tree so the knob, size
// box, power button, LED and labels all appear.
static int renderScreenshot (const char* path)
{
    MistAudioProcessor p;
    p.prepareToPlay (48000.0, 512);

    if (auto* m = p.apvts.getParameter ("mist")) m->setValueNotifyingHost (0.65f);  // mid-wet
    if (auto* s = p.apvts.getParameter ("size")) s->setValueNotifyingHost (0.5f);   // Hall

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    if (ed == nullptr) { std::printf ("render: null editor\n"); return 1; }
    ed->setSize (ed->getWidth(), ed->getHeight());

    const int scale = 2;
    juce::Image img (juce::Image::ARGB, ed->getWidth() * scale, ed->getHeight() * scale, true);
    {
        juce::Graphics g (img);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.addTransform (juce::AffineTransform::scale ((float) scale));
        ed->paintEntireComponent (g, false);
    }

    juce::File out = juce::File::isAbsolutePath (path)
                       ? juce::File (path)
                       : juce::File::getCurrentWorkingDirectory().getChildFile (path);
    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream os (out);
    if (os.failedToOpen()) { std::printf ("render: cannot open %s\n", out.getFullPathName().toRawUTF8()); return 1; }
    juce::PNGImageFormat png;
    const bool ok = png.writeImageToStream (img, os);
    std::printf ("render: %s %s (%dx%d)\n", ok ? "wrote" : "FAILED",
                 out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return ok ? 0 : 1;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;   // message manager for the editor/components

    if (const char* png = std::getenv ("MIST_RENDER_PNG"))
        return renderScreenshot (png);

    testMistTailExistsAndDecays();
    testMistKnobZeroIsDry();
    testMistBypassIsDry();
    testMistFiniteSweep();
    testMistDeterminism();

    testProcessorPaths();
    testProcessorBypass();
    testProcessorBusLayouts();
    testProcessorStateRoundTrip();
    testProcessorMetaAndEntryPoint();

    testEditor();

    std::printf ("\n%s : %d/%d checks passed\n", g_fail == 0 ? "PASS" : "FAIL", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
