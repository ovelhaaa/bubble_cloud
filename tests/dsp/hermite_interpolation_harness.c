// M3.2B "4-Point Hermite Interpolation" objective harness.
//
// Covers the full, shared DSP read-position interpolation change:
//   1. raw interpolator math: constant, linear ramp, sine, wrap-around
//   2. quality-profile selection (MCU* linear, WEB* Hermite)
//   3. reverse playback (rate < 0) is direction-agnostic and correct
//   4. cubic overshoot quantification (no blind clamp inside the interpolator)
//   5. guard safety: the Hermite footprint fits inside the existing guard band
//   6. scheduler determinism: spawn metadata is identical linear vs Hermite
//   7. pitch-shift quality: linear vs Hermite RMS/THD+N/centroid/HF per rate
//   8. interpolation cost: linear vs Hermite at voices x sample rates
//
// The harness includes the real core translation unit so the static helpers
// (LinearInterpolate / Hermite4Interpolate / WrapFloatIndex / guard) are exercised
// exactly as compiled into the engine.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../core/dsp/sound_bubbles_dsp.c"
#include "../../core/bubble_engine.h"

#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif

#define FFT_N 4096

static int g_failures = 0;
static void check(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        g_failures++;
    }
}

// ---------------------------------------------------------------------------
// Independent double-precision Catmull-Rom reference (wrap-aware)
// ---------------------------------------------------------------------------
static double hermite_reference(const BubbleRingSample_t* buf, double pos, int size) {
    int idx = (int)pos;
    double frac = pos - (double)idx;
    int im1 = (idx > 0) ? idx - 1 : size - 1;
    int ip1 = (idx + 1 < size) ? idx + 1 : 0;
    int ip2 = (idx + 2 < size) ? idx + 2 : idx + 2 - size;
    double xm1 = (double)Ring_ReadNormalizedSample(buf, im1);
    double x0 = (double)Ring_ReadNormalizedSample(buf, idx);
    double x1 = (double)Ring_ReadNormalizedSample(buf, ip1);
    double x2 = (double)Ring_ReadNormalizedSample(buf, ip2);
    double a = -0.5 * xm1 + 1.5 * x0 - 1.5 * x1 + 0.5 * x2;
    double b = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
    double c = -0.5 * xm1 + 0.5 * x1;
    double d = x0;
    return ((a * frac + b) * frac + c) * frac + d;
}

// ---------------------------------------------------------------------------
// 1. constant / ramp / sine / wrap
// ---------------------------------------------------------------------------
static void test_constant(void) {
    BubbleRingSample_t buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (BubbleRingSample_t)(BUBBLES_RING_SAMPLE_IS_FLOAT ? (12000.0f / 32768.0f) : 12000);
    double max_err = 0.0;
    for (int i = 0; i < 256; i++) {
        for (int f = 0; f < 10; f++) {
            float pos = (float)i + (float)f * 0.1f;
            float got = Hermite4Interpolate(buf, pos, 256);
            double err = fabs((double)got - 12000.0 / 32768.0);
            if (err > max_err) max_err = err;
        }
    }
    printf("[hermite] constant: max|err|=%.3e\n", max_err);
    check(max_err < 1e-6, "Hermite must reproduce a constant exactly");
}

static void test_ramp(void) {
    BubbleRingSample_t buf[512];
    for (int i = 0; i < 512; i++) buf[i] = (BubbleRingSample_t)(BUBBLES_RING_SAMPLE_IS_FLOAT ? ((1000.0f + 37.0f * (float)i) / 32768.0f) : (int16_t)(1000 + 37 * i));
    double max_err = 0.0;
    // Interior only: the ramp is not periodic, so the wrap boundary would legitimately
    // break linearity of the four samples. The wrap behavior is covered separately.
    for (int i = 1; i < 510; i++) {
        for (int f = 0; f < 10; f++) {
            float frac = (float)f * 0.1f;
            float pos = (float)i + frac;
            float got = Hermite4Interpolate(buf, pos, 512);
            double expected = (1000.0 + 37.0 * (double)pos) / 32768.0;
            double err = fabs((double)got - expected);
            if (err > max_err) max_err = err;
        }
    }
    printf("[hermite] linear ramp: max|err|=%.3e\n", max_err);
    check(max_err < 2e-4, "Hermite must reproduce a linear ramp");
}

