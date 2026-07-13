#include "BubbleCloudEngineWrapper.h"

#include <algorithm>

BubbleCloudEngineWrapper::BubbleCloudEngineWrapper()
{
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
    monoMixBuffer.assign((size_t)safeSamplesPerBlock, 0.0f);
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
    prepared = true;
    
    // Decorrelate the right channel's RNG seed so it doesn't sound completely mono
    engineR.rng_state ^= 0x55555555;
    
    // Apply previously set parameters (macros)
    for (auto const& [k, v] : macroValues) {
        bubble_engine_set_parameter(&engineL, k, v);
        bubble_engine_set_parameter(&engineR, k, v);
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

    // Resize mono mix buffer if needed
    if (monoMixBuffer.size() < (size_t)numSamples) {
        monoMixBuffer.resize(numSamples, 0.0f);
    }
    if (scratchRightFromLeftEngine.size() < (size_t)numSamples) {
        scratchRightFromLeftEngine.resize(numSamples, 0.0f);
    }
    if (scratchLeftFromRightEngine.size() < (size_t)numSamples) {
        scratchLeftFromRightEngine.resize(numSamples, 0.0f);
    }
    
    // Downmix to mono: L+R / 2 (Wait, dual-mono implementation from Phase 1)
    for (int i = 0; i < numSamples; ++i) {
        monoMixBuffer[i] = 0.5f * (inLeft[i] + inRight[i]);
    }
    
    // The core takes mono in, stereo out. 
    // We run two instances, each giving a stereo pair. 
    // For a dual-mono VST, we want to sum the left output of engine L and right output of engine R.
    // Wait, the user specifically wants downmix for state coherence, then each engine outputs a stereo field.
    // If we just use L output from engineL and R output from engineR, we get decorrelated dual-mono!
    // But engineL outputs both Left and Right. To preserve the engine's internal panning (if any), 
    // we should just use engineL for left and engineR for right, or sum them. 
    // Let's use engineL's outLeft and engineR's outRight for a true wide stereo from two engines.
    
    bubble_engine_process(&engineL, monoMixBuffer.data(), outLeft, scratchRightFromLeftEngine.data(), numSamples);
    bubble_engine_process(&engineR, monoMixBuffer.data(), scratchLeftFromRightEngine.data(), outRight, numSamples);
}

void BubbleCloudEngineWrapper::setParameter(BubbleParameterId paramId, float value)
{
    macroValues[paramId] = value;
    if (!prepared) {
        return;
    }

    bubble_engine_set_parameter(&engineL, paramId, value);
    bubble_engine_set_parameter(&engineR, paramId, value);
}

float BubbleCloudEngineWrapper::getParameter(BubbleParameterId paramId) const
{
    auto it = macroValues.find(paramId);
    return it != macroValues.end() ? it->second : 0.0f;
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
