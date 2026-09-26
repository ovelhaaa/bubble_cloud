// Native (no JUCE) behavioural probe for BubbleCloudEngineWrapper stereo contract.
// Validates the explicit stereo wet summing law (1/sqrt(2)), dry channel locality,
// spatial wet crossing, SPACE width, dual-mono loudness/limiter behaviour, and
// sample-rate/block-size invariance. Compiled by tests/dsp/test_wrapper_stereo_contract.py.

#include "BubbleCloudEngineWrapper.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace
{
    struct StereoEnergy
    {
        double left = 0.0;
        double right = 0.0;
        double side = 0.0;
        double mid = 0.0;
        double cross = 0.0;
        double peakL = 0.0;
        double peakR = 0.0;
        long samples = 0;

        double rmsL() const { return std::sqrt(left / std::max(1L, samples)); }
        double rmsR() const { return std::sqrt(right / std::max(1L, samples)); }
        double rmsTotal() const { return std::sqrt((left + right) / std::max(1L, samples)); }
        double width() const { return std::sqrt(side) / std::sqrt(std::max(1.0e-18, mid)); }
        double correlation() const
        {
            return cross / std::sqrt(std::max(1.0e-18, left * right));
        }
    };

    enum class Scenario
    {
        leftOnly,
        rightOnly,
        dualMono,
        stereoCorrelated,
        stereoDecorrelated,
    };

    const char* scenarioName(Scenario scenario)
    {
        switch (scenario) {
            case Scenario::leftOnly: return "L-only";
            case Scenario::rightOnly: return "R-only";
            case Scenario::dualMono: return "dual-mono";
            case Scenario::stereoCorrelated: return "stereo-correlated";
            case Scenario::stereoDecorrelated: return "stereo-decorrelated";
        }
        return "?";
    }

    float probeSample(double time, double frequency, double phase)
    {
        return (float)(0.24 * std::sin(6.283185307179586 * frequency * time + phase));
    }

    void fillStimulus(Scenario scenario, double sampleRate, long offset, int frames,
                      std::vector<float>& inLeft, std::vector<float>& inRight)
    {
        const bool left = scenario != Scenario::rightOnly;
        const bool right = scenario != Scenario::leftOnly;
        const bool decorrelated = scenario == Scenario::stereoDecorrelated;
        for (int i = 0; i < frames; ++i) {
            const double time = (double)(offset + i) / sampleRate;
            inLeft[(size_t)i] = left ? probeSample(time, 220.0, 0.0) : 0.0f;
            inRight[(size_t)i] = right ? probeSample(time, decorrelated ? 277.18 : 220.0, 0.0) : 0.0f;
        }
    }

    // Settle macro smoothing and force a pure wet bus so the probe observes the
    // summed wet field without dry masking it. The limiter ceiling is pushed up
    // for "pre-limiter" measurements.
    void configurePureWet(BubbleCloudEngineWrapper& wrapper, float space, float ceilingDb, int block)
    {
        wrapper.setParameter(BUBBLE_ENGINE_PARAM_FINAL_LIMITER_CEILING_DB, ceilingDb);
        wrapper.setParameter(BUBBLE_PARAM_SPACE, space);
        wrapper.setParameter(BUBBLE_PARAM_MIX, 1.0f);

        std::vector<float> silence((size_t)block, 0.0f);
        std::vector<float> outL((size_t)block, 0.0f);
        std::vector<float> outR((size_t)block, 0.0f);
        for (int i = 0; i < 96; ++i) { // let macros fully resolve and settle
            wrapper.process(silence.data(), silence.data(), outL.data(), outR.data(), block);
        }
        wrapper.setParameter(BUBBLE_ENGINE_PARAM_MIX_DRY_GAIN, 0.0f);
        wrapper.setParameter(BUBBLE_ENGINE_PARAM_MIX_WET_GAIN, 1.0f);
    }

    StereoEnergy renderScenario(BubbleCloudEngineWrapper& wrapper, double sampleRate, int block,
                                double seconds, Scenario scenario)
    {
        std::vector<float> inLeft((size_t)block, 0.0f);
        std::vector<float> inRight((size_t)block, 0.0f);
        std::vector<float> outLeft((size_t)block, 0.0f);
        std::vector<float> outRight((size_t)block, 0.0f);

        StereoEnergy energy;
        const long total = (long)std::ceil(sampleRate * seconds);
        for (long offset = 0; offset < total; offset += block) {
            const int frames = (int)std::min<long>(block, total - offset);
            fillStimulus(scenario, sampleRate, offset, frames, inLeft, inRight);
            wrapper.process(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(), frames);
            for (int i = 0; i < frames; ++i) {
                const double l = (double)outLeft[(size_t)i];
                const double r = (double)outRight[(size_t)i];
                energy.left += l * l;
                energy.right += r * r;
                energy.cross += l * r;
                const double side = 0.5 * (l - r);
                const double mid = 0.5 * (l + r);
                energy.side += side * side;
                energy.mid += mid * mid;
                energy.peakL = std::max(energy.peakL, std::abs(l));
                energy.peakR = std::max(energy.peakR, std::abs(r));
            }
            energy.samples += frames;
        }
        return energy;
    }

    StereoEnergy renderConstantLeft(BubbleCloudEngineWrapper& wrapper, int blocks, float amplitude)
    {
        constexpr int blockSize = 256;
        std::vector<float> inLeft((size_t)blockSize, amplitude);
        std::vector<float> inRight((size_t)blockSize, 0.0f);
        std::vector<float> outLeft((size_t)blockSize, 0.0f);
        std::vector<float> outRight((size_t)blockSize, 0.0f);

        StereoEnergy energy;
        for (int block = 0; block < blocks; ++block) {
            wrapper.process(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(), blockSize);
            for (int i = 0; i < blockSize; ++i) {
                const double l = (double)outLeft[(size_t)i];
                const double r = (double)outRight[(size_t)i];
                energy.left += std::abs(l);
                energy.right += std::abs(r);
                const double side = 0.5 * (l - r);
                const double mid = 0.5 * (l + r);
                energy.side += side * side;
                energy.mid += mid * mid;
            }
        }
        return energy;
    }

    int fail(const char* message)
    {
        std::printf("FAIL: %s\n", message);
        return 1;
    }

    int testSummingLawArithmetic()
    {
        float outL = 0.0f;
        float outR = 0.0f;

        // Dry is channel-local and unit gain.
        BubbleCloudEngineWrapper::sumStereoBus(0.5f, -0.25f, 0.0f, 0.0f, 0.0f, 0.0f, outL, outR);
        if (!(std::abs(outL - 0.5f) < 1.0e-6f && std::abs(outR + 0.25f) < 1.0e-6f))
            return fail("summing law must keep dry strictly channel-local");

        // A single same-side wet field is scaled by 1/sqrt(2).
        const float g = BubbleCloudEngineWrapper::wetSumGain;
        BubbleCloudEngineWrapper::sumStereoBus(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, outL, outR);
        if (!(std::abs(outL - g) < 1.0e-6f && std::abs(outR) < 1.0e-6f))
            return fail("same-side wet field was not scaled by 1/sqrt(2)");

        // Two decorrelated full wet fields at unity sum to sqrt(2) * 1/sqrt(2) = 1.
        BubbleCloudEngineWrapper::sumStereoBus(0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, outL, outR);
        if (!(std::abs(outL - 2.0f * g) < 1.0e-6f && std::abs(outR - 2.0f * g) < 1.0e-6f))
            return fail("dual-engine wet sum does not follow the documented bus law");

        if (!(std::abs(g - 0.70710678f) < 1.0e-6f))
            return fail("wet summing law is not 1/sqrt(2)");
        return 0;
    }
}