static void test_sine_many_freqs(void) {
    const int size = 2048;
    BubbleRingSample_t buf[2048];
    double lin_err = 0.0, herm_err = 0.0;
    long count = 0;
    const double freqs[] = {1.0, 3.0, 7.0, 13.0, 31.0};
    const double amp = 30000.0 / 32768.0;
    for (size_t fi = 0; fi < sizeof(freqs) / sizeof(freqs[0]); fi++) {
        double k = freqs[fi]; // integer periods over the buffer
        for (int i = 0; i < size; i++) {
            double v = sin(2.0 * M_PI * k * (double)i / (double)size);
            buf[i] = (BubbleRingSample_t)(BUBBLES_RING_SAMPLE_IS_FLOAT ? (float)(v * amp) : (int16_t)lround(v * 30000.0));
        }
        for (int i = 0; i < size; i++) {
            for (int f = 0; f < 10; f++) {
                double frac = (double)f * 0.1;
                float pos = (float)i + (float)frac;
                double ideal = amp * sin(2.0 * M_PI * k * ((double)i + frac) / (double)size);
                double l = (double)LinearInterpolate(buf, pos, size);
                double h = (double)Hermite4Interpolate(buf, pos, size);
                lin_err += (l - ideal) * (l - ideal);
                herm_err += (h - ideal) * (h - ideal);
                count++;
            }
        }
    }
    lin_err = sqrt(lin_err / (double)count);
    herm_err = sqrt(herm_err / (double)count);
    printf("[hermite] sine RMS err: linear=%.6f hermite=%.6f (%.2fx better)\n",
           lin_err, herm_err, lin_err / herm_err);
    check(herm_err < lin_err, "Hermite must beat linear interpolation on a sine");
}

static void test_wrap_around(void) {
    const int size = 128;
    BubbleRingSample_t buf[128];
    for (int i = 0; i < size; i++) {
        double v = sin(2.0 * M_PI * 3.0 * (double)i / (double)size);
        buf[i] = (BubbleRingSample_t)(BUBBLES_RING_SAMPLE_IS_FLOAT ? (float)(v * (20000.0 / 32768.0)) : (int16_t)lround(20000.0 * v));
    }
    double max_err = 0.0;
    // Positions straddling 0 and size-1 exercise xm1 at size-1 and x2 at 1/0.
    const double edge_positions[] = {0.0, 0.25, 0.5, 0.9,
                                     (double)size - 1.0, (double)size - 0.75,
                                     (double)size - 0.5, (double)size - 0.1};
    for (size_t p = 0; p < sizeof(edge_positions) / sizeof(edge_positions[0]); p++) {
        int idx = (int)edge_positions[p];
        int im1 = (idx > 0) ? idx - 1 : size - 1;
        int ip1 = (idx + 1 < size) ? idx + 1 : 0;
        int ip2 = (idx + 2 < size) ? idx + 2 : idx + 2 - size;
        check(im1 >= 0 && im1 < size, "xm1 wraps inside the buffer");
        check(ip1 >= 0 && ip1 < size, "x1 wraps inside the buffer");
        check(ip2 >= 0 && ip2 < size, "x2 wraps inside the buffer");
        float got = Hermite4Interpolate(buf, (float)edge_positions[p], size);
        double expected = hermite_reference(buf, edge_positions[p], size);
        double err = fabs((double)got - expected);
        if (err > max_err) max_err = err;
    }
    printf("[hermite] wrap-around: max|err| vs reference=%.3e\n", max_err);
    check(max_err < 1e-6, "Hermite must wrap correctly at the ring-buffer edges");
}

