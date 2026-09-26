// Native (no JUCE) behavioural probe for BubbleCloudEngineWrapper stereo contract.
// Validates: dry stays channel-local, spatial wet crosses channels, and the SPACE
// macro changes the stereo width. Compiled by tests/dsp/test_wrapper_stereo_contract.py.

#include "BubbleCloudEngineWrapper.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    struct StereoEnergy
    {
        double left = 0.0;
        double right = 0.0;
        double side = 0.0;
        double mid = 0.0;
    };

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

    StereoEnergy renderMusicalProbe(BubbleCloudEngineWrapper& wrapper, double sampleRate, double seconds)
    {
        constexpr int blockSize = 256;
        const int64_t total = (int64_t)std::ceil(sampleRate * seconds);
        std::vector<float> inLeft((size_t)blockSize, 0.0f);
        std::vector<float> inRight((size_t)blockSize, 0.0f);
        std::vector<float> outLeft((size_t)blockSize, 0.0f);
        std::vector<float> outRight((size_t)blockSize, 0.0f);

        StereoEnergy energy;
        for (int64_t offset = 0; offset < total; offset += blockSize) {
            const int frames = (int)std::min<int64_t>(blockSize, total - offset);
            for (int i = 0; i < frames; ++i) {
                const double time = (double)(offset + i) / sampleRate;
                const double phrase = std::fmod(time, 0.25) / 0.25;
                const double envelope = std::exp(-5.5 * phrase);
                inLeft[(size_t)i] = (float)(0.24 * envelope
                    * (std::sin(6.283185307179586 * 220.0 * time)
                       + 0.45 * std::sin(6.283185307179586 * 329.63 * time)));
                inRight[(size_t)i] = (float)(0.22 * envelope
                    * (std::sin(6.283185307179586 * 220.0 * time + 0.035)
                       + 0.45 * std::sin(6.283185307179586 * 277.18 * time)));
            }
            wrapper.process(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(), frames);
            for (int i = 0; i < frames; ++i) {
                const double l = (double)outLeft[(size_t)i];
                const double r = (double)outRight[(size_t)i];
                const double side = 0.5 * (l - r);
                const double mid = 0.5 * (l + r);
                energy.side += side * side;
                energy.mid += mid * mid;
            }
        }
        return energy;
    }
}

int main()
{
    constexpr double sampleRate = 48000.0;

    // --- Dry locality: MIX fully dry -> dry stays on its own channel. ---
    {
        BubbleCloudEngineWrapper wrapper;
        wrapper.prepare(sampleRate, 256);
        wrapper.setParameter(BUBBLE_PARAM_MIX, 0.0f);
        wrapper.setParameter(BUBBLE_PARAM_SPACE, 1.0f);
        renderConstantLeft(wrapper, 64, 0.0f); // settle macro smoothing
        const auto energy = renderConstantLeft(wrapper, 8, 0.5f);
        std::printf("dry-only left probe: L=%.6g R=%.6g\n", energy.left, energy.right);
        if (!(energy.left > 0.1 && energy.right < 1.0e-4)) {
            std::printf("FAIL: dry-only left probe must keep dry on the left channel only\n");
            return 1;
        }
    }

    // --- Spatial wet: MIX fully wet -> wet crosses to the right channel. ---
    {
        BubbleCloudEngineWrapper wrapper;
        wrapper.prepare(sampleRate, 256);
        wrapper.setParameter(BUBBLE_PARAM_MIX, 1.0f);
        wrapper.setParameter(BUBBLE_PARAM_SPACE, 1.0f);
        renderConstantLeft(wrapper, 64, 0.0f);
        const auto energy = renderConstantLeft(wrapper, 32, 0.5f);
        std::printf("wet left probe: L=%.6g R=%.6g\n", energy.left, energy.right);
        if (!(energy.right > 1.0e-3)) {
            std::printf("FAIL: spatial wet from an L-only input must reach the right channel\n");
            return 1;
        }
    }

    // --- SPACE macro changes stereo width. ---
    {
        double narrowWidth = 0.0;
        double wideWidth = 0.0;
        for (int pass = 0; pass < 2; ++pass) {
            BubbleCloudEngineWrapper wrapper;
            wrapper.prepare(sampleRate, 256);
            wrapper.setParameter(BUBBLE_PARAM_MIX, 1.0f);
            wrapper.setParameter(BUBBLE_PARAM_SPACE, pass == 0 ? 0.0f : 1.0f);
            renderMusicalProbe(wrapper, sampleRate, 0.2); // settle
            const auto energy = renderMusicalProbe(wrapper, sampleRate, 0.8);
            const double width = std::sqrt(energy.side) / std::sqrt(std::max(1.0e-18, energy.mid));
            if (pass == 0) narrowWidth = width; else wideWidth = width;
        }
        std::printf("STEREO WIDTH narrow(SPACE=0)=%.6g wide(SPACE=1)=%.6g\n", narrowWidth, wideWidth);
        if (!(wideWidth > narrowWidth * 1.08)) {
            std::printf("FAIL: SPACE=1 did not measurably widen the stereo field vs SPACE=0\n");
            return 1;
        }
    }

    std::printf("wrapper stereo probe passed\n");
    return 0;
}
