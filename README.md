This is a an alpha plugin lightly tested on windows 11 25h2 in fl studio. It still has some debuging features present.   Do let me know if this is not working :) 

# DCOffsetremover
This aims to be a dc offset removal tool very basic
# DC Offset Remover Plugin

 ## Overview

This is a lightweight,  **DC Offset Remover** audio plugin built with the JUCE framework. It effectively removes unwanted DC offset and subsonic frequencies using selectable filter topologies, while providing detailed real-time metering and an optional waveform visualizer for signal analysis.

The plugin processes mono or stereo audio with **zero latency**, minimal CPU usage, and true bypass capability. It is ideal for mixing, mastering, tracking, vinyl restoration, podcasting, and live sound applications where clean, centered audio signals are essential.

## What is DC Offset and Why Remove It?

DC offset occurs when an audio signal has a non-zero average value, shifting the entire waveform up or down from the center line (zero volts).

Even though DC is inaudible, it causes serious issues in professional audio work:

- Reduced headroom – wastes valuable dynamic range
- Clicks/pops at edit points – especially problematic in DAW editing
- Inaccurate metering – VU and peak meters show false readings
- Speaker stress – causes unnecessary woofer excursion
- Problems with transformers/hardware – can saturate analog gear
- Phase and correlation issues – degrades stereo image in M/S processing

High-pass filtering (including dedicated DC blockers) removes this offset and subsonic rumble, ensuring a clean signal for downstream processing.

## The Four Operating Modes

The plugin offers four modes via a combo box (default: 2nd-order 20Hz HPF):

| Mode | Name                          | Type                   | Cutoff | Roll-off   | Characteristics                               | Best Use Cases                                      |
|------|-------------------------------|------------------------|--------|------------|-----------------------------------------------|-----------------------------------------------------|
| 0    | Bypass                        | True pass-through      | N/A    | N/A        | No processing, zero CPU overhead              | A/B comparison, troubleshooting                     |
| 1    | 1st-order DC blocker (~5Hz)   | Stateful 1-pole IIR    | ~5Hz   | 6dB/oct    | Minimal phase shift, transparent transients, slower convergence | Mastering (esp. acoustic/orchestral), M/S processing, phase-critical work |
| 2    | 2nd-order 10Hz HPF (Gentle)   | Butterworth 2-pole IIR | 10Hz   | 12dB/oct   | Preserves musical sub-bass, moderate phase shift | EDM, hip-hop, bass-heavy tracks, vinyl rumble removal |
| 3    | 2nd-order 20Hz HPF (Standard) | Butterworth 2-pole IIR | 20Hz   | 12dB/oct   | Industry-standard, fast DC removal            | Vocals, dialogue, podcasts, general mixing          |

All filters are minimum-phase with **0 samples latency**. The 2nd-order filters use JUCE's optimized Butterworth high-pass design for flat passband response.

## Visualizer: Real-Time Waveform Display

Toggle **"Show Visualizer"** to enable a high-performance waveform scope (30 FPS refresh).

Key elements (always shows **post-filter** output – what you hear):

- **Cyan waveform** – Filtered signal over ~23ms window (1024 samples at 44.1kHz)
- **Grid** – ±1.0 / ±0.5 / 0 lines (center zero line emphasized)
- **Red line** – Remaining post-filter DC offset
- **Orange bar (left)** – RMS energy of remaining subsonic content
- **Colored mode text** – Current filter (Red=Bypass, Yellow=1st-order, Green=10Hz, Cyan=20Hz)
- **DC Out %** – Numeric post-filter DC value

In Bypass mode, the visualizer shows the unprocessed input signal.

## Dual Pre/Post Metering

Comprehensive labels display metrics for both input (pre-filter) and output (post-filter):

| Metric | Description                                   | Typical Good Values                     |
|--------|-----------------------------------------------|-----------------------------------------|
| DC     | Average signal value (% of full scale)        | Post: < 0.1% (ideally ~0.000%)          |
| RMS    | Overall average level (% FS)                  | Pre ≈ Post (minimal change)             |
| Peak   | Maximum absolute value (% FS)                 | Pre ≈ Post                              |
| LF     | RMS energy below cutoff (% FS)                | Post << Pre (filter effectiveness)      |

Use these to objectively verify the filter is working without relying solely on ears.

## Professional Workflow Recommendations

1. **Detection**
   - Insert on track/master
   - Start in Bypass + Visualizer on
   - Check Pre DC and LF meters
   - If Pre DC > 0.5% or LF high → issue present

2. **Selection**
   - Acoustic/mastering/phase-critical → 1st-order
   - Bass-heavy electronic → 10Hz mode
   - Vocals/instruments/general → 20Hz mode

3. **Verification**
   - Switch to selected mode
   - Confirm Post DC ≈ 0%, Post LF near zero, red line centered
   - Bypass toggle to A/B (no audible difference = success)
   - Check in context of full mix

**Common Placements**

- Early in chain (after gain staging)
- Mastering: Input → DC Remover (1st-order) → EQ → Dynamics → Limiter
- Vinyl transfer: Source → DC Remover (10Hz) → De-click → EQ
- Live microphones: Preamp → DC Remover (20Hz) → Gate/Comp

 

## Version History

- (Current): Fixed 1st-order DC blocker algorithm, persistent state, correct mode mapping (0=Bypass), improved analysis filtering
- Old version: Added 10Hz mode and full visualizer with the original 20hz mode operating with 2nd order filter.
- **v1.0**: Initial 20Hz filter release

This plugin delivers transparent, reliable DC/subsonic removal trusted in professional environments – all while remaining extremely lightweight and visually informative.