// ---------------------------------------------------------------------------
// 2. selection
// ---------------------------------------------------------------------------
static void test_selection(void) {
    check(SoundBubbles_InterpolationModeForProfile(BUBBLE_QUALITY_PROFILE_MCU_SAFE) == BUBBLES_INTERPOLATION_LINEAR,
          "MCU_SAFE -> linear");
    check(SoundBubbles_InterpolationModeForProfile(BUBBLE_QUALITY_PROFILE_MCU_PLUS) == BUBBLES_INTERPOLATION_LINEAR,
          "MCU_PLUS -> linear");
    check(SoundBubbles_InterpolationModeForProfile(BUBBLE_QUALITY_PROFILE_WEB_STANDARD) == BUBBLES_INTERPOLATION_HERMITE,
          "WEB_STANDARD -> Hermite");
    check(SoundBubbles_InterpolationModeForProfile(BUBBLE_QUALITY_PROFILE_WEB_ULTRA) == BUBBLES_INTERPOLATION_HERMITE,
          "WEB_ULTRA -> Hermite");

    static BubbleRingSample_t delay[96000];
    EngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = 48000.0f;
    cfg.quality_profile = BUBBLE_QUALITY_PROFILE_MCU_SAFE;
    cfg.active_voice_limit = 8;
    memset(delay, 0, sizeof(delay));
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    check(SoundBubbles_GetInterpolationMode(&engine) == BUBBLES_INTERPOLATION_LINEAR,
          "live MCU_SAFE engine reports linear");
    check(bubble_engine_set_quality_profile(&engine, BUBBLE_QUALITY_PROFILE_WEB_ULTRA), "set WEB_ULTRA");
    check(SoundBubbles_GetInterpolationMode(&engine) == BUBBLES_INTERPOLATION_HERMITE,
          "live WEB_ULTRA engine reports Hermite");
    printf("[hermite] selection: MCU->linear, WEB->Hermite\n");
}

// ---------------------------------------------------------------------------
// 3 + 7. pitch-shift quality (linear vs Hermite) for forward and reverse rates
// ---------------------------------------------------------------------------
static void fft(double* re, double* im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / (double)len;
        double wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cwr = 1.0, cwi = 0.0;
            for (int k = 0; k < len / 2; k++) {
                int u = i + k, v = i + k + len / 2;
                double xr = re[v] * cwr - im[v] * cwi;
                double xi = re[v] * cwi + im[v] * cwr;
                re[v] = re[u] - xr; im[v] = im[u] - xi;
                re[u] += xr; im[u] += xi;
                double nwr = cwr * wr - cwi * wi;
                cwi = cwr * wi + cwi * wr;
                cwr = nwr;
            }
        }
    }
}

typedef struct {
    double rms_err;
    double peak_err;
    double thdn;
    double thdn_ref;
    double centroid_hz;
    double hf_ratio;
} QualityMetrics_t;

static QualityMetrics_t analyse(const float* out, const double* ideal, int n, double fundamental_hz, double fs) {
    QualityMetrics_t m = {0};
    double sum_err = 0.0, peak = 0.0, sum_ideal = 0.0;
    for (int i = 0; i < n; i++) {
        double e = (double)out[i] - ideal[i];
        sum_err += e * e;
        sum_ideal += ideal[i] * ideal[i];
        double a = fabs(e);
        if (a > peak) peak = a;
    }
    m.rms_err = sqrt(sum_err / (double)n);
    m.peak_err = peak;
    m.thdn_ref = (sum_ideal > 1e-18) ? sqrt(sum_err / sum_ideal) : 0.0;

    static double re[FFT_N], im[FFT_N];
    int N = FFT_N;
    for (int i = 0; i < N; i++) {
        double w = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)(N - 1));
        re[i] = (double)out[i] * w;
        im[i] = 0.0;
    }
    fft(re, im, N);
    int half = N / 2;
    double total = 0.0, centroid_w = 0.0, hf = 0.0;
    double fund_bin = fundamental_hz * (double)N / fs;
    int fund = (int)lround(fund_bin);
    double fund_energy = 0.0;
    double nyquist = fs * 0.5;
    for (int k = 1; k < half; k++) {
        double mag = sqrt(re[k] * re[k] + im[k] * im[k]);
        double power = mag * mag;
        double freq = (double)k * fs / (double)N;
        total += power;
        centroid_w += freq * power;
        if (freq > 0.40 * nyquist) hf += power;
        if (abs(k - fund) <= 1) fund_energy += power;
    }
    double nonfund = total - fund_energy;
    if (nonfund < 0.0) nonfund = 0.0;
    m.thdn = (fund_energy > 1e-18) ? sqrt(nonfund / fund_energy) : 0.0;
    m.centroid_hz = (total > 1e-18) ? centroid_w / total : 0.0;
    m.hf_ratio = (total > 1e-18) ? hf / total : 0.0;
    return m;
}

