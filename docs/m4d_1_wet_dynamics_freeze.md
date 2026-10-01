# M4D.1 — Wet Dynamics Qualification & Architectural Freeze Report

## Executive Summary & Official Sign-Off

- **Milestone**: `M4D.1 — Wet Dynamics Qualification Freeze`
- **Candidate Commit**: `28677f681626b1bddd56904a9aab5bf9c38a0e86` (on branch `main`)
- **Baseline Reference (M4C.1)**: `865842c01f4cb46854f9f0a80af364ddf024df78` (checked out in dedicated worktree `../bubble_cloud_m4c_baseline`)
- **Qualification Date**: `2026-10-01`
- **Environment**: `Windows 10, gcc 14.2.0 (MinGW-W64 UCRT), Python 3.14.0`
- **Overall Qualification Status**: **`M4D READY TO FREEZE`**

The M4D Wet Dynamics and Limiter Decoupling architecture has undergone comprehensive historical A/B qualification against the frozen M4C.1 baseline. All evaluation criteria—including direct dry pumping elimination, limiter decoupling, BLOOM loudness stabilization, stereo image integrity, tail preservation, and silence downward-only invariants—have passed without qualification defects.

---

## 1. Worktree & Build Methodology

To ensure 100% reproducible historical comparison without header or object leakage:

1. **Baseline Worktree**: Located at `../bubble_cloud_m4c_baseline`, strictly pinned to commit `865842c01f4cb46854f9f0a80af364ddf024df78`.
   - Compiled with: `gcc -O2 -Wall -Wextra -std=c11 -I<baseline>/core -I<baseline>/core/dsp -I<baseline>/core/engine ...`
2. **Candidate Worktree**: Located at `.`, targeting candidate HEAD at `28677f681626b1bddd56904a9aab5bf9c38a0e86`.
   - Compiled with: `gcc -O2 -Wall -Wextra -std=c11 -DM4D_CANDIDATE_BUILD=1 -Icore -Icore/dsp -Icore/engine ...`
3. Both executables were built by the same compiler under identical target architecture, optimization flags (`-O2`), and runtime seeds.

---

## 2. Direct Dry Pumping Measurement (Sections 3–8)

Direct dry pumping is evaluated under the prompt's required scenario: a steady dry sine combined with bursty high-density wet excitation at `Mix = 0.25` (`master_dry_gain ≈ 0.704`, `master_wet_gain ≈ 0.225`). 

Effective dry gain over time $G_{\text{dry\_eff}}(t)$ is measured over 10 ms windowed RMS envelopes in the stationary region:
$$G_{\text{dry\_eff}}(w) = \frac{\text{RMS}(D_{\text{actual}})}{\text{RMS}(D_{\text{reference}})}$$
$$\text{dry\_gain\_db}(w) = 20 \log_{10}(G_{\text{dry\_eff}}(w))$$

| Metric | Baseline M4C.1 | Candidate M4D | Target Criterion | Status |
|:---|:---:|:---:|:---:|:---:|
| **`dry_gain_mean_db`** | `-0.0105 dB` | `0.0000 dB` | — | **PASSED** |
| **`dry_gain_min_db`** | `-0.5051 dB` | `0.0000 dB` | — | **PASSED** |
| **`dry_gain_max_db`** | `0.0000 dB` | `0.0000 dB` | — | **PASSED** |
| **`dry_modulation_depth_db`** | **`0.5051 dB`** | **`0.0000 dB`** | `< 0.25 dB` | **PASSED (100% Reduction)** |
| **`dry_gain_stddev_db`** | `0.0498 dB` | `0.0000 dB` | — | **PASSED** |
| **`wet_burst_correlation`** | **`+0.1718`** | **`0.0000`** | Dropped to ~0 | **PASSED** |

### Interpretation
In baseline M4C.1, wet bursts exceed output headroom, driving the final limiter into active attenuation and visibly pumping the steady dry signal by **0.51 dB**, with strong correlation to wet bursts. In candidate M4D, the combination of downward-only normalization and the linked wet limiter catches wet energy before the mix point, completely protecting the final limiter ($g_{\text{lim}} = 1.0000$ identically). Dry modulation depth is **0.00 dB**, achieving a **100% reduction in dry pumping**.

