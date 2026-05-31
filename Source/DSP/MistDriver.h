#pragma once

#include <cmath>
#include <vector>
#include <array>
#include <algorithm>

// =============================================================================
// MistDriver — Mist's single-knob reverb (original DSP).
//
// A Moorer/FDN algorithmic reverb: a medium room that adds depth without booming.
// Original Stereo Anxiety DSP.
//
// Signal flow (stereo, per sample):
//   pre-delay (L/R offset for width)
//     -> input diffusion (4 series Schroeder all-passes)
//       -> 8-line Feedback Delay Network, energy-preserving Householder feedback,
//          each line: HF-damping one-pole (highs decay faster)
//                   + LF cut in the loop (lows decay faster — keeps the tail tight)
//                   + RT60 loop gain  g = 10^(-3·len/(RT60·fs))
//         -> decorrelated L/R output taps (+/- sign patterns)
//           -> tail EQ: low-shelf cut (tighten lows) + gentle high-shelf
//             -> early-reflection taps mixed in
//               -> dry/wet mix  (the macro knob)
//
// The one "Mist" knob (0..10) sets the wet amount (dry -> drenched). "Size"
// (Room/Hall/Cathedral) scales the delay network + RT60. Zero dependencies
// beyond <cmath>/<vector> so it unit-tests in isolation.
// =============================================================================
class MistDriver
{
public:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr int    kLines = 8;

    enum SizeMode { kRoom = 0, kHall = 1, kCathedral = 2 };

    void setSampleRate (double sr)
    {
        sampleRate = (sr > 0.0) ? sr : 44100.0;
        allocate();
        designEq();
        designSize();
        reset();
    }

    void reset()
    {
        for (auto& dl : lines)  { std::fill (dl.buf.begin(), dl.buf.end(), 0.0f); dl.idx = 0; dl.lp = 0.0; dl.hp = 0.0; }
        for (auto& ap : diff)   { std::fill (ap.buf.begin(), ap.buf.end(), 0.0f); ap.idx = 0; }
        std::fill (preL.begin(), preL.end(), 0.0f);
        std::fill (preR.begin(), preR.end(), 0.0f);
        preIdx = 0;
        eqLoL = eqLoR = eqHiL = eqHiR = 0.0;
        outPeak = 0.0;
    }

    // ---- macro: knob 0..10 -> wet amount (dry .. drenched) ----
    void setKnob (double k)
    {
        knob = std::clamp (k, 0.0, 10.0);
        // concave so reverb appears early then eases in; max ~70% wet (insert-friendly).
        wetMix = 0.70 * std::pow (knob / 10.0, 0.85);
    }

    void setSize (int mode)
    {
        const int m = std::clamp (mode, 0, 2);
        if (m != sizeMode) { sizeMode = m; designSize(); }
    }

    void setBypass (bool b) { bypassActive = b; }

    // ============================================================
    //  block processing (stereo, in place)
    // ============================================================
    void processBlock (float* left, float* right, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const double dryL = (double) left[i];
            const double dryR = (double) right[i];

            double wetL = 0.0, wetR = 0.0;
            processStereo (dryL, dryR, wetL, wetR);

            double outL = dryL, outR = dryR;
            if (! bypassActive)
            {
                outL = dryL * (1.0 - wetMix) + wetL * wetMix;
                outR = dryR * (1.0 - wetMix) + wetR * wetMix;
            }

            left[i]  = (float) outL;
            right[i] = (float) outR;

            const double a = std::max (std::fabs (outL), std::fabs (outR));
            if (a > outPeak) outPeak = a;
        }
    }

    double outputPeak() const { return outPeak; }
    double tailSeconds() const { return rt60 * 1.2; }

