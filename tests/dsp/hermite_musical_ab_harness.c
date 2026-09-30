// M3.2B musical A/B harness: linear (MCU) vs Hermite (WEB) through the real
// engine, with identical seed/preset/input/scheduler.
//
// Materials: pluck (decaying harmonic guitar-like), pad (sustained), transient
// (click train). Sparkle: 0.0 / 0.5 / 1.0. The point is not bit parity (the
// read path legitimately differs) but to verify Hermite does not blow up the
// bus, does not darken the spectrum, keeps the limiter in range, and that the
// difference is a bounded interpolation texture rather than a different cloud.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/bubble_engine.h"

#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FFT_N 2048
#define SR 44100.0f

static int g_failures = 0;
static void check(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        g_failures++;
    }
}

// --- deterministic materials ---
static void synth_pluck(float* out, int n) {
    for (int i = 0; i < n; i++) {
        double t = (double)i / SR;
        double env = exp(-t * 3.2);
        double v = 0.0;
        const double partials[6] = {196.0, 392.0, 588.0, 784.0, 1176.0, 1568.0};
        for (int p = 0; p < 6; p++) {
            v += (1.0 / (p + 1.0)) * sin(2.0 * M_PI * partials[p] * t) * exp(-t * (2.0 + p));
        }
        // short pick transient
        if (t < 0.01) v += 0.6 * sin(2.0 * M_PI * 3000.0 * t) * exp(-t * 400.0);
        out[i] = (float)(0.22 * env * v);
    }
}

static void synth_pad(float* out, int n) {
    for (int i = 0; i < n; i++) {
        double t = (double)i / SR;
        double env = fmin(1.0, t / 0.5) * fmin(1.0, ((double)n / SR - t) / 0.5);
        double v = 0.0;
        const double partials[5] = {110.0, 164.81, 220.0, 329.63, 440.0};
        const double amps[5] = {0.55, 0.28, 0.22, 0.14, 0.08};
        for (int p = 0; p < 5; p++) {
            v += amps[p] * sin(2.0 * M_PI * partials[p] * t + 0.3 * sin(2.0 * M_PI * 0.6 * t));
        }
        out[i] = (float)(0.26 * env * v);
    }
}

static void synth_transient(float* out, int n) {
    memset(out, 0, (size_t)n * sizeof(float));
    int step = (int)(SR * 0.4);
    for (int onset = 0; onset < n; onset += step) {
        for (int i = onset; i < n && i < onset + (int)(SR * 0.02); i++) {
            double t = (double)(i - onset) / SR;
            out[i] += (float)(0.65 * sin(2.0 * M_PI * 1400.0 * t) * exp(-t * 220.0));
        }
        for (int i = onset; i < n && i < onset + (int)(SR * 0.2); i++) {
            double t = (double)(i - onset) / SR;
            out[i] += (float)(0.22 * sin(2.0 * M_PI * 196.0 * t) * exp(-t * 7.0));
        }
    }
}

// --- metrics ---
typedef struct {
    float* l;
    float* r;
    int n;
} Render_t;

static float g_min_limiter = 1.0f;
static int g_last_mode = -1;
static unsigned long long g_last_linear = 0, g_last_hermite = 0;
static void metrics_cb(const SoundBubblesBlockMetrics_t* m, void* user) {
    (void)user;
    if (m->limiter_gain < g_min_limiter) g_min_limiter = m->limiter_gain;
}

static void render(BubbleQualityProfile profile, float sparkle, const float* in, int n, Render_t* out) {
    static BubbleRingSample_t delay[192000];
    memset(delay, 0, sizeof(delay));
    EngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = SR;
    cfg.quality_profile = profile;
    cfg.rng_seed = 0xA11CEu;
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_SPARKLE, sparkle);
    g_min_limiter = 1.0f;
    bubble_engine_set_metrics_callback(&engine, metrics_cb, NULL);
    // Fully wet, dense cloud so the read path dominates the output (macro
    // smoothing settles during the render window).
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_MIX, 1.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DENSITY, 0.7f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_BLOOM, 0.8f);
    for (int i = 0; i < n; i += BUBBLES_BLOCK_SIZE) {
        int chunk = BUBBLES_BLOCK_SIZE;
        if (i + chunk > n) chunk = n - i;
        bubble_engine_process(&engine, &in[i], &out->l[i], &out->r[i], chunk);
    }
    out->n = n;
    g_last_mode = (int)SoundBubbles_GetInterpolationMode(&engine);