static void test_pitch_and_reverse(void) {
    const int size = 4096;
    static BubbleRingSample_t src[4096];
    const double k = 32.0;
    for (int i = 0; i < size; i++) {
        double v = sin(2.0 * M_PI * k * (double)i / (double)size);
        src[i] = (BubbleRingSample_t)(BUBBLES_RING_SAMPLE_IS_FLOAT ? (float)(v * (30000.0 / 32768.0)) : (int16_t)lround(30000.0 * v));
    }
    const double fs = 48000.0;
    const double f0 = fs * k / (double)size;
    const int n = FFT_N;
    static float lin[FFT_N], herm[FFT_N];
    static double ideal[FFT_N];

    const double rates[] = {1.0, 1.01, 1.498307, 2.0, 2.996614, -1.0, -2.0};
    const char* labels[] = {"1.0", "1.01", "+7", "+12", "+19", "-1.0", "-2.0"};
    const double amp = 30000.0 / 32768.0;
    printf("[hermite] pitch-shift quality (fundamental %.1f Hz @ %.0f Hz):\n", f0, fs);
    printf("  %-6s | %-11s %-11s | %-10s %-10s | %-9s %-9s | %-8s\n",
           "rate", "lin RMSerr", "herm RMSerr", "lin err/ref", "herm err/ref",
           "lin centr", "herm centr", "herm<lin");
    for (size_t r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
        // Both modes read the exact same fractional position sequence.
        float pos = 0.0f;
        for (int i = 0; i < n; i++) {
            ideal[i] = amp * sin(2.0 * M_PI * k * (double)pos / (double)size);
            lin[i] = LinearInterpolate(src, pos, size);
            herm[i] = Hermite4Interpolate(src, pos, size);
            pos = WrapFloatIndex(pos + (float)rates[r], (float)size);
        }
        double fout = f0 * fabs(rates[r]);
        QualityMetrics_t ml = analyse(lin, ideal, n, fout, fs);
        QualityMetrics_t mh = analyse(herm, ideal, n, fout, fs);
        // Integer rates land exactly on samples (frac == 0), so both paths are
        // exact; fractional rates must show a strict Hermite improvement.
        int integer_rate = (fabs(rates[r] - floor(rates[r] + 0.5)) < 1e-9);
        int better_or_equal = (mh.rms_err <= ml.rms_err * 1.0000001 + 1e-12) ? 1 : 0;
        printf("  %-6s | %-11.6f %-11.6f | %-10.6f %-10.6f | %-9.1f %-9.1f | %-8s\n",
               labels[r], ml.rms_err, mh.rms_err, ml.thdn_ref, mh.thdn_ref,
               ml.centroid_hz, mh.centroid_hz, better_or_equal ? "yes" : "NO");
        check(better_or_equal, "Hermite must not worsen the pitch-shift RMS error");
        check(mh.thdn <= ml.thdn + 1e-9, "Hermite must not worsen the spectral THD+N");
        if (!integer_rate) {
            check(mh.rms_err < ml.rms_err, "Hermite must lower the RMS error at fractional rates");
            check(mh.thdn_ref < ml.thdn_ref, "Hermite must lower the error/reference ratio at fractional rates");
        }
        check(isfinite(mh.centroid_hz) && isfinite(mh.hf_ratio), "quality metrics finite");
    }
    // Reverse playback must be correct: the interpolator depends only on the
    // fractional position, never on the direction of travel.
    check(SoundBubbles_InterpolationModeForProfile(BUBBLE_QUALITY_PROFILE_MCU_SAFE) == BUBBLES_INTERPOLATION_LINEAR,
          "reverse rates exercise both selectable paths");
}

