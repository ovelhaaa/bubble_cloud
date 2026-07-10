#pragma once

#include <vector>
#include <memory>

extern "C" {
#include "bubble_engine.h"
}

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
    
    // State getting/setting for preset saving
    EngineConfig_t getConfig() const;
    void setConfig(const EngineConfig_t& config);

private:
    BubbleEngine_t engineL;
    BubbleEngine_t engineR;
    
    std::vector<int16_t> delayBufferL;
    std::vector<int16_t> delayBufferR;
    
    std::vector<float> monoMixBuffer;

    double currentSampleRate = 44100.0;
    
    // Need to keep track of macros
    #include <map>
    std::map<BubbleParameterId, float> macroValues;
};
