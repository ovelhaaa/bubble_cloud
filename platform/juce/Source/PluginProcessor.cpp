#include "PluginProcessor.h"
#include "PluginEditor.h"
extern "C" {
#include "bubble_preset.h"
}

BubbleCloudAudioProcessor::BubbleCloudAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                       ),
       treeState(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // Register parameter listeners
    auto paramNames = {
        "DENSITY", "PANORAMA", "MEMORY_PULL", "SPARKLE", "REVERSE", "DIFFUSION",
        "WET_PRESENCE", "DUCKING", "DECAY", "ATTACK_RATE", "PITCH_MODE",
        "QUALITY_PROFILE", "FREEZE"
    };

    for (const auto& name : paramNames) {
        treeState.addParameterListener(name, this);
    }
}

BubbleCloudAudioProcessor::~BubbleCloudAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout BubbleCloudAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"DENSITY", 1}, "Density", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"PANORAMA", 1}, "Panorama", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"MEMORY_PULL", 1}, "Memory Pull", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"SPARKLE", 1}, "Sparkle", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"REVERSE", 1}, "Reverse", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"DIFFUSION", 1}, "Diffusion", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"WET_PRESENCE", 1}, "Wet Presence", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"DUCKING", 1}, "Ducking", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"DECAY", 1}, "Decay", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"ATTACK_RATE", 1}, "Attack Rate", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"PITCH_MODE", 1}, "Pitch Mode", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"QUALITY_PROFILE", 1}, "Quality Profile", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"FREEZE", 1}, "Freeze", 0.0f, 1.0f, 0.0f));

    return { params.begin(), params.end() };
}

void BubbleCloudAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engineWrapper.prepare(sampleRate, samplesPerBlock);

    // Initial sync
    auto paramNames = {
        "DENSITY", "PANORAMA", "MEMORY_PULL", "SPARKLE", "REVERSE", "DIFFUSION",
        "WET_PRESENCE", "DUCKING", "DECAY", "ATTACK_RATE", "PITCH_MODE",
        "QUALITY_PROFILE", "FREEZE"
    };
    for (const auto& name : paramNames) {
        if (auto* p = treeState.getParameter(name)) {
            parameterChanged(name, p->getValue());
        }
    }
}

void BubbleCloudAudioProcessor::releaseResources()
{
}

bool BubbleCloudAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo() &&
        layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono())
        return false;

    return true;
}

void BubbleCloudAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear output channels that don't contain input data
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const float* inLeft = buffer.getReadPointer(0);
    const float* inRight = totalNumInputChannels > 1 ? buffer.getReadPointer(1) : inLeft;

    float* outLeft = buffer.getWritePointer(0);
    float* outRight = buffer.getWritePointer(1);

    engineWrapper.process(inLeft, inRight, outLeft, outRight, buffer.getNumSamples());
}

bool BubbleCloudAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* BubbleCloudAudioProcessor::createEditor()
{
    return new BubbleCloudAudioProcessorEditor(*this);
}

void BubbleCloudAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = treeState.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void BubbleCloudAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr)
        if (xmlState->hasTagName (treeState.state.getType()))
            treeState.replaceState (juce::ValueTree::fromXml (*xmlState));
}

void BubbleCloudAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    BubbleParameterId paramId = BUBBLE_PARAM_DENSITY;
    
    if (parameterID == "DENSITY") paramId = BUBBLE_PARAM_DENSITY;
    else if (parameterID == "PANORAMA") paramId = BUBBLE_ENGINE_PARAM_STEREO_WIDTH;
    else if (parameterID == "MEMORY_PULL") paramId = BUBBLE_ENGINE_PARAM_MEMORY_PULL;
    else if (parameterID == "SPARKLE") paramId = BUBBLE_PARAM_SPARKLE;
    else if (parameterID == "REVERSE") paramId = BUBBLE_ENGINE_PARAM_SMART_START_ENABLE;
    else if (parameterID == "DIFFUSION") paramId = BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_AMOUNT;
    else if (parameterID == "WET_PRESENCE") paramId = BUBBLE_ENGINE_PARAM_MIX_WET_GAIN;
    else if (parameterID == "DUCKING") paramId = BUBBLE_ENGINE_PARAM_DUCK_BURST_LEVEL;
    else if (parameterID == "DECAY") paramId = BUBBLE_ENGINE_PARAM_DENSITY_DECAY;
    else if (parameterID == "ATTACK_RATE") paramId = BUBBLE_ENGINE_PARAM_ATTACK_REGION_MAX_OFFSET_SAMPLES;
    else if (parameterID == "PITCH_MODE") paramId = BUBBLE_ENGINE_PARAM_TONE_VARIATION;
    else if (parameterID == "QUALITY_PROFILE") paramId = BUBBLE_ENGINE_PARAM_NOISE_FLOOR;
    else if (parameterID == "FREEZE") paramId = BUBBLE_PARAM_FREEZE;
    else return;
    
    engineWrapper.setParameter(paramId, newValue);
}

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BubbleCloudAudioProcessor();
}
