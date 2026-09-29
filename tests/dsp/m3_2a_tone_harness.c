// M3.2A "Tonal Bus Rebalance" objective harness.
//
// Measures the two global tonal buses separately, before and after their filters:
//   attack bus:  raw bus (0 dB reference) -> 1-pole HPF
//   sustain bus: raw bus (0 dB reference) -> 1-pole LPF
//
// The harness reads the actual coefficients configured on a live engine instance
// (engine.attack_hpf_l / engine.sustain_lpf_l) and applies the exact same 1-pole
// difference equations used by core/dsp/sound_bubbles_dsp.c:
//
//   LPF: y[n] = b0 * x[n] + a1 * y[n-1]
//   HPF: y[n] = x[n] - LPF(x[n])
//
// Covered:
//   1. per-frequency bus gain table (100/220/440/1k/2k/4k/6k/10k Hz)
//   2. band-energy continuity between attack and sustain buses
//   3. state/warmth modulation of the dynamic sustain LPF cutoff
//   4. control-rate smoothing bound (no abrupt cutoff step / zipper)
//   5. sample-rate invariance of the rebalanced bus filters

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "engine/bubble_engine.h"

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        return 1; \
    } \
} while (0)

#define SINE_X 0.5
#define PI_F 3.14159265358979323846f

static const double kTestFreqs[] = {100.0, 220.0, 440.0, 1000.0, 2000.0, 4000.0, 6000.0, 10000.0};
#define TEST_FREQ_COUNT ((int)(sizeof(kTestFreqs) / sizeof(kTestFreqs[0])))

static void init_memory(int16_t* delay, int samples) {
    memset(delay, 0, (size_t)samples * sizeof(delay[0]));
}

// Simulates a unit sine through one bus filter and returns the steady-state gain
// in dB. `is_hpf` selects x - LPF(x); otherwise y = LPF(x).
static double measure_bus_gain_db(const Filter1Pole_t* filter, int is_hpf, double freq_hz, double sample_rate) {
    const int total = 8192;
    const int discard = 4096;
    const double w = 2.0 * 3.14159265358979323846 * freq_hz / sample_rate;
    double b0 = (double)filter->b0;
    double a1 = (double)filter->a1;
    double z1 = 0.0;
    double sum_in = 0.0;
    double sum_out = 0.0;
    for (int n = 0; n < total; n++) {
        double x = (double)SINE_X * sin(w * (double)n);
        z1 = b0 * x + a1 * z1;
        double y = is_hpf ? (x - z1) : z1;
        if (n >= discard) {
            sum_in += x * x;
            sum_out += y * y;
        }
    }
    double in_rms = sqrt(sum_in / (double)(total - discard));
    double out_rms = sqrt(sum_out / (double)(total - discard));
    if (in_rms <= 1.0e-12) return -120.0;
    if (out_rms <= 1.0e-12) return -120.0;
    return 20.0 * log10(out_rms / in_rms);
}

static int configure_sustain(EngineConfig_t* cfg, float sample_rate) {
    bubble_engine_default_config(cfg);
    cfg->sample_rate = sample_rate;
    cfg->rng_seed = 0x32A32Au;
    cfg->active_voice_limit = 32;
    return 0;
}