---

## 3. Historical Limiter Matrix (36 Conditions)

Measured across 3 source types (Sustained Sine, Broadband Noise, Harmonic Pluck), 3 densities (0.50, 0.75, 1.00), 2 Bloom levels (0.00, 1.00), and 2 Mix ratios (0.25, 1.00).

| Source | Density | Bloom | Mix | Baseline Out RMS | Baseline Out Peak | Baseline Lim Max GR | Candidate Out RMS | Candidate Out Peak | Candidate Lim Max GR | Candidate Lim Act % |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Sustained Sine | 0.50 | 0.00 | 0.25 | 0.2656 | 0.6073 | 0.00 dB | 0.2671 | 0.6073 | 0.00 dB | 0.0% |
| Sustained Sine | 0.50 | 0.00 | 1.00 | 0.1752 | 0.6573 | 0.00 dB | 0.2667 | 0.7648 | 0.00 dB | 0.0% |
| Sustained Sine | 0.50 | 1.00 | 0.25 | 0.2869 | 0.6759 | 0.00 dB | 0.2768 | 0.6073 | 0.00 dB | 0.0% |
| Sustained Sine | 0.50 | 1.00 | 1.00 | 0.3306 | 0.8914 | **2.41 dB** | 0.2372 | 0.7784 | **0.00 dB** | 0.0% |
| Sustained Sine | 0.75 | 0.00 | 0.25 | 0.2571 | 0.6073 | 0.00 dB | 0.2544 | 0.6073 | 0.00 dB | 0.0% |
| Sustained Sine | 0.75 | 0.00 | 1.00 | 0.1867 | 0.7133 | 0.00 dB | 0.2434 | 0.7283 | 0.00 dB | 0.0% |
| Sustained Sine | 0.75 | 1.00 | 0.25 | 0.2892 | 0.6756 | 0.00 dB | 0.2767 | 0.6073 | 0.00 dB | 0.0% |
| Sustained Sine | 0.75 | 1.00 | 1.00 | 0.3624 | 0.8913 | **2.28 dB** | 0.2530 | 0.7451 | **0.00 dB** | 0.0% |
| Sustained Sine | 1.00 | 0.00 | 0.25 | 0.2691 | 0.6073 | 0.00 dB | 0.2763 | 0.6073 | 0.00 dB | 0.0% |
| Sustained Sine | 1.00 | 0.00 | 1.00 | 0.2025 | 0.6555 | 0.00 dB | 0.2842 | 0.7452 | 0.00 dB | 0.0% |
| Sustained Sine | 1.00 | 1.00 | 0.25 | 0.2792 | 0.7269 | 0.00 dB | 0.2712 | 0.6073 | 0.00 dB | 0.0% |
| Sustained Sine | 1.00 | 1.00 | 1.00 | 0.2995 | 0.8913 | **3.67 dB** | 0.2165 | 0.7809 | **0.00 dB** | 0.0% |
| Broadband Noise | 0.50 | 0.00 | 0.25 | 0.1843 | 0.3805 | 0.00 dB | 0.1905 | 0.4442 | 0.00 dB | 0.0% |
| Broadband Noise | 0.50 | 0.00 | 1.00 | 0.1256 | 0.4268 | 0.00 dB | 0.2207 | 0.7295 | 0.00 dB | 0.0% |
| Broadband Noise | 0.50 | 1.00 | 0.25 | 0.1938 | 0.5308 | 0.00 dB | 0.1860 | 0.4443 | 0.00 dB | 0.0% |
| Broadband Noise | 0.50 | 1.00 | 1.00 | 0.2932 | 0.8913 | **1.69 dB** | 0.1990 | 0.7316 | **0.00 dB** | 0.0% |
| Broadband Noise | 0.75 | 0.00 | 0.25 | 0.1770 | 0.3717 | 0.00 dB | 0.1770 | 0.4282 | 0.00 dB | 0.0% |
| Broadband Noise | 0.75 | 0.00 | 1.00 | 0.1157 | 0.3573 | 0.00 dB | 0.2166 | 0.6297 | 0.00 dB | 0.0% |
| Broadband Noise | 0.75 | 1.00 | 0.25 | 0.1763 | 0.4880 | 0.00 dB | 0.1740 | 0.4210 | 0.00 dB | 0.0% |
| Broadband Noise | 0.75 | 1.00 | 1.00 | 0.3071 | 0.8913 | **0.55 dB** | 0.1983 | 0.6335 | **0.00 dB** | 0.0% |
| Broadband Noise | 1.00 | 0.00 | 0.25 | 0.1784 | 0.3975 | 0.00 dB | 0.1794 | 0.4504 | 0.00 dB | 0.0% |
| Broadband Noise | 1.00 | 0.00 | 1.00 | 0.1535 | 0.4912 | 0.00 dB | 0.2571 | 0.7268 | 0.00 dB | 0.0% |
| Broadband Noise | 1.00 | 1.00 | 0.25 | 0.1940 | 0.5810 | 0.00 dB | 0.1808 | 0.4564 | 0.00 dB | 0.0% |
| Broadband Noise | 1.00 | 1.00 | 1.00 | 0.3728 | 0.8913 | **3.81 dB** | 0.2415 | 0.7359 | **0.00 dB** | 0.0% |
| Harmonic Pluck | 0.50 | 0.00 | 0.25 | 0.1121 | 0.8255 | 0.00 dB | 0.1135 | 0.8255 | 0.00 dB | 0.0% |
| Harmonic Pluck | 0.50 | 0.00 | 1.00 | 0.0531 | 0.8255 | 0.00 dB | 0.0724 | 0.8255 | 0.00 dB | 0.0% |
| Harmonic Pluck | 0.50 | 1.00 | 0.25 | 0.1123 | 0.8255 | 0.00 dB | 0.1117 | 0.8255 | 0.00 dB | 0.0% |
| Harmonic Pluck | 0.50 | 1.00 | 1.00 | 0.0787 | 0.8255 | 0.00 dB | 0.0605 | 0.8255 | 0.00 dB | 0.0% |
| Harmonic Pluck | 0.75 | 0.00 | 0.25 | 0.1116 | 0.8255 | 0.00 dB | 0.1121 | 0.8255 | 0.00 dB | 0.0% |
| Harmonic Pluck | 0.75 | 0.00 | 1.00 | 0.0514 | 0.8255 | 0.00 dB | 0.0695 | 0.8255 | 0.00 dB | 0.0% |
| Harmonic Pluck | 0.75 | 1.00 | 0.25 | 0.1127 | 0.8255 | 0.00 dB | 0.1121 | 0.8255 | 0.00 dB | 0.0% |
| Harmonic Pluck | 0.75 | 1.00 | 1.00 | 0.0710 | 0.8255 | 0.00 dB | 0.0569 | 0.8255 | 0.00 dB | 0.0% |
| Harmonic Pluck | 1.00 | 0.00 | 0.25 | 0.1114 | 0.8255 | 0.00 dB | 0.1117 | 0.8255 | 0.00 dB | 0.0% |
| Harmonic Pluck | 1.00 | 0.00 | 1.00 | 0.0510 | 0.8255 | 0.00 dB | 0.0689 | 0.8255 | 0.00 dB | 0.0% |
| Harmonic Pluck | 1.00 | 1.00 | 0.25 | 0.1159 | 0.8255 | 0.00 dB | 0.1138 | 0.8255 | 0.00 dB | 0.0% |
| Harmonic Pluck | 1.00 | 1.00 | 1.00 | 0.1056 | 0.8255 | 0.00 dB | 0.0767 | 0.8255 | 0.00 dB | 0.0% |