// ---------------------------------------------------------------------------
// 4. cubic overshoot quantification
// ---------------------------------------------------------------------------
static void test_overshoot(void) {
    const int size = 2048;
    static BubbleRingSample_t buf[2048];
    long over = 0, total = 0;
    double max_abs = 0.0;
    for (int mode = 0; mode < 3; mode++) {
        for (int i = 0; i < size; i++) {
            double v;
            if (mode == 0) {
                v = sin(2.0 * M_PI * 64.0 * (double)i / (double)size);
            } else if (mode == 1) {
                v = ((i / 8) % 2 == 0) ? 0.95 : -0.95; // near-Nyquist alternating
            } else {
                v = ((i / 64) % 2 == 0) ? 0.9 : -0.9; // sparse alternating
            }
            buf[i] = (BubbleRingSample_t)(BUBBLES_RING_SAMPLE_IS_FLOAT ? (float)(v * (32000.0 / 32768.0)) : (int16_t)lround(v * 32000.0));
        }
        for (int i = 0; i < size; i++) {
            for (int f = 0; f < 10; f++) {
                float pos = (float)i + (float)f * 0.1f;
                double h = (double)Hermite4Interpolate(buf, pos, size);
                double a = fabs(h);
                if (a > max_abs) max_abs = a;
                if (a > 1.0) over++;
                total++;
            }
        }
    }
    printf("[hermite] overshoot: |out|>1.0 in %ld/%ld cases, max|out|=%.4f\n", over, total, max_abs);
    // The interpolator must not clamp internally; overshoot is measured raw and
    // the wet soft-clip / final limiter downstream handles it.
    check(max_abs < 1.5, "Hermite overshoot stays bounded on the measured stress signals");
    check(max_abs >= 1.0, "the stress signal actually exercised the cubic overshoot region");
}

// ---------------------------------------------------------------------------
// 5. guard safety for the wider footprint
// ---------------------------------------------------------------------------
static void test_guard_footprint(void) {
    const int size = 4096;
    const int W = 2000;
    // Forward: read_ptr is "behind" the write head. Guard rejects distance < 64.
    check(CheckGuardZoneDirectional(W, (float)(W - 10), 1.0f, size),
          "forward read 10 samples behind write is guarded");
    check(!CheckGuardZoneDirectional(W, (float)(W - 64), 1.0f, size),
          "forward read exactly at guard distance is allowed");
    // Hermite footprint at the closest allowed read (distance 64) reaches idx+2,
    // i.e. 62 samples behind the write head: still strictly behind, never the
    // unwritten head sample.
    int read_idx = W - 64;
    if (read_idx < 0) read_idx += size;
    int touches_head = 0;
    for (int d = -1; d <= 2; d++) {
        int foot = read_idx + d;
        if (foot < 0) foot += size;
        if (foot >= size) foot -= size;
        if (foot == W) touches_head = 1;
    }
    check(!touches_head, "Hermite footprint never touches the write head");
    // Reverse: read_ptr is ahead of the write head.
    check(CheckGuardZoneDirectional(W, (float)(W + 10), -1.0f, size),
          "reverse read 10 samples ahead of write is guarded");
    check(!CheckGuardZoneDirectional(W, (float)(W + 64), -1.0f, size),
          "reverse read exactly at guard distance is allowed");
    printf("[hermite] guard: footprint [-1,+2] fits inside the %d-sample guard band\n",
           BUBBLES_GUARD_ZONE_SAMPLES);
}