// ---------------------------------------------------------------------------
// 1 + 2: per-frequency bus gain and band continuity
// ---------------------------------------------------------------------------
static int test_bus_gain_table_and_band_continuity(void) {
    static int16_t delay[192000];
    init_memory(delay, 192000);
    EngineConfig_t cfg;
    configure_sustain(&cfg, 48000.0f);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);

    double attack_db[TEST_FREQ_COUNT];
    double sustain_db[TEST_FREQ_COUNT];
    printf("[M3.2A] Bus filter response (engine coefficients @ %.0f Hz):\n", (double)cfg.sample_rate);
    printf("  %-8s | %-22s | %-22s\n", "Hz", "attack HPF (after)", "sustain LPF (after)");
    printf("  ---------------------------------------------------------------\n");
    for (int i = 0; i < TEST_FREQ_COUNT; i++) {
        attack_db[i] = measure_bus_gain_db(&engine.attack_hpf_l, 1, kTestFreqs[i], (double)cfg.sample_rate);
        sustain_db[i] = measure_bus_gain_db(&engine.sustain_lpf_l, 0, kTestFreqs[i], (double)cfg.sample_rate);
        printf("  %-8.0f | %-22.2f | %-22.2f\n", kTestFreqs[i], attack_db[i], sustain_db[i]);
    }

    double attack_100 = attack_db[0];
    double attack_220 = attack_db[1];
    double attack_440 = attack_db[2];
    double sustain_2k = sustain_db[4];
    double sustain_4k = sustain_db[5];
    double sustain_6k = sustain_db[6];
    double sustain_10k = sustain_db[7];

    // Attack only removes rumble: fundamental and low-mid must survive.
    CHECK(attack_100 <= -6.0, "attack HPF removes sub-100 Hz rumble/DC");
    CHECK(attack_220 >= -6.0, "attack bus keeps useful 220 Hz body (was lost at 1500 Hz HPF)");
    CHECK(attack_440 >= -3.0, "attack bus keeps 440 Hz low-mid body");
    CHECK(attack_440 > attack_100 + 4.0, "attack HPF still rolls off the low end below the body");

    // Sustain keeps body and presence, then rolls off above presence.
    CHECK(sustain_2k >= -1.5, "sustain bus keeps 2 kHz presence");
    CHECK(sustain_4k >= -3.5, "sustain bus keeps relevant 4 kHz content");
    CHECK(sustain_2k > sustain_4k && sustain_4k > sustain_6k && sustain_6k > sustain_10k,
          "sustain LPF falls gradually above the presence region");
    CHECK(sustain_10k <= -4.0, "sustain bus is clearly darker than the attack at 10 kHz");
    CHECK(sustain_10k < attack_db[7] - 1.0, "sustain tail is softer than the attack in the top band");

    // Band-energy continuity table (linear power sum of the band test tones).
    struct { const char* name; int first; int last; } bands[] = {
        { "80-250 Hz",  0, 1 },
        { "250-800 Hz", 2, 2 },
        { "800-2k Hz",  3, 3 },
        { "2-5k Hz",    4, 5 },
        { "5-10k Hz",   6, 7 },
    };
    printf("[M3.2A] Band gain (mean of band test tones, dB):\n");
    printf("  %-12s | %-10s | %-10s\n", "Band", "attack", "sustain");
    printf("  ---------------------------------------\n");
    for (size_t b = 0; b < sizeof(bands) / sizeof(bands[0]); b++) {
        double a = 0.0, s = 0.0;
        int count = bands[b].last - bands[b].first + 1;
        for (int i = bands[b].first; i <= bands[b].last; i++) {
            a += attack_db[i];
            s += sustain_db[i];
        }
        a /= (double)count;
        s /= (double)count;
        printf("  %-12s | %-10.2f | %-10.2f\n", bands[b].name, a, s);
        if (strcmp(bands[b].name, "800-2k Hz") == 0 || strcmp(bands[b].name, "2-5k Hz") == 0) {
            CHECK(a >= -3.0, "attack and sustain share the 800 Hz-5 kHz body/presence region");
            CHECK(s >= -3.5, "sustain keeps the 800 Hz-5 kHz body/presence region");
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 3: dynamic sustain LPF cutoff (state + warmth)
// ---------------------------------------------------------------------------
static double render_applied_cutoff(BubbleEngine_t* engine, float freq, int blocks) {
    float in[BUBBLES_BLOCK_SIZE];
    float out_l[BUBBLES_BLOCK_SIZE];
    float out_r[BUBBLES_BLOCK_SIZE];
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            int n = b * BUBBLES_BLOCK_SIZE + i;
            in[i] = 0.5f * sinf(2.0f * PI_F * freq * (float)n / engine->config.sample_rate);
        }
        bubble_engine_process(engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
    }
    return (double)engine->sustain_lpf_applied_hz;
}

static int test_dynamic_sustain_cutoff(void) {
    static int16_t delay[192000];
    init_memory(delay, 192000);
    EngineConfig_t cfg;
    configure_sustain(&cfg, 48000.0f);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);

    // Settle into SUSTAIN_BODY with a steady tone.
    double cutoff_sustain = render_applied_cutoff(&engine, 300.0f, 400);
    CHECK(cutoff_sustain >= 3500.0 && cutoff_sustain <= 7000.0,
          "dynamic sustain cutoff stays inside the safe 3.5-7 kHz range");
    CHECK(fabs(cutoff_sustain - 5000.0) > 1.0, "sustain cutoff is actually modulated, not fixed at the base");

    // WARMTH low should be more open than WARMTH high.
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_WARMTH, 0.0f);
    double cutoff_cool = render_applied_cutoff(&engine, 300.0f, 400);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_WARMTH, 1.0f);
    double cutoff_warm = render_applied_cutoff(&engine, 300.0f, 400);
    printf("[M3.2A] Dynamic sustain cutoff: sustain=%.0f Hz cool=%.0f Hz warm=%.0f Hz\n",
           cutoff_sustain, cutoff_cool, cutoff_warm);
    CHECK(cutoff_warm < cutoff_cool - 30.0, "higher WARMTH darkens the sustain bus cutoff");

    // Restore neutral and verify the cutoff stays bounded.
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_WARMTH, 0.5f);
    double cutoff_neutral = render_applied_cutoff(&engine, 300.0f, 400);
    CHECK(cutoff_neutral >= 3500.0 && cutoff_neutral <= 7000.0,
          "sustain cutoff remains bounded after automation");
    return 0;
}