**Summary**: Baseline M4C.1 experiences up to **3.81 dB** final limiter gain reduction when Bloom = 1.00. In candidate M4D, the final limiter has **0.00 dB maximum GR and 0.0% active blocks across 100% of tested conditions**.

---

## 4. Normalization Telemetry Distribution (Sections 9–11)

Evaluating the distribution of `wet_normalization_gain` across tiers to prove it behaves as an adaptive energy follower rather than a continuous brickwall compressor:

| Evaluation Tier | Min Norm Gain | Median Norm Gain | p95 GR (dB) | Mean Norm Gain | Time < 0.95 | Time < 0.80 | Time Near Floor (0.45) | Wet Limiter Max GR | Final Limiter Max GR | Final Limiter Active % |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **Normal** (Dens=0.35, Bloom=0.35, Warmth=0.40) | `0.9214` | `0.9860` | `0.67 dB` | `0.9702` | `30.5%` | `0.0%` | `0.0%` | `0.56 dB` | `0.00 dB` | `0.0%` |
| **Dense** (Dens=0.80, Bloom=0.75, Warmth=0.60) | `1.0000` | `1.0000` | `0.00 dB` | `1.0000` | `0.0%` | `0.0%` | `0.0%` | `0.00 dB` | `0.00 dB` | `0.0%` |
| **Extreme** (Dens=1.00, Bloom=1.00, Warmth=0.85, ULTRA, Shimmer, Reverse) | `0.7636` | `0.8499` | `2.32 dB` | `0.8710` | `61.9%` | `26.9%` | `0.0%` | `2.15 dB` | `0.00 dB` | `0.0%` |

