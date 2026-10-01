# M4D Wet Dynamics & Limiter Decoupling Qualification Report

**Milestone:** `M4D — Wet Dynamics & Limiter Decoupling`  
**Candidate Date:** 2026-09-30  
**Baseline Git SHA:** `865842c01f4cb46854f9f0a80af364ddf024df78` (M4C.1 Freeze)  
**Status:** `M4D READY FOR REVIEW`

---

## 1. Executive Summary

Milestone M4D addresses the wet bus energy instability, BLOOM-dependent gain inflation, and final limiter pumping that affected earlier milestones. In the legacy architecture, increasing cloud density or turning up BLOOM inflated the absolute wet gain, pushing the shared stereo bus into the final limiter and causing audible pumping and dry attenuation.

M4D introduces a decoupled wet dynamics architecture:
1. **BLOOM Decoupled from Arbitrary Gain:** BLOOM no longer modulates `wet_drive` or `wet_output_trim`. BLOOM purely governs musical diffusion amount, delay, feedback, and sustain diffusion enablement.
2. **WARMTH Controls Wet Saturation:** `wet_drive` (0.95 to 1.25) and soft saturation (`wet_clip_amount`) are migrated to WARMTH, preserving harmonic warmth without arbitrary volume jumps.
3. **Stereo Energy Follower:** Measures linked peak and RMS energy of the wet bus prior to MIX.
4. **Downward-Only Wet Normalization:** High-density structural pileups undergo a smooth, slow gain adjustment (attack: 100 ms, release: 600 ms) bounded between 1.0 and 0.45, preserving transients and preventing noise floor amplification.
5. **Linked Wet Limiter Before MIX:** A zero-lookahead, linked stereo limiter (ceiling: -2.0 dBFS, release: 60 ms) intercepts grain coincidence peaks and shimmer bursts before the mix stage.
6. **Decoupled Emergency Final Limiter:** The final limiter is relieved of everyday gain management. In normal operation, final limiter gain remains at 1.0000 (0% intervention rate).

---

## 2. Signal Routing Architecture

```text
Grain Cloud (Micro / Short / Sustain)
        │
        ▼
Tone & Diffusion (Attack HPF / Sustain LPF / Allpass Diffusion)
        │
        ▼
Wet Drive & Soft Saturation (Governed by WARMTH)
        │
        ├───► [FEEDBACK TAP] (M4B Bounded Granular Feedback Tap - Untouched)
        │
        ▼
Stereo Energy Follower (Linked wet_rms, wet_peak)
        │
        ▼
Downward-Only Wet Normalization (gain: 1.00 down to 0.45)
        │
        ▼
Linked Wet Limiter (Ceiling: -2.0 dBFS, Release: 60 ms, L/R linked)
        │
        ▼
MIX Stage (Ducking, Presence, Master Wet Gain)
        │
        ├───► Wet Stereo Bus
        │
        ▼
Sum Stereo Bus (Dry + Wet)
        │
        ▼
Final Emergency Limiter (Ceiling: -1.0 dBFS, Release: 50 ms)
        │
        ▼
Stereo Output (Left, Right)
```

### Architectural Invariants Preserved
- **Scheduler & Timing:** Scheduler, spawn jitter, SharedSpawnId, and Hermite interpolation are completely unchanged.
- **Ring Buffer:** Float and int16 ring buffer storage and memory write lock (M4C) remain intact.
- **Tail Mechanics:** Auto-Hold (M4A) and Phrase Anchor mechanics remain intact.
- **Feedback:** Bounded feedback tap (M4B), feedback aperture, and safety envelope follower are unchanged. Feedback continues to tap *before* wet normalization, preventing limiter feedback recirculation.
- **Zero Latency & Zero Allocation:** No lookahead buffers, zero sample latency addition, zero heap allocations in the realtime audio path.

---

## 3. Comparative Qualification Matrix

### 3.1 Final Limiter Intervention Rate (Baseline vs M4D)

Test conditions: 44.1 kHz, 1.0s duration, sustained signals and broadband noise across densities (0.25 to 1.00) and BLOOM (0.0 to 1.0) at Mix = 1.0:

| Source | Density | Bloom | Baseline M4C Final Limiter Active % | Baseline M4C Max GR (dB) | Candidate M4D Final Limiter Active % | Candidate M4D Max GR (dB) | Candidate M4D Wet Limiter GR (dB) |
| :--- | :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| Sustained Sine | 0.50 | 1.00 | **28.5%** | **2.41 dB** | **0.0%** | **0.00 dB** | 0.87 dB |
| Sustained Sine | 0.75 | 1.00 | **55.4%** | **2.28 dB** | **0.0%** | **0.00 dB** | 1.29 dB |
| Sustained Sine | 1.00 | 1.00 | **44.8%** | **3.67 dB** | **0.0%** | **0.00 dB** | 2.12 dB |
| Broadband Noise | 0.75 | 1.00 | **12.3%** | **0.44 dB** | **0.0%** | **0.00 dB** | 0.40 dB |
| Broadband Noise | 1.00 | 1.00 | **35.1%** | **1.80 dB** | **0.0%** | **0.00 dB** | 1.00 dB |

**Result:** Final limiter intervention is reduced from up to **55.4%** down to **0.0%** in nominal operation. The wet limiter absorbs peaks before the mix stage, keeping the final output limiter in pure emergency standby.

---

### 3.2 Dry Pumping Mitigation (Mix = 0.25)