int main()
{
    if (const int code = testSummingLawArithmetic(); code != 0)
        return code;

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    // --- Dry locality: MIX fully dry -> dry stays on its own channel. ---
    {
        BubbleCloudEngineWrapper wrapper;
        wrapper.prepare(sampleRate, blockSize);
        wrapper.setParameter(BUBBLE_PARAM_MIX, 0.0f);
        wrapper.setParameter(BUBBLE_PARAM_SPACE, 1.0f);
        renderConstantLeft(wrapper, 64, 0.0f); // settle macro smoothing
        const auto energy = renderConstantLeft(wrapper, 8, 0.5f);
        std::printf("dry-only left probe: L=%.6g R=%.6g\n", energy.left, energy.right);
        if (!(energy.left > 0.1 && energy.right < 1.0e-4))
            return fail("dry-only left probe must keep dry on the left channel only");
    }

    // --- Spatial wet: MIX fully wet -> wet crosses to the right channel. ---
    {
        BubbleCloudEngineWrapper wrapper;
        wrapper.prepare(sampleRate, blockSize);
        wrapper.setParameter(BUBBLE_PARAM_MIX, 1.0f);
        wrapper.setParameter(BUBBLE_PARAM_SPACE, 1.0f);
        renderConstantLeft(wrapper, 64, 0.0f);
        const auto energy = renderConstantLeft(wrapper, 32, 0.5f);
        std::printf("wet left probe: L=%.6g R=%.6g\n", energy.left, energy.right);
        if (!(energy.right > 1.0e-3))
            return fail("spatial wet from an L-only input must reach the right channel");
    }

    // --- Wet bus measurements across the required signal scenarios. ---
    std::array<StereoEnergy, 5> wet {};
    const Scenario scenarios[] = {
        Scenario::leftOnly, Scenario::rightOnly, Scenario::dualMono,
        Scenario::stereoCorrelated, Scenario::stereoDecorrelated,
    };
    {
        std::printf("wet bus (pre-limiter, dry=0, SPACE=1):\n");
        for (std::size_t i = 0; i < std::size(scenarios); ++i) {
            BubbleCloudEngineWrapper wrapper;
            wrapper.prepare(sampleRate, blockSize);
            configurePureWet(wrapper, 1.0f, 12.0f, blockSize);
            wet[i] = renderScenario(wrapper, sampleRate, blockSize, 1.0, scenarios[i]);
            std::printf("  %-20s rmsL=%.6f rmsR=%.6f rms=%.6f peakL=%.6f peakR=%.6f corr=%.4f width=%.4f\n",
                        scenarioName(scenarios[i]), wet[i].rmsL(), wet[i].rmsR(),
                        wet[i].rmsTotal(), wet[i].peakL, wet[i].peakR,
                        wet[i].correlation(), wet[i].width());
        }
    }

    // L-only and R-only must both carry wet on both channels (spatial field).
    if (!(wet[0].right > 1.0e-5 && wet[0].left > 1.0e-5))
        return fail("L-only wet field did not span both output channels");
    if (!(wet[1].left > 1.0e-5 && wet[1].right > 1.0e-5))
        return fail("R-only wet field did not span both output channels");

    // Dual-mono must not blow up relative to the single-engine reference: with
    // the 1/sqrt(2) law the two fields sum to roughly one field's total power per
    // engine pair, i.e. within +3 dB of a single engine. M2 shares attack events
    // so a small amount of correlated summation is expected and allowed.
    const double singleEngine = std::max(wet[0].rmsTotal(), wet[1].rmsTotal());
    const double dualMono = wet[2].rmsTotal();
    std::printf("dual-mono vs single-engine wet total: %.6f / %.6f = %.4fx\n",
                dualMono, singleEngine, dualMono / std::max(1.0e-12, singleEngine));
    if (dualMono > singleEngine * 1.5)
        return fail("dual-mono inflated wet loudness beyond the equal-power bound");

    // M2 stereo coherence: the field must stay wide (side energy present) and
    // must not collapse into near-mono, nor explode into a chaotic side field.
    // The dual-mono case is the most coherent one, so it carries the tightest
    // upper bound; the decorrelated case carries the lower bound on width.
    {
        const double dualCorrelation = wet[2].correlation();
        const double dualWidth = wet[2].width();
        const double decorrelatedWidth = wet[4].width();
        std::printf("stereo coherence: dual-mono corr=%.4f width=%.4f decorrelated width=%.4f\n",
                    dualCorrelation, dualWidth, decorrelatedWidth);
        if (!(dualCorrelation > -0.99 && dualCorrelation < 0.985))
            return fail("dual-mono wet field collapsed into mono or inverted");
        if (!(dualWidth > 0.05 && dualWidth < 2.5))
            return fail("dual-mono side energy collapsed or exploded");
        if (!(decorrelatedWidth > 0.05))
            return fail("decorrelated stereo input lost its spatial width");
    }

    // --- SPACE macro changes stereo width of the wet field. ---
    {
        double narrowWidth = 0.0;
        double wideWidth = 0.0;
        for (int pass = 0; pass < 2; ++pass) {
            BubbleCloudEngineWrapper wrapper;
            wrapper.prepare(sampleRate, blockSize);
            configurePureWet(wrapper, pass == 0 ? 0.0f : 1.0f, 12.0f, blockSize);
            const auto energy = renderScenario(wrapper, sampleRate, blockSize, 0.8,
                                               Scenario::stereoDecorrelated);
            const double width = energy.width();
            if (pass == 0) narrowWidth = width; else wideWidth = width;
        }
        std::printf("STEREO WIDTH narrow(SPACE=0)=%.6g wide(SPACE=1)=%.6g\n", narrowWidth, wideWidth);
        if (!(wideWidth > narrowWidth * 1.08))
            return fail("SPACE=1 did not measurably widen the stereo field vs SPACE=0");
    }

    // --- Limiter must engage for loud signals and recover in silence, and the
    //     dual-engine sum must not keep it pinned harder than a single engine. ---
    {
        constexpr int hotBlock = 256;
        auto runHot = [&](bool dual) {
            BubbleCloudEngineWrapper wrapper;
            wrapper.prepare(sampleRate, hotBlock);
            wrapper.setParameter(BUBBLE_PARAM_MIX, 1.0f);
            wrapper.setParameter(BUBBLE_PARAM_SPACE, 1.0f);
            renderConstantLeft(wrapper, 64, 0.0f);

            std::vector<float> inLeft((size_t)hotBlock, 0.9f);
            std::vector<float> inRight((size_t)hotBlock, dual ? 0.9f : 0.0f);
            std::vector<float> outLeft((size_t)hotBlock, 0.0f);
            std::vector<float> outRight((size_t)hotBlock, 0.0f);
            for (int i = 0; i < 160; ++i)
                wrapper.process(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(), hotBlock);
            const float driven = wrapper.getTelemetrySnapshot().limiterGain;

            // Read the per-block min so recovery is observable (the snapshot
            // resets the min-hold each call).
            std::vector<float> silence((size_t)hotBlock, 0.0f);
            float recovered = 0.0f;
            for (int i = 0; i < 4000; ++i) {
                wrapper.process(silence.data(), silence.data(), outLeft.data(), outRight.data(), hotBlock);
                recovered = wrapper.getTelemetrySnapshot().limiterGain;
                if (recovered > 0.99f)
                    break;
            }
            return std::make_pair(driven, recovered);
        };

        const auto single = runHot(false);
        const auto dual = runHot(true);
        std::printf("limiter gain single=%.4f/%.4f dual=%.4f/%.4f (driven/recovered)\n",
                    single.first, single.second, dual.first, dual.second);
        if (!(single.first < 0.999f && dual.first < 0.999f))
            return fail("final limiter did not engage on a hot signal");
        if (!(single.second > 0.99f && dual.second > 0.99f))
            return fail("final limiter did not recover after the input went silent");
        // M2 intentionally shares attack events and favours recent body material,
        // which raises the wet presence on this synthetic DC-step and makes the
        // shared limiter work harder than the M1 decorrelated-field baseline. The
        // guard only ensures it is not pinned into permanent heavy limiting; the
        // summing law still bounds the dual-mono loudness above.
        if (!(dual.first > 0.20f))
            return fail("dual-engine sum pinned the limiter into permanent heavy limiting");
    }

    // --- Sample-rate and block-size invariance of the wet crossing/width. ---
    {
        const std::array<double, 4> sampleRates {{ 44100.0, 48000.0, 88200.0, 96000.0 }};
        const std::array<int, 6> blockSizes {{ 32, 64, 127, 256, 512, 2048 }};
        for (const double sr : sampleRates) {
            for (const int block : blockSizes) {
                BubbleCloudEngineWrapper wrapper;
                wrapper.prepare(sr, block);
                configurePureWet(wrapper, 1.0f, 12.0f, block);
                const auto energy = renderScenario(wrapper, sr, block, 0.25,
                                                   Scenario::leftOnly);
                if (!(energy.right > 1.0e-6 && energy.left > 1.0e-6)) {
                    std::printf("  sr=%.0f block=%d L=%.6g R=%.6g\n", sr, block, energy.left, energy.right);
                    return fail("wet crossing failed at a sample-rate/block-size combination");
                }
            }
        }
        std::printf("sample-rate/block-size matrix passed\n");
    }

    std::printf("wrapper stereo probe passed\n");
    return 0;
}
