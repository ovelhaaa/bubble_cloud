#include "BubbleCloudEngineWrapper.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    constexpr std::array<BubbleParameterId, 19> cachedParameterIds {{
        BUBBLE_PARAM_DENSITY,
        BUBBLE_PARAM_BLOOM,
        BUBBLE_PARAM_MOTION,
        BUBBLE_PARAM_TEXTURE,
        BUBBLE_PARAM_SPACE,
        BUBBLE_PARAM_GRAVITY,
        BUBBLE_PARAM_MEMORY,
        BUBBLE_PARAM_CLARITY,
        BUBBLE_PARAM_FREEZE,
        BUBBLE_PARAM_SPARKLE,
        BUBBLE_PARAM_WARMTH,
        BUBBLE_PARAM_MIX,
        BUBBLE_ENGINE_PARAM_QUALITY_PROFILE,
        BUBBLE_ENGINE_PARAM_TEMPO_SYNC_ENABLED,
        BUBBLE_ENGINE_PARAM_RHYTHM_DIVISION,
        BUBBLE_ENGINE_PARAM_BURST_MODE,
        BUBBLE_ENGINE_PARAM_RHYTHM_PATTERN,
        BUBBLE_ENGINE_PARAM_PITCH_MODE,
        BUBBLE_ENGINE_PARAM_MOTION_SHAPE,
    }};
}

BubbleCloudEngineWrapper::BubbleCloudEngineWrapper()
{
    metricsContextL = { this, 0 };
    metricsContextR = { this, 1 };
}

BubbleCloudEngineWrapper::~BubbleCloudEngineWrapper()
{
}

void BubbleCloudEngineWrapper::prepare(double sampleRate, int samplesPerBlock)
{
    prepared = false;
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    const int safeSamplesPerBlock = samplesPerBlock > 0 ? samplesPerBlock : 1;
    
    // Calculate required delay buffer size from the C core API
    size_t requiredBufferSize = SoundBubbles_RequiredBufferSamples((float)currentSampleRate);
    
    // Allocate buffers
    delayBufferL.assign(requiredBufferSize, 0);
    delayBufferR.assign(requiredBufferSize, 0);
    scratchRightFromLeftEngine.assign((size_t)safeSamplesPerBlock, 0.0f);
    scratchLeftFromRightEngine.assign((size_t)safeSamplesPerBlock, 0.0f);
    
    // Initialize default configs
    EngineConfig_t configL, configR;
    if (hasPendingConfig) {
        configL = pendingConfig;
        configR = pendingConfig;
    } else {
        bubble_engine_default_config(&configL);
        bubble_engine_default_config(&configR);
    }
    
    configL.sample_rate = (float)currentSampleRate;
    configR.sample_rate = (float)currentSampleRate;
    
    // Initialize engines
    bubble_engine_init(&engineL, delayBufferL.data(), &configL);
    bubble_engine_init(&engineR, delayBufferR.data(), &configR);
    bubble_engine_set_parameter(&engineL, BUBBLE_PARAM_DEVELOPER_MODE, 1.0f);
    bubble_engine_set_parameter(&engineR, BUBBLE_PARAM_DEVELOPER_MODE, 1.0f);
    bubble_engine_set_metrics_callback(&engineL, metricsCallback, &metricsContextL);
    bubble_engine_set_metrics_callback(&engineR, metricsCallback, &metricsContextR);
    lastHostTempo = -1.0f;
    prepared = true;

    telemetryActiveVoices.store(0);
    telemetryActiveVoiceLimit.store(engineL.active_voice_limit + engineR.active_voice_limit);
    telemetrySpawnCount.store(0);
    telemetryRhythmStep.store(-1);
    telemetryEnvelopeL.store(0.0f);
    telemetryEnvelopeR.store(0.0f);
    telemetryPeakL.store(0.0f);
    telemetryPeakR.store(0.0f);
    telemetryLimiterGainL.store(1.0f);
    telemetryLimiterGainR.store(1.0f);
    telemetryTempoSync.store(0);
    telemetryFrozen.store(0);
    telemetrySamplesUntilVoicePublish = 0;
    for (auto& voice : telemetryVoices)
        voice.active.store(0);
    
    // Decorrelate the right channel's RNG seed so it doesn't sound completely mono
    engineR.rng_state ^= 0x55555555;
    
    // Apply previously set parameters without maps or other dynamic storage.
    for (std::size_t i = 0; i < cachedParameterIds.size(); ++i) {
        if (!cachedParameterValid[i])
            continue;
        bubble_engine_set_parameter(&engineL, cachedParameterIds[i], cachedParameterValues[i]);
        bubble_engine_set_parameter(&engineR, cachedParameterIds[i], cachedParameterValues[i]);
    }
}

