// M2 comparative validation probe.
//
// Renders short deterministic stereo scenarios through the real dual-engine
// wrapper and prints one CSV row per scenario with the basic musical metrics:
// RMS, peak, stereo correlation, side/mid ratio, max active voices and minimum
// final-limiter gain. Compiled twice (M1 baseline sources and M2 sources) by
// scripts/m2_validation.py to produce before/after reports.
//
// Usage: m2_validation_probe <mono|stereo>   (stdout is CSV)

#include "BubbleCloudEngineWrapper.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    constexpr double seconds = 1.6;

    double pluckEnvelope(double localTime, double decay)
    {
        return localTime < 0.0 ? 0.0 : std::exp(-localTime / decay);
    }

    double phrase(double time, double frequency, double phase, double detune)
    {
        double value = 0.0;
        const double onsets[3] = {0.0, 0.45, 0.95};
        const double freq[3] = {196.0, 246.94, 293.66};
        for (int note = 0; note < 3; ++note) {
            const double local = time - onsets[note];
            const double env = pluckEnvelope(local, 0.42);
            if (env <= 0.0) continue;
            const double f = freq[note] * (1.0 + detune);
            value += env * (0.55 * std::sin(6.283185307179586 * f * (time + phase))
                          + 0.22 * std::sin(6.283185307179586 * 2.0 * f * (time + phase))
                          + 0.10 * std::sin(6.283185307179586 * 3.0 * f * (time + phase)));
        }
        return 0.33 * value;
    }

    struct Scenario
    {
        const char* name;
        float memory;
        float sparkle;
        float motion;
        float freeze;
        float space;
    };

    struct Metrics
    {
        double rms = 0.0;
        double peak = 0.0;
        double correlation = 0.0;
        double sideMid = 0.0;
        int activeVoices = 0;
        double limiterGain = 1.0;
    };

    void setMacros(BubbleCloudEngineWrapper& wrapper, const Scenario& scenario, float freeze)
    {
        wrapper.setParameter(BUBBLE_PARAM_DENSITY, 0.62f);
        wrapper.setParameter(BUBBLE_PARAM_BLOOM, 0.5f);
        wrapper.setParameter(BUBBLE_PARAM_MOTION, scenario.motion);
        wrapper.setParameter(BUBBLE_PARAM_TEXTURE, 0.5f);
        wrapper.setParameter(BUBBLE_PARAM_SPACE, scenario.space);
        wrapper.setParameter(BUBBLE_PARAM_GRAVITY, 0.5f);
        wrapper.setParameter(BUBBLE_PARAM_MEMORY, scenario.memory);
        wrapper.setParameter(BUBBLE_PARAM_CLARITY, 0.5f);
        wrapper.setParameter(BUBBLE_PARAM_FREEZE, freeze);
        wrapper.setParameter(BUBBLE_PARAM_SPARKLE, scenario.sparkle);
        wrapper.setParameter(BUBBLE_PARAM_WARMTH, 0.5f);
        wrapper.setParameter(BUBBLE_PARAM_MIX, 0.55f);
    }

    Metrics renderScenario(bool stereoInput, const Scenario& scenario)
    {
        BubbleCloudEngineWrapper wrapper;
        wrapper.prepare(sampleRate, blockSize);
        // Freeze is a performance gesture: engage it halfway through so the
        // granular memory has real content to lock instead of silence.
        setMacros(wrapper, scenario, 0.0f);

        std::vector<float> inLeft((size_t)blockSize, 0.0f);
        std::vector<float> inRight((size_t)blockSize, 0.0f);
        std::vector<float> outLeft((size_t)blockSize, 0.0f);
        std::vector<float> outRight((size_t)blockSize, 0.0f);

        Metrics metrics;
        double sumL = 0.0;
        double sumR = 0.0;
        double sumCross = 0.0;
        double sumSide = 0.0;
        double sumMid = 0.0;
        long total = 0;
        const long frames = (long)(sampleRate * seconds);

        // Prime macro smoothing with a short silence pass.
        for (int i = 0; i < 64; ++i)
            wrapper.process(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(), blockSize);

        const long freezeAt = (long)(sampleRate * 0.9);
        for (long offset = 0; offset < frames; offset += blockSize)
        {
            if (scenario.freeze > 0.5f && offset >= freezeAt && offset - blockSize < freezeAt)
                wrapper.setParameter(BUBBLE_PARAM_FREEZE, scenario.freeze);
            const int framesThisBlock = (int)std::min<long>(blockSize, frames - offset);
            for (int i = 0; i < framesThisBlock; ++i)
            {
                const double t = (double)(offset + i) / sampleRate;
                const double base = phrase(t, 1.0, 0.0, 0.0);
                inLeft[(size_t)i] = (float)base;
                inRight[(size_t)i] = (float)(stereoInput ? phrase(t, 1.0, 0.0025, 0.001) : base);
            }
            wrapper.process(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(), framesThisBlock);

            for (int i = 0; i < framesThisBlock; ++i)
            {
                const double l = (double)outLeft[(size_t)i];
                const double r = (double)outRight[(size_t)i];
                sumL += l * l;
                sumR += r * r;
                sumCross += l * r;
                const double side = 0.5 * (l - r);
                const double mid = 0.5 * (l + r);
                sumSide += side * side;
                sumMid += mid * mid;
                metrics.peak = std::max(metrics.peak, std::max(std::abs(l), std::abs(r)));
            }
            total += framesThisBlock;

            const auto snapshot = wrapper.getTelemetrySnapshot();
            metrics.activeVoices = std::max(metrics.activeVoices, snapshot.activeVoices);
            metrics.limiterGain = std::min(metrics.limiterGain, (double)snapshot.limiterGain);
        }

        const double denom = std::max(1L, total);
        metrics.rms = std::sqrt((sumL + sumR) / (2.0 * denom));
        metrics.correlation = sumCross / std::sqrt(std::max(1.0e-18, sumL * sumR));
        metrics.sideMid = std::sqrt(sumSide) / std::sqrt(std::max(1.0e-18, sumMid));
        return metrics;
    }
}

int main(int argc, char** argv)
{
    const bool stereoInput = argc > 1 && std::strcmp(argv[1], "stereo") == 0;
    const std::array<Scenario, 8> scenarios {{
        { "m2_defaults",    0.5f, 0.4f, 0.4f, 0.0f, 0.7f },
        { "memory_low",     0.0f, 0.4f, 0.4f, 0.0f, 0.7f },
        { "memory_high",    1.0f, 0.4f, 0.4f, 0.0f, 0.7f },
        { "sparkle_low",    0.5f, 0.0f, 0.4f, 0.0f, 0.7f },
        { "sparkle_high",   0.5f, 1.0f, 0.4f, 0.0f, 0.7f },
        { "motion_low",     0.5f, 0.4f, 0.0f, 0.0f, 0.7f },
        { "motion_high",    0.5f, 0.4f, 1.0f, 0.0f, 0.7f },
        { "freeze",         0.5f, 0.4f, 0.4f, 1.0f, 0.7f },
    }};

    std::printf("scenario,input,rms,peak,correlation,side_mid,active_voices,limiter_gain\n");
    for (const Scenario& scenario : scenarios)
    {
        const Metrics metrics = renderScenario(stereoInput, scenario);
        std::printf("%s,%s,%.6f,%.6f,%.6f,%.6f,%d,%.6f\n",
                    scenario.name,
                    stereoInput ? "stereo" : "mono",
                    metrics.rms,
                    metrics.peak,
                    metrics.correlation,
                    metrics.sideMid,
                    metrics.activeVoices,
                    metrics.limiterGain);
    }
    return 0;
}
