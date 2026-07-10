#include "BubbleCloudEngineWrapper.h"

BubbleCloudEngineWrapper::BubbleCloudEngineWrapper()
{
}

BubbleCloudEngineWrapper::~BubbleCloudEngineWrapper()
{
}

void BubbleCloudEngineWrapper::prepare(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    
    // Calculate required delay buffer size from the C core API
    size_t requiredBufferSize = SoundBubbles_RequiredBufferSamples((float)sampleRate);
    
    // Allocate buffers
    delayBufferL.resize(requiredBufferSize, 0);
    delayBufferR.resize(requiredBufferSize, 0);
    monoMixBuffer.resize(samplesPerBlock, 0.0f);
    
    // Initialize default configs
    EngineConfig_t configL, configR;
    bubble_engine_default_config(&configL);
    bubble_engine_default_config(&configR);
    
    configL.sample_rate = (float)sampleRate;
    configR.sample_rate = (float)sampleRate;
    
    // Initialize engines
    bubble_engine_init(&engineL, delayBufferL.data(), &configL);
    bubble_engine_init(&engineR, delayBufferR.data(), &configR);
    
    // Decorrelate the right channel's RNG seed so it doesn't sound completely mono
    engineR.core.rng_state ^= 0x55555555;
    
    // Apply previously set parameters (macros)
    for (int i = 0; i < BUBBLE_ENGINE_NUM_PARAMS; ++i) {
        bubble_engine_set_parameter(&engineL, (BubbleEngineParameterId_t)i, macroValues[i]);
        bubble_engine_set_parameter(&engineR, (BubbleEngineParameterId_t)i, macroValues[i]);
    }
}

void BubbleCloudEngineWrapper::process(const float* inLeft, const float* inRight, float* outLeft, float* outRight, int numSamples)
{
    // Resize mono mix buffer if needed
    if (monoMixBuffer.size() < (size_t)numSamples) {
        monoMixBuffer.resize(numSamples, 0.0f);
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
    
    std::vector<float> dumpL_R(numSamples);
    std::vector<float> dumpR_L(numSamples);
    
    bubble_engine_process(&engineL, monoMixBuffer.data(), outLeft, dumpL_R.data(), numSamples);
    bubble_engine_process(&engineR, monoMixBuffer.data(), dumpR_L.data(), outRight, numSamples);
}

void BubbleCloudEngineWrapper::setParameter(BubbleEngineParameterId_t paramId, float value)
{
    macroValues[paramId] = value;
    bubble_engine_set_parameter(&engineL, paramId, value);
    bubble_engine_set_parameter(&engineR, paramId, value);
}

float BubbleCloudEngineWrapper::getParameter(BubbleEngineParameterId_t paramId) const
{
    return macroValues[paramId];
}

EngineConfig_t BubbleCloudEngineWrapper::getConfig() const
{
    return engineL.config;
}

void BubbleCloudEngineWrapper::setConfig(const EngineConfig_t& config)
{
    EngineConfig_t safeConfig = config;
    safeConfig.sample_rate = (float)currentSampleRate;
    
    bubble_engine_set_config(&engineL, &safeConfig);
    bubble_engine_set_config(&engineR, &safeConfig);
}