#if defined(BUBBLES_INTERPOLATION_TELEMETRY)
    {
        uint64_t l = 0, h = 0;
        SoundBubbles_GetInterpolationCallCounts(&engine, &l, &h);
        g_last_linear = (unsigned long long)l;
        g_last_hermite = (unsigned long long)h;
    }
#endif
}

typedef struct {
    double rms;
    double peak;
    double centroid_hz;
    double low_ratio;
    double mid_ratio;
    double high_ratio;
    double correlation;
    double limiter_gr_db;
} Metrics_t;

static void fft(double* re, double* im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
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
                re[v] = re[u] - xr; im[v] = im[u] - xi; re[u] += xr; im[u] += xi;
                double nwr = cwr * wr - cwi * wi;
                cwi = cwr * wi + cwi * wr; cwr = nwr;
            }
        }
    }
}

static Metrics_t analyse(const Render_t* render_in) {
    Metrics_t m = {0};
    double sum_l = 0.0, sum_r = 0.0, sum_cross = 0.0, peak = 0.0;
    for (int i = 0; i < render_in->n; i++) {
        double l = render_in->l[i], r = render_in->r[i];
        sum_l += l * l; sum_r += r * r; sum_cross += l * r;
        double a = fabs(l); if (a > peak) peak = a;
        a = fabs(r); if (a > peak) peak = a;
    }
    int n = render_in->n;
    m.rms = sqrt((sum_l + sum_r) / (2.0 * (double)n));
    m.peak = peak;
    m.correlation = sum_cross / sqrt(fmax(1e-18, sum_l * sum_r));
    m.limiter_gr_db = (g_min_limiter > 0.0f) ? -20.0 * log10((double)g_min_limiter) : 0.0;

    // Mono spectrum, averaged over non-overlapping windows.
    static double re[FFT_N], im[FFT_N];
    double total = 0.0, centroid_w = 0.0, low = 0.0, mid = 0.0, high = 0.0;
    int frames = n / FFT_N;
    if (frames < 1) frames = 1;
    for (int f = 0; f < frames; f++) {
        int start = f * FFT_N;
        for (int i = 0; i < FFT_N; i++) {
            double w = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)(FFT_N - 1));
            re[i] = 0.5 * ((double)render_in->l[start + i] + (double)render_in->r[start + i]) * w;
            im[i] = 0.0;
        }
        fft(re, im, FFT_N);
        for (int k = 1; k < FFT_N / 2; k++) {
            double mag = sqrt(re[k] * re[k] + im[k] * im[k]);
            double power = mag * mag;
            double freq = (double)k * (double)SR / (double)FFT_N;
            total += power; centroid_w += freq * power;
            if (freq < 300.0) low += power;
            else if (freq < 3000.0) mid += power;
            else high += power;
        }
    }
    m.centroid_hz = (total > 1e-18) ? centroid_w / total : 0.0;
    m.low_ratio = (total > 1e-18) ? low / total : 0.0;
    m.mid_ratio = (total > 1e-18) ? mid / total : 0.0;
    m.high_ratio = (total > 1e-18) ? high / total : 0.0;
    return m;
}

static double diff_rms(const Render_t* a, const Render_t* b) {
    double sum = 0.0;
    for (int i = 0; i < a->n; i++) {
        double dl = (double)a->l[i] - (double)b->l[i];
        double dr = (double)a->r[i] - (double)b->r[i];
        sum += dl * dl + dr * dr;
    }
    return sqrt(sum / (2.0 * (double)a->n));
}