// ---------------------------------------------------------------------------
// 4: control-rate smoothing bound (no abrupt step / zipper)
// ---------------------------------------------------------------------------
static int test_cutoff_smoothing_is_bounded(void) {
    static int16_t delay[192000];
    init_memory(delay, 192000);
    EngineConfig_t cfg;
    configure_sustain(&cfg, 48000.0f);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);

    float in[BUBBLES_BLOCK_SIZE];
    float out_l[BUBBLES_BLOCK_SIZE];
    float out_r[BUBBLES_BLOCK_SIZE];

    // Quiet settle, then a hard transient that forces the phrase state open.
    for (int b = 0; b < 200; b++) {
        memset(in, 0, sizeof(in));
        bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
    }
    double prev = (double)engine.sustain_lpf_applied_hz;
    double max_step = 0.0;
    float peak = 0.0f;
    for (int b = 0; b < 200; b++) {
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            in[i] = 0.9f * sinf(2.0f * PI_F * 440.0f * (float)(b * BUBBLES_BLOCK_SIZE + i) / cfg.sample_rate);
            if (i == 0) in[i] = 0.95f;
        }
        bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            if (!isfinite(out_l[i]) || !isfinite(out_r[i])) {
                fprintf(stderr, "FAIL %s:%d: non-finite output during tone sweep\n", __FILE__, __LINE__);
                return 1;
            }
            float a = fabsf(out_l[i]);
            if (a > peak) peak = a;
        }
        double now = (double)engine.sustain_lpf_applied_hz;
        double step = fabs(now - prev);
        if (step > max_step) max_step = step;
        prev = now;
    }
    printf("[M3.2A] Cutoff smoothing: max per-tick step=%.1f Hz, output peak=%.3f\n", max_step, (double)peak);
    CHECK(max_step <= 300.0, "control-rate smoothing bounds the per-tick cutoff step (no abrupt jump)");
    CHECK(peak <= 1.05f, "output stays bounded during the tone transition");
    return 0;
}

// ---------------------------------------------------------------------------
// 5: sample-rate invariance of the rebalanced bus filters
// ---------------------------------------------------------------------------
static int test_sample_rate_invariance(void) {
    const float rates[4] = {44100.0f, 48000.0f, 88200.0f, 96000.0f};
    static int16_t delay[192000];
    for (int r = 0; r < 4; r++) {
        init_memory(delay, 192000);
        EngineConfig_t cfg;
        configure_sustain(&cfg, rates[r]);
        BubbleEngine_t engine;
        bubble_engine_init(&engine, delay, &cfg);

        double attack_220 = measure_bus_gain_db(&engine.attack_hpf_l, 1, 220.0, (double)rates[r]);
        double attack_440 = measure_bus_gain_db(&engine.attack_hpf_l, 1, 440.0, (double)rates[r]);
        double attack_100 = measure_bus_gain_db(&engine.attack_hpf_l, 1, 100.0, (double)rates[r]);
        double sustain_2k = measure_bus_gain_db(&engine.sustain_lpf_l, 0, 2000.0, (double)rates[r]);
        double sustain_4k = measure_bus_gain_db(&engine.sustain_lpf_l, 0, 4000.0, (double)rates[r]);
        double sustain_10k = measure_bus_gain_db(&engine.sustain_lpf_l, 0, 10000.0, (double)rates[r]);

        CHECK(attack_100 <= -6.0, "attack rumble removal holds across sample rates");
        CHECK(attack_220 >= -6.0, "attack 220 Hz survives across sample rates");
        CHECK(attack_440 >= -3.0, "attack 440 Hz survives across sample rates");
        CHECK(sustain_2k >= -1.5, "sustain 2 kHz presence holds across sample rates");
        CHECK(sustain_4k >= -3.5, "sustain 4 kHz content holds across sample rates");
        CHECK(sustain_10k <= -4.0, "sustain top-band rolloff holds across sample rates");
        printf("[M3.2A] SR %.0f Hz: attack 220=%.2f 440=%.2f | sustain 2k=%.2f 4k=%.2f 10k=%.2f (dB)\n",
               (double)rates[r], attack_220, attack_440, sustain_2k, sustain_4k, sustain_10k);
    }
    return 0;
}

int main(void) {
    printf("=== SoundBubbles M3.2A Tonal Bus Rebalance Harness ===\n");
    if (test_bus_gain_table_and_band_continuity() != 0) return 1;
    if (test_dynamic_sustain_cutoff() != 0) return 1;
    if (test_cutoff_smoothing_is_bounded() != 0) return 1;
    if (test_sample_rate_invariance() != 0) return 1;
    printf("=== All M3.2A Tone Tests Passed Successfully ===\n");
    return 0;
}