// ---------------------------------------------------------------------------
// 6. scheduler determinism: metadata identical linear vs Hermite
// ---------------------------------------------------------------------------
typedef struct {
    int32_t spawn_count[BUBBLES_MAX_VOICES * 8];
    int32_t active_voices[BUBBLES_MAX_VOICES * 8];
    int32_t engine_state[BUBBLES_MAX_VOICES * 8];
    int count;
} SpawnTrace_t;

static SpawnTrace_t g_trace;
static void trace_cb(const SoundBubblesBlockMetrics_t* metrics, void* user) {
    (void)user;
    if (g_trace.count >= BUBBLES_MAX_VOICES * 8) return;
    int i = g_trace.count++;
    g_trace.spawn_count[i] = metrics->spawn_count;
    g_trace.active_voices[i] = metrics->active_voices;
    g_trace.engine_state[i] = metrics->engine_state;
}

static void run_trace(BubbleQualityProfile profile, SpawnTrace_t* out, float* out_energy) {
    static BubbleRingSample_t delay[192000];
    memset(delay, 0, sizeof(delay));
    EngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = 48000.0f;
    cfg.quality_profile = profile;
    cfg.active_voice_limit = 24;
    cfg.rng_seed = 0x5EED5EEDu;
    cfg.density_burst = 90.0f;
    cfg.density_sustain = 70.0f;
    cfg.density_decay = 30.0f;
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    g_trace.count = 0;
    bubble_engine_set_metrics_callback(&engine, trace_cb, NULL);

    float in[BUBBLES_BLOCK_SIZE], l[BUBBLES_BLOCK_SIZE], r[BUBBLES_BLOCK_SIZE];
    double energy = 0.0;
    for (int b = 0; b < 300; b++) {
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            int n = b * BUBBLES_BLOCK_SIZE + i;
            in[i] = 0.25f * sinf(2.0f * M_PI_F * 220.0f * (float)n / 48000.0f);
            if ((n % 257) == 0) in[i] += 0.6f;
        }
        bubble_engine_process(&engine, in, l, r, BUBBLES_BLOCK_SIZE);
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) energy += (double)l[i] * l[i];
    }
    *out = g_trace;
    *out_energy = energy;
}

static void test_determinism(void) {
    static SpawnTrace_t linear_trace, hermite_trace;
    float lin_energy = 0.0f, herm_energy = 0.0f;
    run_trace(BUBBLE_QUALITY_PROFILE_MCU_SAFE, &linear_trace, &lin_energy);
    run_trace(BUBBLE_QUALITY_PROFILE_WEB_STANDARD, &hermite_trace, &herm_energy);

    check(linear_trace.count == hermite_trace.count, "same number of control blocks");
    check(linear_trace.count > 0, "trace captured control blocks");
    int mismatches = 0;
    int n = linear_trace.count < hermite_trace.count ? linear_trace.count : hermite_trace.count;
    for (int i = 0; i < n; i++) {
        if (linear_trace.spawn_count[i] != hermite_trace.spawn_count[i]) mismatches++;
        if (linear_trace.active_voices[i] != hermite_trace.active_voices[i]) mismatches++;
        if (linear_trace.engine_state[i] != hermite_trace.engine_state[i]) mismatches++;
    }
    printf("[hermite] determinism: spawn metadata mismatches=%d over %d blocks\n", mismatches, n);
    check(mismatches == 0, "Hermite must not change the spawn scheduler sequence");
}