void BubbleCloudEngineWrapper::process(const float* inLeft, const float* inRight, float* outLeft, float* outRight, int numSamples)
{
    if (numSamples <= 0 || outLeft == nullptr || outRight == nullptr) {
        return;
    }

    if (!prepared || inLeft == nullptr) {
        std::fill(outLeft, outLeft + numSamples, 0.0f);
        std::fill(outRight, outRight + numSamples, 0.0f);
        return;
    }

    if (inRight == nullptr) {
        inRight = inLeft;
    }

    // The C core is mono-in/stereo-out. Two synchronized instances preserve
    // each input channel's own memory while retaining the core's spatial DSP.
    // Processing in prepared-size chunks avoids allocations in the audio callback
    // if a host unexpectedly supplies a larger block.
    const int scratchCapacity = (int)std::min(scratchRightFromLeftEngine.size(),
                                              scratchLeftFromRightEngine.size());
    if (scratchCapacity <= 0) {
        std::fill(outLeft, outLeft + numSamples, 0.0f);
        std::fill(outRight, outRight + numSamples, 0.0f);
        return;
    }

    int processed = 0;
    while (processed < numSamples) {
        const int chunk = std::min(numSamples - processed, scratchCapacity);
        bubble_engine_process(&engineL,
                              inLeft + processed,
                              outLeft + processed,
                              scratchRightFromLeftEngine.data(),
                              chunk);
        bubble_engine_process(&engineR,
                              inRight + processed,
                              scratchLeftFromRightEngine.data(),
                              outRight + processed,
                              chunk);
        processed += chunk;
    }

    const bool tempoSync = engineL.config.tempo_sync_enabled != 0;
    const int nextStep = engineL.rhythm_step_index & 15;
    telemetryRhythmStep.store(tempoSync ? ((nextStep + 15) & 15) : -1, std::memory_order_relaxed);
    telemetryTempoSync.store(tempoSync ? 1 : 0, std::memory_order_relaxed);

    telemetrySamplesUntilVoicePublish -= numSamples;
    if (telemetrySamplesUntilVoicePublish <= 0) {
        publishVoiceTelemetry();
        telemetrySamplesUntilVoicePublish = std::max(1, (int)(currentSampleRate / 60.0));
    }
}

void BubbleCloudEngineWrapper::storePeak(std::atomic<float>& destination, float value) noexcept
{
    float current = destination.load(std::memory_order_relaxed);
    while (value > current
           && !destination.compare_exchange_weak(current, value,
                                                 std::memory_order_relaxed,
                                                 std::memory_order_relaxed)) {
    }
}

void BubbleCloudEngineWrapper::metricsCallback(const BubbleEngineBlockMetrics_t* metrics, void* userData)
{
    if (metrics == nullptr || userData == nullptr)
        return;

    const auto* context = static_cast<const MetricsCallbackContext*>(userData);
    auto* owner = context->owner;
    if (owner == nullptr)
        return;

    int spawnCount = owner->telemetrySpawnCount.load(std::memory_order_relaxed);
    const int addedSpawns = std::max(0, metrics->spawn_count);
    while (spawnCount < 1024) {
        const int nextCount = std::min(1024, spawnCount + addedSpawns);
        if (owner->telemetrySpawnCount.compare_exchange_weak(spawnCount, nextCount,
                                                             std::memory_order_relaxed,
                                                             std::memory_order_relaxed))
            break;
    }
    if (context->channel == 0) {
        owner->telemetryEnvelopeL.store(metrics->envelope, std::memory_order_relaxed);
        owner->telemetryEngineStateL.store(metrics->engine_state, std::memory_order_relaxed);
        owner->telemetryLimiterGainL.store(metrics->limiter_gain, std::memory_order_relaxed);
        storePeak(owner->telemetryPeakL, metrics->peak_l);
    } else {
        owner->telemetryEnvelopeR.store(metrics->envelope, std::memory_order_relaxed);
        owner->telemetryEngineStateR.store(metrics->engine_state, std::memory_order_relaxed);
        owner->telemetryLimiterGainR.store(metrics->limiter_gain, std::memory_order_relaxed);
        storePeak(owner->telemetryPeakR, metrics->peak_r);
    }
}