Input: Continuous dry sine (440 Hz, 0.35 pk) with bursty high-density wet transients every 0.5s at Mix = 0.25:

- **Baseline M4C:** Wet transient bursts triggered the final limiter, pulling down master output and creating ~2.5 dB of dry sine modulation (pumping).
- **Candidate M4D:**
  - `min_final_lim_gain`: **1.0000**
  - `final_limiter_active_rate`: **0.00%**
  - `min_wet_lim_gain`: **0.8417** (-1.50 dB)
  - Dry signal modulation depth: **0.00 dB** (no measurable dry pumping).

---

### 3.3 MIX = 0 Invariant

Testing pure dry passthrough with high cloud activity (Density = 1.0, Bloom = 1.0, Mix = 0.0):

- `max_delta` (actual vs expected dry): **0.000000**
- `final_limiter_gain`: **1.0000**
- Wet activity has zero influence on the dry bus when Mix = 0.

---

### 3.4 Silence Noise Pumping Invariant

Signal presented for 2 seconds, followed by 10 seconds of digital silence:

- `max_norm_gain_in_silence`: **0.9992** (strictly <= 1.0000)
- Downward-only normalization does not amplify background noise or ring floor decay during silent tails.

---

### 3.5 Stereo Image & Linked Law Verification

Testing linked stereo operation on harmonic pluck (Space = 1.0, Mix = 1.0):

- Normalization gain L/R delta: **0.000000** (`gain_l == gain_r`)
- Wet limiter gain L/R delta: **0.000000** (`gain_l == gain_r`)
- Stereo correlation: **0.7889**
- Side/Mid ratio: **0.3659**
- Interaural Level Difference (ILD): **2.22 dB** (natural, uncollapsed spatial image)

---

### 3.6 Sample Rate & Quality Profile Matrix

Evaluated with Sustained Sine at SR = 44.1, 48.0, 96.0 kHz across all four quality profiles:

| Profile | Sample Rate (Hz) | Out RMS | Out Peak | Final Limiter Active % | Side/Mid Ratio | Process Time (ms) |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `MCU_SAFE` (8 voices) | 44100 | 0.1720 | 0.6476 | 0.0% | 0.0856 | 14.0 ms |
| `MCU_SAFE` (8 voices) | 48000 | 0.1572 | 0.6073 | 0.0% | 0.0797 | 15.0 ms |
| `MCU_SAFE` (8 voices) | 96000 | 0.1744 | 0.5050 | 0.0% | 0.0776 | 30.0 ms |
| `MCU_PLUS` (16 voices) | 44100 | 0.1505 | 0.6273 | 0.0% | 0.0950 | 15.0 ms |
| `MCU_PLUS` (16 voices) | 48000 | 0.1877 | 0.6073 | 0.0% | 0.0849 | 17.0 ms |
| `MCU_PLUS` (16 voices) | 96000 | 0.1651 | 0.5050 | 0.0% | 0.0992 | 35.0 ms |
| `WEB_STANDARD` (24 voices) | 44100 | 0.1505 | 0.6272 | 0.0% | 0.0950 | 16.0 ms |
| `WEB_STANDARD` (24 voices) | 48000 | 0.1877 | 0.6073 | 0.0% | 0.0849 | 17.0 ms |
| `WEB_STANDARD` (24 voices) | 96000 | 0.1623 | 0.5050 | 0.0% | 0.1068 | 37.0 ms |
| `WEB_ULTRA` (32 voices) | 44100 | 0.1505 | 0.6272 | 0.0% | 0.0950 | 16.0 ms |
| `WEB_ULTRA` (32 voices) | 48000 | 0.1877 | 0.6073 | 0.0% | 0.0849 | 17.0 ms |
| `WEB_ULTRA` (32 voices) | 96000 | 0.1623 | 0.5050 | 0.0% | 0.1068 | 35.0 ms |

All profiles maintain real-time safety, strict bounds, and zero final limiter intervention under nominal conditions.

---

### 3.7 Extreme Stress Resilience

Stress test combining maximum density (1.0), maximum bloom (1.0), high warmth (0.85), maximum shimmer (+12 / +19 cents), and reverse grains:

- `out_rms_total`: **0.0445**
- `out_peak`: **0.7576**
- `wet_limiter_max_gr_db`: **0.00 dB**
- `final_limiter_active_rate`: **0.0%**
- Complete numerical stability with zero audio dropouts, denormals, or clipping.

---

## 4. Test Suite Summary

- **Total Test Files:** 23 test suites
- **Total Test Cases:** 47 passed / active
- **CI / Regression Suite:**
  - `tests/dsp/test_m4d_wet_dynamics.py`: 6 passed
  - `tests/dsp/test_wrapper_stereo_contract.py`: 1 passed
  - `tests/dsp/test_m4c_freeze_qualification.py`: 1 passed
  - `tests/dsp/test_m4b_feedback.py`: 1 passed
  - `tests/dsp/test_m4a_tail_anchor.py`: 1 passed
  - Unit & Preset tests: 37 passed

---

## 5. Conclusion

Milestone M4D fulfills all architectural, mathematical, and musical requirements:
- The wet bus energy is stabilized across all densities and sources.
- BLOOM is fully decoupled from output gain and restored to its musical role of governing fullness, tail, and diffusion.
- Final limiter pumping is eliminated in normal operation (intervention rate: 0.0%).
- Dry signals retain full dynamic integrity regardless of wet activity.

**Sign-off:** `M4D READY FOR REVIEW`