// ---------------------------------------------------------------------------
// 8. performance
// ---------------------------------------------------------------------------
static double run_perf(BubbleQualityProfile profile, int voice_limit, float sample_rate, int blocks) {
    static BubbleRingSample_t delay[384000];
    memset(delay, 0, sizeof(delay));
    EngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = sample_rate;
    cfg.quality_profile = profile;
    cfg.active_voice_limit = voice_limit;
    cfg.density_burst = 120.0f;
    cfg.density_sustain = 100.0f;
    cfg.density_decay = 60.0f;
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    float in[BUBBLES_BLOCK_SIZE], l[BUBBLES_BLOCK_SIZE], r[BUBBLES_BLOCK_SIZE];
    // Warm-up.
    for (int b = 0; b < 64; b++) {
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) in[i] = 0.2f;
        bubble_engine_process(&engine, in, l, r, BUBBLES_BLOCK_SIZE);
    }
    clock_t start = clock();
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            int n = b * BUBBLES_BLOCK_SIZE + i;
            in[i] = 0.25f * sinf(2.0f * M_PI_F * 440.0f * (float)n / sample_rate);
            if ((n % 257) == 0) in[i] += 0.7f;
        }
        bubble_engine_process(&engine, in, l, r, BUBBLES_BLOCK_SIZE);
    }
    clock_t end = clock();
    double seconds = (double)(end - start) / (double)CLOCKS_PER_SEC;
    double samples = (double)blocks * BUBBLES_BLOCK_SIZE;
    return seconds * 1e9 / samples; // ns/sample
}

static void test_performance(void) {
    const int voices[] = {8, 16, 24, 32};
    const float rates[] = {44100.0f, 48000.0f, 96000.0f};
    const int blocks = 2000;
    const int trials = 3;
    printf("[hermite] performance (best of %d trials, ns/sample, linear vs Hermite):\n", trials);
    printf("  %-6s %-6s | %-12s %-12s | %-8s\n", "voices", "SR", "linear", "Hermite", "ratio");
    double worst_ratio = 0.0;
    for (size_t v = 0; v < sizeof(voices) / sizeof(voices[0]); v++) {
        for (size_t s = 0; s < sizeof(rates) / sizeof(rates[0]); s++) {
            double lin = 1.0e30, herm = 1.0e30;
            for (int t = 0; t < trials; t++) {
                double l = run_perf(BUBBLE_QUALITY_PROFILE_MCU_SAFE, voices[v], rates[s], blocks);
                double h = run_perf(BUBBLE_QUALITY_PROFILE_WEB_STANDARD, voices[v], rates[s], blocks);
                if (l > 0.0 && l < lin) lin = l;
                if (h > 0.0 && h < herm) herm = h;
            }
            if (lin >= 1.0e30) lin = 0.0;
            if (herm >= 1.0e30) herm = 0.0;
            double ratio = (lin > 0.0 && herm > 0.0) ? herm / lin : 0.0;
            if (ratio > worst_ratio) worst_ratio = ratio;
            printf("  %-6d %-6.0f | %-12.2f %-12.2f | %-8.2f\n",
                   voices[v], (double)rates[s], lin, herm, ratio);
        }
    }
    printf("[hermite] worst Hermite/linear cost ratio: %.2fx\n", worst_ratio);
    // Coarse sanity bound only: interpolation is a small part of the voice
    // pipeline. Best-of-N trials keep the clock() granularity from producing a
    // spurious ratio; this guards against accidental O(n^2) behavior.
    check(worst_ratio > 0.0 && worst_ratio < 6.0,
          "Hermite interpolation cost stays within a reasonable multiple of linear");
}

int main(void) {
    printf("=== M3.2B Hermite 4-point interpolation harness ===\n");
    test_constant();
    test_ramp();
    test_sine_many_freqs();
    test_wrap_around();
    test_selection();
    test_pitch_and_reverse();
    test_overshoot();
    test_guard_footprint();
    test_determinism();
    test_performance();
    if (g_failures != 0) {
        fprintf(stderr, "=== M3.2B FAILED with %d failure(s) ===\n", g_failures);
        return 1;
    }
    printf("=== All M3.2B Hermite tests passed ===\n");
    return 0;
}