void BubbleCloudEngineWrapper::publishVoiceTelemetry() noexcept
{
    int activeVoices = 0;

    const auto publishEngine = [this, &activeVoices](const BubbleEngine_t& engine, int channel) {
        for (int i = 0; i < BUBBLES_MAX_VOICES; ++i) {
            const int telemetryIndex = channel * BUBBLES_MAX_VOICES + i;
            auto& destination = telemetryVoices[(std::size_t)telemetryIndex];
            const auto& source = engine.voices[i];
            const bool active = i < engine.active_voice_limit && source.state != VOICE_STATE_INACTIVE;
            if (!active) {
                destination.active.store(0, std::memory_order_release);
                continue;
            }

            const float panDenominator = std::max(0.0001f, source.pan_l + source.pan_r);
            const float localPan = std::clamp((source.pan_r - source.pan_l) / panDenominator, -1.0f, 1.0f);
            const float channelCentre = channel == 0 ? -0.64f : 0.64f;
            const float globalPan = std::clamp(channelCentre + localPan * 0.34f, -1.0f, 1.0f);
            destination.phase.store(source.phase, std::memory_order_relaxed);
            destination.pan.store(globalPan, std::memory_order_relaxed);
            destination.gain.store(std::abs(source.gain * source.amp), std::memory_order_relaxed);
            destination.pitchRate.store(std::abs(source.quantized_rate), std::memory_order_relaxed);
            destination.bubbleClass.store((int)source.bubble_class, std::memory_order_relaxed);
            destination.reverse.store(source.read_direction != 0 ? 1 : 0, std::memory_order_relaxed);
            destination.channel.store(channel, std::memory_order_relaxed);
            destination.active.store(1, std::memory_order_release);
            ++activeVoices;
        }
    };

    publishEngine(engineL, 0);
    publishEngine(engineR, 1);
    telemetryActiveVoices.store(activeVoices, std::memory_order_relaxed);
    telemetryActiveVoiceLimit.store(engineL.active_voice_limit + engineR.active_voice_limit,
                                    std::memory_order_relaxed);
    const bool tempoSync = engineL.config.tempo_sync_enabled != 0;
    const int nextStep = engineL.rhythm_step_index & 15;
    telemetryRhythmStep.store(tempoSync ? ((nextStep + 15) & 15) : -1, std::memory_order_relaxed);
    telemetryTempoSync.store(tempoSync ? 1 : 0, std::memory_order_relaxed);
    const bool frozen = engineL.config.freeze_enabled != 0 || engineL.config.freeze_amount >= 0.5f;
    telemetryFrozen.store(frozen ? 1 : 0, std::memory_order_relaxed);
}