private:
    // ---- a single FDN delay line with in-loop damping ----
    struct Line
    {
        std::vector<float> buf;
        int    idx   = 0;
        int    len   = 1;
        double lp    = 0.0;   // HF-damping one-pole state
        double hp    = 0.0;   // LF-cut one-pole state
        double gain  = 0.0;   // RT60 feedback gain

        inline double read() const { return buf[(size_t) idx]; }
        inline void   write (double v)
        {
            buf[(size_t) idx] = (float) v;
            if (++idx >= len) idx = 0;
        }
    };

    // ---- Schroeder all-pass (input diffusion) ----
    struct Allpass
    {
        std::vector<float> buf;
        int    idx = 0, len = 1;
        double g   = 0.7;
        inline double process (double x)
        {
            const double d = buf[(size_t) idx];
            const double y = -g * x + d;
            buf[(size_t) idx] = (float) (x + g * y);
            if (++idx >= len) idx = 0;
            return y;
        }
    };

    // ============================================================
    //  per-sample stereo reverb core
    // ============================================================
    void processStereo (double inL, double inR, double& outL, double& outR)
    {
        // --- pre-delay (L/R offset for width) ---
        preL[(size_t) preIdx] = (float) inL;
        preR[(size_t) preIdx] = (float) inR;
        const int preN = (int) preL.size();
        const double pL = preL[(size_t) wrap (preIdx - preLenL, preN)];
        const double pR = preR[(size_t) wrap (preIdx - preLenR, preN)];
        if (++preIdx >= preN) preIdx = 0;

        // tank input: diffuse the mono sum
        double x = 0.5 * (pL + pR);
        for (auto& ap : diff) x = ap.process (x);

        // --- read the 8 lines, apply per-line damping ---
        std::array<double, kLines> v {};
        for (int i = 0; i < kLines; ++i)
        {
            double s = lines[(size_t) i].read();
            // HF damping: one-pole lowpass (highs lose energy each pass)
            lines[(size_t) i].lp += hfDamp * (s - lines[(size_t) i].lp);
            s = lines[(size_t) i].lp;
            // LF cut: subtract a slice of the lows (lows decay faster — tight tail)
            lines[(size_t) i].hp += lfCoef * (s - lines[(size_t) i].hp);
            s -= lfAmount * lines[(size_t) i].hp;
            v[(size_t) i] = s;
        }

        // --- energy-preserving Householder feedback: y = v - (2/N) sum(v) ---
        double sum = 0.0;
        for (double s : v) sum += s;
        const double m = (2.0 / (double) kLines) * sum;

        // --- write back: diffused input + scaled feedback ---
        for (int i = 0; i < kLines; ++i)
        {
            const double fb = (v[(size_t) i] - m) * lines[(size_t) i].gain;
            lines[(size_t) i].write (x + fb);
        }

        // --- decorrelated stereo taps (orthogonal sign patterns) ---
        static constexpr int sL[kLines] = { +1, -1, +1, -1, +1, -1, +1, -1 };
        static constexpr int sR[kLines] = { +1, +1, -1, -1, +1, +1, -1, -1 };
        double wL = 0.0, wR = 0.0;
        for (int i = 0; i < kLines; ++i)
        {
            wL += sL[i] * v[(size_t) i];
            wR += sR[i] * v[(size_t) i];
        }
        const double norm = 1.0 / std::sqrt ((double) kLines);
        wL *= norm; wR *= norm;

        // --- tail EQ: low-shelf cut (tighten) + gentle high-shelf air ---
        wL = tailEq (wL, eqLoL, eqHiL);
        wR = tailEq (wR, eqLoR, eqHiR);

        // --- early reflections: a few diffusion-buffer taps for depth ---
        const double er = earlyReflections();
        wL += er * 0.5;
        wR += er * 0.5;

        outL = wL;
        outR = wR;
    }

    // low-shelf cut + high-shelf lift via two one-pole splits (cheap, stable)
    inline double tailEq (double x, double& loState, double& hiState)
    {
        loState += eqLoCoef * (x - loState);          // low band
        const double lo = loState;
        const double hiBand = x - lo;                 // high band (complement)
        hiState += eqHiCoef * (hiBand - hiState);     // smoothed top
        // cut the lows (kLoShelfGain<0 -> attenuate), nudge the air up
        return x + kLoShelfGain * lo + kHiShelfGain * hiState;
    }

    inline double earlyReflections()
    {
        // sparse taps off the input-diffusion chain's first all-pass buffer
        const auto& b = diff[0].buf;
        const int   L = diff[0].len;
        double e = 0.0;
        e += 0.50 * b[(size_t) wrap (diff[0].idx - (L * 1 / 7), L)];
        e += 0.38 * b[(size_t) wrap (diff[0].idx - (L * 3 / 7), L)];
        e += 0.28 * b[(size_t) wrap (diff[0].idx - (L * 5 / 7), L)];
        return e * erLevel;
    }

    // ============================================================
    //  design
    // ============================================================
    void allocate()
    {
        const double scaleMax = kSizeScale[kCathedral];
        // tank lines (sized to the largest preset at this sample rate)
        for (int i = 0; i < kLines; ++i)
        {
            const int maxLen = msToSamp (kBaseDelayMs[i] * scaleMax) + 4;
            lines[(size_t) i].buf.assign ((size_t) maxLen, 0.0f);
        }
        // diffusion all-passes (fixed short lengths)
        for (int i = 0; i < (int) diff.size(); ++i)
        {
            diff[(size_t) i].len = msToSamp (kDiffMs[i]);
            diff[(size_t) i].g   = 0.7;
            diff[(size_t) i].buf.assign ((size_t) diff[(size_t) i].len, 0.0f);
            diff[(size_t) i].idx = 0;
        }
        // pre-delay (max ~80 ms), L/R offset for width
        const int preMax = msToSamp (80.0) + 4;
        preL.assign ((size_t) preMax, 0.0f);
        preR.assign ((size_t) preMax, 0.0f);
        preLenL = msToSamp (kPreDelayMs);
        preLenR = msToSamp (kPreDelayMs * 1.18);   // slightly longer right -> width
    }

    void designSize()
    {
        const double scale = kSizeScale[sizeMode];
        rt60 = kRt60[sizeMode];
        for (int i = 0; i < kLines; ++i)
        {
            int len = msToSamp (kBaseDelayMs[i] * scale);
            len = std::clamp (len, 1, (int) lines[(size_t) i].buf.size() - 1);
            lines[(size_t) i].len  = len;
            lines[(size_t) i].idx  = std::min (lines[(size_t) i].idx, len - 1);
            // RT60 feedback gain for this delay length
            lines[(size_t) i].gain = std::pow (10.0, -3.0 * (double) len / (rt60 * sampleRate));
        }
        // HF damping increases a touch with size (bigger -> darker tail)
        hfDamp = std::clamp (0.45 - 0.06 * (double) sizeMode, 0.20, 0.6);
        erLevel = 0.9;   // prominent early reflections (depth)
    }

    void designEq()
    {
        // one-pole split corners
        eqLoCoef = onePole (200.0);    // low band corner
        eqHiCoef = onePole (2800.0);   // air band corner
        lfCoef   = onePole (180.0);    // in-loop LF-cut corner
    }

    // ============================================================
    //  helpers
    // ============================================================
    int msToSamp (double ms) const { return std::max (1, (int) std::lround (ms * 0.001 * sampleRate)); }

    double onePole (double fc) const
    {
        const double x = std::exp (-2.0 * kPi * fc / sampleRate);
        return 1.0 - x;   // one-pole LP coefficient (y += coef*(in-y))
    }

    static int wrap (int i, int n) { i %= n; return (i < 0) ? i + n : i; }

    // ---- constants / voicing ----
    // base tank delays (ms) at size scale 1.0 — mutually spread, vaguely prime
    static constexpr double kBaseDelayMs[kLines] =
        { 18.03, 23.71, 29.53, 34.27, 41.18, 47.89, 53.13, 59.31 };
    static constexpr double kDiffMs[4]  = { 4.77, 3.59, 12.61, 9.31 };
    static constexpr double kPreDelayMs = 18.0;

    static constexpr double kSizeScale[3] = { 0.62, 1.00, 1.65 };   // Room / Hall / Cathedral
    static constexpr double kRt60[3]      = { 0.71, 1.60, 3.00 };   // Room == a tight medium room

    // tail EQ depths (one-pole shelves): cut lows, tiny air lift
    static constexpr double kLoShelfGain = -0.62;   // subtract 0.62 of the low band
    static constexpr double kHiShelfGain =  0.10;   // small air lift
    static constexpr double lfAmount     =  0.45;   // in-loop LF-cut depth (fast LF decay)

    // ---- runtime state ----
    double sampleRate = 44100.0;
    double knob = 5.0, wetMix = 0.30;
    int    sizeMode = kRoom;
    bool   bypassActive = false;

    double rt60 = 0.71, hfDamp = 0.45, erLevel = 0.9;
    double eqLoCoef = 0.0, eqHiCoef = 0.0, lfCoef = 0.0;
    double eqLoL = 0.0, eqLoR = 0.0, eqHiL = 0.0, eqHiR = 0.0;

    std::array<Line, kLines> lines;
    std::array<Allpass, 4>   diff;
    std::vector<float>       preL, preR;
    int                      preIdx = 0, preLenL = 1, preLenR = 1;

    double outPeak = 0.0;
};