int main(void) {
    printf("=== M3.2B musical A/B (linear MCU_SAFE vs Hermite WEB_STANDARD) ===\n");
    const int n = (int)(SR * 2.0);
    static float in[90000];
    static float lin_l[90000], lin_r[90000], herm_l[90000], herm_r[90000];
    const char* mat_names[] = {"pluck", "pad", "transient"};
    const float sparkles[] = {0.0f, 0.5f, 1.0f};
    const char* sparkle_names[] = {"0.0", "0.5", "1.0"};
    double max_diff_pct = 0.0;
    int mode_ok = 1;

    printf("  %-10s %-7s | %-8s %-8s | %-8s %-8s | %-9s | %-9s | %-8s | %-7s\n",
           "material", "sparkle", "lin rms", "herm rms", "lin peak", "herm peak",
           "lin centr", "herm centr", "diff rms", "diff%");

    for (int mt = 0; mt < 3; mt++) {
        if (mt == 0) synth_pluck(in, n);
        else if (mt == 1) synth_pad(in, n);
        else synth_transient(in, n);

        for (int s = 0; s < 3; s++) {
            Render_t lin = {lin_l, lin_r, n};
            Render_t herm = {herm_l, herm_r, n};
            render(BUBBLE_QUALITY_PROFILE_MCU_SAFE, sparkles[s], in, n, &lin);
            int lin_mode = g_last_mode;
            unsigned long long lin_lc = g_last_linear, lin_hc = g_last_hermite;
            Metrics_t ml = analyse(&lin);
            render(BUBBLE_QUALITY_PROFILE_WEB_STANDARD, sparkles[s], in, n, &herm);
            int herm_mode = g_last_mode;
            unsigned long long herm_lc = g_last_linear, herm_hc = g_last_hermite;
            Metrics_t mh = analyse(&herm);

            double dr = diff_rms(&lin, &herm);
            double diff_pct = 100.0 * dr / fmax(1e-12, ml.rms);
            if (diff_pct > max_diff_pct) max_diff_pct = diff_pct;
            if (lin_mode != 0 || herm_mode != 1) mode_ok = 0;
            printf("  %-10s %-7s | %-8.4f %-8.4f | %-8.4f %-8.4f | %-9.1f | %-9.1f | %-8.6f | %-6.2f%%\n",
                   mat_names[mt], sparkle_names[s], ml.rms, mh.rms, ml.peak, mh.peak,
                   ml.centroid_hz, mh.centroid_hz, dr, diff_pct);
            printf("      A/B interpolation mode: linear-engine=%d Hermite-engine=%d | read-path calls lin=%llu/%llu herm=%llu/%llu\n",
                   lin_mode, herm_mode, lin_lc, lin_hc, herm_lc, herm_hc);

            check(ml.rms > 1e-4 && mh.rms > 1e-4, "A/B render is non-silent");
            check(isfinite(ml.rms) && isfinite(mh.rms), "A/B RMS finite");
            check(ml.peak <= 0.95 && mh.peak <= 0.95, "final limiter keeps the output under ceiling");
            check(isfinite(ml.correlation) && isfinite(mh.correlation), "A/B correlation finite");
            check(ml.limiter_gr_db < 24.0 && mh.limiter_gr_db < 24.0, "limiter gain reduction stays sane");
            // Hermite must not audibly darken the cloud beyond a small margin.
            check(mh.centroid_hz >= ml.centroid_hz * 0.95, "Hermite does not darken the spectral centroid");
            // The read path legitimately differs, so the difference must stay a
            // texture, not become a different cloud.
            check(diff_pct < 60.0, "Hermite difference stays a texture, not a different cloud");
            // HF retention: Hermite must not lose the top band.
            check(mh.high_ratio >= ml.high_ratio * 0.85 - 1e-9, "Hermite retains the high band");
        }
    }
    check(mode_ok, "A/B engines selected linear for MCU and Hermite for WEB");
    check(max_diff_pct > 0.0, "linear and Hermite renders actually differ (not bit parity)");
    printf("[hermite] A/B max difference: %.3f%% of RMS\n", max_diff_pct);

    if (g_failures != 0) {
        fprintf(stderr, "=== M3.2B musical A/B FAILED with %d failure(s) ===\n", g_failures);
        return 1;
    }
    printf("=== M3.2B musical A/B passed ===\n");
    return 0;
}