BubbleCloudTelemetry BubbleCloudEngineWrapper::getTelemetrySnapshot() noexcept
{
    BubbleCloudTelemetry snapshot;
    snapshot.activeVoices = telemetryActiveVoices.load(std::memory_order_relaxed);
    snapshot.activeVoiceLimit = telemetryActiveVoiceLimit.load(std::memory_order_relaxed);
    snapshot.spawnCount = telemetrySpawnCount.exchange(0, std::memory_order_relaxed);
    const float envelopeL = telemetryEnvelopeL.load(std::memory_order_relaxed);
    const float envelopeR = telemetryEnvelopeR.load(std::memory_order_relaxed);
    snapshot.envelope = 0.5f * (envelopeL + envelopeR);
    snapshot.engineState = envelopeL >= envelopeR
        ? telemetryEngineStateL.load(std::memory_order_relaxed)
        : telemetryEngineStateR.load(std::memory_order_relaxed);
    snapshot.rhythmStep = telemetryRhythmStep.load(std::memory_order_relaxed);
    snapshot.peakLeft = telemetryPeakL.exchange(0.0f, std::memory_order_relaxed);
    snapshot.peakRight = telemetryPeakR.exchange(0.0f, std::memory_order_relaxed);
    snapshot.limiterGain = std::min(telemetryLimiterGainL.load(std::memory_order_relaxed),
                                    telemetryLimiterGainR.load(std::memory_order_relaxed));
    snapshot.tempoSync = telemetryTempoSync.load(std::memory_order_relaxed) != 0;
    snapshot.frozen = telemetryFrozen.load(std::memory_order_relaxed) != 0;

    for (std::size_t i = 0; i < telemetryVoices.size(); ++i) {
        const auto& source = telemetryVoices[i];
        auto& destination = snapshot.voices[i];
        destination.active = source.active.load(std::memory_order_acquire) != 0;
        if (!destination.active)
            continue;
        destination.phase = source.phase.load(std::memory_order_relaxed);
        destination.pan = source.pan.load(std::memory_order_relaxed);
        destination.gain = source.gain.load(std::memory_order_relaxed);
        destination.pitchRate = source.pitchRate.load(std::memory_order_relaxed);
        destination.bubbleClass = source.bubbleClass.load(std::memory_order_relaxed);
        destination.reverse = source.reverse.load(std::memory_order_relaxed) != 0;
        destination.channel = source.channel.load(std::memory_order_relaxed);
    }

    return snapshot;
}

void BubbleCloudEngineWrapper::setParameter(BubbleParameterId paramId, float value)
{
    const int cacheIndex = cachedParameterIndex(paramId);
    if (cacheIndex >= 0) {
        cachedParameterValues[(std::size_t)cacheIndex] = value;
        cachedParameterValid[(std::size_t)cacheIndex] = true;
    }
    if (!prepared) {
        return;
    }

    bubble_engine_set_parameter(&engineL, paramId, value);
    bubble_engine_set_parameter(&engineR, paramId, value);
}

float BubbleCloudEngineWrapper::getParameter(BubbleParameterId paramId) const
{
    const int cacheIndex = cachedParameterIndex(paramId);
    return cacheIndex >= 0 && cachedParameterValid[(std::size_t)cacheIndex]
        ? cachedParameterValues[(std::size_t)cacheIndex]
        : 0.0f;
}

int BubbleCloudEngineWrapper::cachedParameterIndex(BubbleParameterId paramId) noexcept
{
    for (std::size_t i = 0; i < cachedParameterIds.size(); ++i) {
        if (cachedParameterIds[i] == paramId)
            return (int)i;
    }
    return -1;
}

void BubbleCloudEngineWrapper::setHostTempo(float bpm)
{
    const float safeBpm = std::clamp(bpm, 20.0f, 300.0f);
    if (!prepared || std::fabs(safeBpm - lastHostTempo) < 0.001f) {
        return;
    }

    bubble_engine_set_parameter(&engineL, BUBBLE_ENGINE_PARAM_TEMPO_BPM, safeBpm);
    bubble_engine_set_parameter(&engineR, BUBBLE_ENGINE_PARAM_TEMPO_BPM, safeBpm);
    lastHostTempo = safeBpm;
}

void BubbleCloudEngineWrapper::syncRhythmPhase(double ppqPosition)
{
    if (!prepared) {
        return;
    }

    bubble_engine_sync_rhythm_phase(&engineL, ppqPosition);
    bubble_engine_sync_rhythm_phase(&engineR, ppqPosition);
}

EngineConfig_t BubbleCloudEngineWrapper::getConfig() const
{
    if (!prepared) {
        EngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = (float)currentSampleRate;
        return hasPendingConfig ? pendingConfig : config;
    }

    return engineL.config;
}

void BubbleCloudEngineWrapper::setConfig(const EngineConfig_t& config)
{
    EngineConfig_t safeConfig = config;
    safeConfig.sample_rate = (float)currentSampleRate;

    pendingConfig = safeConfig;
    hasPendingConfig = true;

    if (!prepared) {
        return;
    }
    
    SoundBubbles_UpdateConfig(&engineL, &safeConfig);
    SoundBubbles_UpdateConfig(&engineR, &safeConfig);
}
