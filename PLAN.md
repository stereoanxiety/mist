# Mist by Stereo Anxiety — Development Plan

## Overview

Mist is a single-knob reverb VST3/AU/Standalone plugin. One macro **Amount** knob sweeps
the dry→wet balance of an original Moorer/FDN algorithmic reverb; a 3-mode **Size** toggle
(Room/Hall/Cathedral) scales the network and decay. JUCE 8 + CMake, targeting macOS
(Apple Silicon + Intel) and Windows.

The DSP is Mist's own original implementation — no other product's code or coefficients
are used. See `Source/DSP/MistDriver.h`.

---

## Signal Flow (per sample, stereo)

```
INPUT (L, R)
  ├─ pre-delay (L/R offset ~18 ms for width)
  ├─ input diffusion: 4 series Schroeder all-passes (mono sum)
  ├─ FDN tank: 8 delay lines, energy-preserving Householder feedback
  │     each line: HF-damping one-pole  (highs decay faster)
  │              + in-loop LF cut        (lows decay faster — keeps the tail tight)
  │              + RT60 gain g = 10^(-3·len/(RT60·fs))
  ├─ decorrelated L/R output taps (orthogonal +/- sign patterns)
  ├─ tail EQ: low-shelf cut (tighten) + gentle high-shelf (air)
  ├─ early reflections: sparse diffusion-buffer taps (depth)
  └─ dry/wet mix  ← the Amount macro (0..10 → up to ~70% wet)
OUTPUT
```

Size presets: Room (scale 0.62, RT60 0.71 s — tight medium room), Hall (1.0, 1.6 s),
Cathedral (1.65, 3.0 s).

---

## Status

| Phase | State |
|-------|-------|
| 0 — Scaffold (CMake, JUCE 8, VST3/AU/Standalone/CLAP) | ✅ |
| 1 — Reverb engine (MistDriver FDN) | ✅ first pass |
| 2 — GUI / seafoam visual identity | ✅ |
| 3 — Features (size modes, A/B, metering) | ✅ basic |
| 4 — Polish (modulation depth, ER tuning, pluginval, profiling) | ▢ |
| 5 — Distribution (signing, notarization, release CI, installer) | ▢ |

## Next-phase polish ideas

- Light delay-line modulation (anti-metallic).
- Tune the ER tap pattern/level by ear.
- Optional tempo-synced pre-delay.
- pluginval CTest (copy Dust's block), CPU profiling of the 8-line tank.
