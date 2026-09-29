#pragma once

#include <array>
#include <atomic>
#include <vector>

extern "C" {
#include "bubble_engine.h"
}

struct BubbleCloudVoiceTelemetry
{
    bool active = false;
    float phase = 0.0f;
    float pan = 0.0f;
    float gain = 0.0f;
    float pitchRate = 1.0f;
    int bubbleClass = 0;
    bool reverse = false;
    int channel = 0;
};

struct BubbleCloudTelemetry
{
    static constexpr int maxVoices = BUBBLES_MAX_VOICES * 2;
    std::array<BubbleCloudVoiceTelemetry, maxVoices> voices {};
    int activeVoices = 0;
    int activeVoiceLimit = BUBBLES_MAX_VOICES * 2;
    int spawnCount = 0;
    int engineState = 0;
    int rhythmStep = -1;
    float envelope = 0.0f;
    float peakLeft = 0.0f;
    float peakRight = 0.0f;
    float limiterGain = 1.0f;
    int clipCount = 0;
    bool tempoSync = false;
    bool frozen = false;
};

class BubbleCloudEngineWrapper
{
public:
    BubbleCloudEngineWrapper();
    ~BubbleCloudEngineWrapper();

    void prepare(double sampleRate, int samplesPerBlock);
    void process(const float* inLeft, const float* inRight, float* outLeft, float* outRight, int numSamples);
    
    // Parameter setting
    void setParameter(BubbleParameterId paramId, float value);
    float getParameter(BubbleParameterId paramId) const;
    void setHostTempo(float bpm);
    void syncRhythmPhase(double ppqPosition);
    BubbleCloudTelemetry getTelemetrySnapshot() noexcept;
    
    // State getting/setting for preset saving
    EngineConfig_t getConfig() const;
    void setConfig(const EngineConfig_t& config);

    // M3.2B: cached read-position interpolator of the left engine
    // (0 = linear, 1 = Hermite), derived from the active quality profile. The
    // JUCE layer only selects the profile; the DSP core owns the interpolator.
    int getInterpolationMode() const noexcept;

#if defined(BUBBLES_BUILD_PROCESSOR_TESTS)
    // Test-only observation that the selected interpolation path actually ran.
    // Compiled out of production builds.
    void getInterpolationCallCounts(unsigned long long& linearSamples,
                                    unsigned long long& hermiteSamples) const noexcept;

    // M3.2C: per-engine voice limit resolved from the global quality profile.
    int getActiveVoiceLimit() const noexcept;
#endif

    // Explicit stereo wet summing law.
    //
    // Each engine contributes its own decorrelated spatial wet field (separate
    // RNG streams) to the shared output bus:
    //     L = dryL + wetSumGain * (wetLL + wetRL)
    //     R = dryR + wetSumGain * (wetLR + wetRR)
    //
    // Two independent fields sum in power, so the equal-power factor 1/sqrt(2)
    // conserves the total wet energy of the single-field reference architecture
    // for mono, dual-mono and stereo input, while preserving the stereo width
    // ratio. The dry bus stays channel-local and is never scaled.
    static constexpr float wetSumGain = 0.70710678118654752440f;

    // Applies the bus law above. Exposed so native probes can validate the
    // arithmetic directly without duplicating the constants.
    static inline void sumStereoBus(float dryL, float dryR,
                                    float wetLeftFromL, float wetRightFromL,
                                    float wetLeftFromR, float wetRightFromR,
                                    float& outL, float& outR) noexcept
    {
        outL = dryL + wetSumGain * (wetLeftFromL + wetLeftFromR);
        outR = dryR + wetSumGain * (wetRightFromL + wetRightFromR);
    }

private:
    struct AtomicVoiceTelemetry
    {
        std::atomic<int> active { 0 };
        std::atomic<float> phase { 0.0f };
        std::atomic<float> pan { 0.0f };
        std::atomic<float> gain { 0.0f };
        std::atomic<float> pitchRate { 1.0f };
        std::atomic<int> bubbleClass { 0 };
        std::atomic<int> reverse { 0 };
        std::atomic<int> channel { 0 };
    };

    struct MetricsCallbackContext
    {
        BubbleCloudEngineWrapper* owner = nullptr;
        int channel = 0;
    };

    static void metricsCallback(const BubbleEngineBlockMetrics_t* metrics, void* userData);
    static void storePeak(std::atomic<float>& destination, float value) noexcept;
    static void storeMin(std::atomic<float>& destination, float value) noexcept;
    static int cachedParameterIndex(BubbleParameterId paramId) noexcept;
    void publishVoiceTelemetry() noexcept;

    BubbleEngine_t engineL {};
    BubbleEngine_t engineR {};
    EngineConfig_t pendingConfig {};
    
    std::vector<int16_t> delayBufferL;
    std::vector<int16_t> delayBufferR;

    // Per-instance spatial split buffers. Each mono instance yields a wet stereo
    // bus plus its own dry mono bus; the wrapper sums wet from both instances
    // (full spatial field) and places dry on its own channel only.
    std::vector<float> wetLeftFromL;
    std::vector<float> wetRightFromL;
    std::vector<float> dryFromL;
    std::vector<float> wetLeftFromR;
    std::vector<float> wetRightFromR;
    std::vector<float> dryFromR;

    double currentSampleRate = 44100.0;
    float lastHostTempo = -1.0f;
    bool prepared = false;
    bool hasPendingConfig = false;
    int telemetrySamplesUntilVoicePublish = 0;
    MetricsCallbackContext metricsContextL;
    MetricsCallbackContext metricsContextR;
    std::array<AtomicVoiceTelemetry, BubbleCloudTelemetry::maxVoices> telemetryVoices;
    std::atomic<int> telemetryActiveVoices { 0 };
    std::atomic<int> telemetryActiveVoiceLimit { BUBBLES_MAX_VOICES * 2 };
    std::atomic<int> telemetrySpawnCount { 0 };
    std::atomic<int> telemetryEngineStateL { 0 };
    std::atomic<int> telemetryEngineStateR { 0 };
    std::atomic<int> telemetryRhythmStep { -1 };
    std::atomic<float> telemetryEnvelopeL { 0.0f };
    std::atomic<float> telemetryEnvelopeR { 0.0f };
    std::atomic<float> telemetryPeakL { 0.0f };
    std::atomic<float> telemetryPeakR { 0.0f };
    // Final-bus readings captured after the shared limiter, not the per-engine
    // pre-limiter wet metrics owned by the core.
    std::atomic<float> telemetryFinalLimiterGain { 1.0f };
    std::atomic<int> telemetryClipCount { 0 };
    std::atomic<int> telemetryTempoSync { 0 };
    std::atomic<int> telemetryFrozen { 0 };
    
    // Fixed storage keeps parameter recall allocation-free on the audio thread.
    static constexpr std::size_t cachedParameterCount = 19;
    std::array<float, cachedParameterCount> cachedParameterValues {};
    std::array<bool, cachedParameterCount> cachedParameterValid {};
};