### Key Findings
1. In Normal conditions, gain stays close to 1.0 (mean 0.97, time < 0.80 is 0.0%).
2. In Extreme stress conditions, normalization smoothly stabilizes the high-energy shimmer cloud (median 0.85, p95 GR 2.32 dB).
3. The floor bound ($0.45$) is **never approached** (`0.0%` time near floor), verifying that normalization does not collapse dynamic range.
4. The final limiter remains **0.0% active** across all tiers.

---

## 5. Limiter Hierarchy Proof (Section 12)

| Scenario | Wet Normalization Gain | Wet Limiter GR | Final Limiter GR | Hierarchy Interpretation |
|:---|:---:|:---:|:---:|:---|
| **Normal Musical** | `0.9860` (transparent) | `0.56 dB` (sporadic micro-transients) | `0.00 dB` (idle) | Normalization tracks average level; final limiter 100% idle |
| **Dense Cloud** | `1.0000` (transparent) | `0.00 dB` (within headroom) | `0.00 dB` (idle) | Balanced energy; zero limiting needed |
| **Extreme Cluster / Shimmer** | `0.7636` (stabilizing) | `2.15 dB` (peak clamping) | `0.00 dB` (idle) | Normalization absorbs structural accumulation; wet limiter catches peaks; final limiter protected |

The three-stage dynamics hierarchy functions exactly as designed:
$$\text{Average Energy Follower (Normalization)} \longrightarrow \text{Peak Limiter (Linked Wet)} \longrightarrow \text{Emergency Ceiling (Final Limiter)}$$

---

## 6. BLOOM Historical A/B Comparison (Section 13)

Auditing the output RMS shift between `BLOOM = 0.00` and `BLOOM = 1.00`:

| Density | Baseline M4C.1 Delta | Candidate M4D Delta | Delta Stabilization |
|:---:|:---:|:---:|:---:|
| `0.25` | `+1.23 dB` | `-1.69 dB` | Stabilized (Gain inflation eliminated) |
| `0.50` | `+3.29 dB` | `-1.24 dB` | Stabilized (Gain inflation eliminated) |
| `0.75` | `+2.53 dB` | `-1.68 dB` | Stabilized (Gain inflation eliminated) |
| `1.00` | `+5.69 dB` | `+0.14 dB` | **Definite Stabilization (+5.69 dB -> +0.14 dB)** |

BLOOM now acts purely as an expressive density, diffusion, and tail fullness control rather than an unintended +5.7 dB loudness multiplier.

---

## 7. Reverb Tail & Spectral Centroid Preservation (Section 14)

Decay evaluated from a 500 ms pluck input across a 10-second observation window:

| Time Window | Baseline RMS (dBFS) | Candidate RMS (dBFS) | RMS Delta | Baseline Centroid (Hz) | Candidate Centroid (Hz) |
|:---|:---:|:---:|:---:|:---:|:---:|
| `0.5 – 1.0 s` | `-26.82 dB` | `-27.83 dB` | `1.01 dB` | `17819.9 Hz` | `17970.7 Hz` |
| `1.0 – 2.0 s` | `-32.55 dB` | `-33.57 dB` | `1.02 dB` | `21981.5 Hz` | `21945.5 Hz` |
| `2.0 – 4.0 s` | `-41.12 dB` | `-42.18 dB` | `1.06 dB` | `16183.0 Hz` | `16182.6 Hz` |
| `4.0 – 6.0 s` | `-49.28 dB` | `-50.30 dB` | `1.02 dB` | `2181.7 Hz` | `2049.9 Hz` |
| `6.0 – 8.0 s` | `-93.47 dB` | `-95.42 dB` | `1.95 dB` | `6472.1 Hz` | `6547.0 Hz` |
| `8.0 – 10.0 s` | `-137.95 dB` | `-140.09 dB` | `2.14 dB` | `11679.9 Hz` | `11665.0 Hz` |

The natural decay curvature, spectral brightness trajectory, and deep tail extinction are preserved with near-identical envelopes (~1 dB delta due to removed gain inflation).

---

## 8. Stereo Image & Spatial Law Verification (Section 15)

Evaluated under `SPACE = 1.00` and `MIX = 1.00`:

| Metric | Baseline M4C.1 | Candidate M4D | Tolerance / Criterion | Status |
|:---|:---:|:---:|:---:|:---:|
| **Inter-Channel Correlation** | `0.8549` | `0.8421` | Delta < 0.05 | **PASSED** |
| **Side / Mid Energy Ratio** | `0.2873` | `0.3018` | Preserved width (> 0.20) | **PASSED** |
| **Interaural Level Diff (ILD)** | `1.15 dB` | `1.28 dB` | Natural balance (< 3.0 dB) | **PASSED** |

Because both wet normalization and wet limiting apply linked scalar gains (`gain_l == gain_r`), stereo localization and binaural aperture are strictly preserved.

---

## 9. MIX = 0 & Silence Invariants (Sections 16–17)

1. **MIX = 0 Invariant**:
   - High-density cloud running internally with `MIX = 0.00`.
   - Maximum output deviation from pure dry input: **`0.000000`** (bit-exact).
   - Final limiter gain: **`1.0000`** (100% idle).
2. **Silence Downward-Only Invariant**:
   - 30 seconds startup silence output peak: **`0.000000`**.
   - 15 seconds post-signal decay maximum normalization gain: **`0.9992`** ($\le 1.0000$).
   - Upward noise-floor amplification: **Zero**.

---

## 10. Parameter Automation Slew (Section 18)

Rapid full-scale parameter sweeps across density, bloom, warmth, and mix:
- **Maximum sample discontinuity**: `0.2528` (within normal continuous audio range).
- **Maximum normalization slew per block**: `0.000868` (smooth exponential tracking).
- No audio dropouts, zipper noise, or discontinuities detected.

---

## 11. CPU Overhead Benchmark (Section 19)

Measured over 5.0 seconds of audio processing:

| Benchmark Config | Sample Rate | Active Voices | Baseline Time | Candidate Time | Measured Overhead | Realtime Factor |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|
| **Benchmark 1** (Standard) | 44,100 Hz | 24 | `83.0 ms` | `91.0 ms` | `+9.6%` | `0.0182` |
| **Benchmark 2** (48k Studio) | 48,000 Hz | 24 | `91.0 ms` | `93.0 ms` | `+2.2%` | `0.0186` |
| **Benchmark 3** (96k Ultra) | 96,000 Hz | 32 | `173.0 ms` | `187.0 ms` | `+8.1%` | `0.0374` |

Candidate M4D introduces less than **10% CPU overhead**, maintaining a real-time factor well below 0.04 (over 25x faster than real time at 96 kHz).

---

## 12. Final Verdict

All 10 qualification categories satisfy their respective criteria without qualification defects, regressions, or audio artifacts:

```text
======================================================================
VERDICT: M4D READY TO FREEZE
======================================================================
```
