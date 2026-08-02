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
    static int cachedParameterIndex(BubbleParameterId paramId) noexcept;
    void publishVoiceTelemetry() noexcept;

    BubbleEngine_t engineL {};
    BubbleEngine_t engineR {};
    EngineConfig_t pendingConfig {};
    
    std::vector<int16_t> delayBufferL;
    std::vector<int16_t> delayBufferR;
    
    std::vector<float> scratchRightFromLeftEngine;
    std::vector<float> scratchLeftFromRightEngine;

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
    std::atomic<float> telemetryLimiterGainL { 1.0f };
    std::atomic<float> telemetryLimiterGainR { 1.0f };
    std::atomic<int> telemetryTempoSync { 0 };
    std::atomic<int> telemetryFrozen { 0 };
    
    // Fixed storage keeps parameter recall allocation-free on the audio thread.
    static constexpr std::size_t cachedParameterCount = 19;
    std::array<float, cachedParameterCount> cachedParameterValues {};
    std::array<bool, cachedParameterCount> cachedParameterValid {};
};
