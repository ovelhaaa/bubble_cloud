#include "PluginProcessor.h"
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
        treeState.addParameterListener(name, [this, name](const juce::String&, float value) {
            BubbleEngineParameterId_t paramId = BUBBLE_ENGINE_PARAM_DENSITY;
            
            if (name == "DENSITY") paramId = BUBBLE_ENGINE_PARAM_DENSITY;
            else if (name == "PANORAMA") paramId = BUBBLE_ENGINE_PARAM_PANORAMA;
            else if (name == "MEMORY_PULL") paramId = BUBBLE_ENGINE_PARAM_MEMORY_PULL;
            else if (name == "SPARKLE") paramId = BUBBLE_ENGINE_PARAM_SPARKLE;
            else if (name == "REVERSE") paramId = BUBBLE_ENGINE_PARAM_REVERSE;
            else if (name == "DIFFUSION") paramId = BUBBLE_ENGINE_PARAM_DIFFUSION;
            else if (name == "WET_PRESENCE") paramId = BUBBLE_ENGINE_PARAM_WET_PRESENCE;
            else if (name == "DUCKING") paramId = BUBBLE_ENGINE_PARAM_DUCKING;
            else if (name == "DECAY") paramId = BUBBLE_ENGINE_PARAM_DECAY;
            else if (name == "ATTACK_RATE") paramId = BUBBLE_ENGINE_PARAM_ATTACK_RATE;
            else if (name == "PITCH_MODE") paramId = BUBBLE_ENGINE_PARAM_PITCH_MODE;
            else if (name == "QUALITY_PROFILE") paramId = BUBBLE_ENGINE_PARAM_QUALITY_PROFILE;
            else if (name == "FREEZE") paramId = BUBBLE_ENGINE_PARAM_FREEZE;
            
            engineWrapper.setParameter(paramId, value);
        });
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
    for (auto* param : treeState.getParameterTree().getParameters()) {
        float value = param->getValue();
        // Trigger listener manually since JUCE doesn't fire listeners initially
        if (param->paramID == "DENSITY") engineWrapper.setParameter(BUBBLE_ENGINE_PARAM_DENSITY, value);
        else if (param->paramID == "PANORAMA") engineWrapper.setParameter(BUBBLE_ENGINE_PARAM_PANORAMA, value);
        else if (param->paramID == "MEMORY_PULL") engineWrapper.setParameter(BUBBLE_ENGINE_PARAM_MEMORY_PULL, value);
        else if (param->paramID == "SPARKLE") engineWrapper.setParameter(BUBBLE_ENGINE_PARAM_SPARKLE, value);
        else if (param->paramID == "REVERSE") engineWrapper.setParameter(BUBBLE_ENGINE_PARAM_REVERSE, value);
        else if (param->paramID == "DIFFUSION") engineWrapper.setParameter(BUBBLE_ENGINE_PARAM_DIFFUSION, value);
        else if (param->paramID == "WET_PRESENCE") engineWrapper.setParameter(BUBBLE_ENGINE_PARAM_WET_PRESENCE, value);
        else if (param->paramID == "DUCKING") engineWrapper.setParameter(BUBBLE_ENGINE_PARAM_DUCKING, value);
        else if (param->paramID == "DECAY") engineWrapper.setParameter(BUBBLE_ENGINE_PARAM_DECAY, value);
        else if (param->paramID == "ATTACK_RATE") engineWrapper.setParameter(BUBBLE_ENGINE_PARAM_ATTACK_RATE, value);
        else if (param->paramID == "PITCH_MODE") engineWrapper.setParameter(BUBBLE_ENGINE_PARAM_PITCH_MODE, value);
        else if (param->paramID == "QUALITY_PROFILE") engineWrapper.setParameter(BUBBLE_ENGINE_PARAM_QUALITY_PROFILE, value);
        else if (param->paramID == "FREEZE") engineWrapper.setParameter(BUBBLE_ENGINE_PARAM_FREEZE, value);
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
    return false; // For now, use generic UI
}

juce::AudioProcessorEditor* BubbleCloudAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

void BubbleCloudAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Use the core's JSON serializer
    BubbleEnginePreset_t preset;
    preset.config = engineWrapper.getConfig();
    preset.master_dry_gain = 0.0f; // No dry parameter yet
    preset.master_wet_gain = 1.0f; 

    // We also need to store the macro values since those dictate the UI!
    // But bubble_preset_save_json saves the engine config, not the macros.
    // Actually, in APVTS, you can just save the APVTS state as XML.
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

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BubbleCloudAudioProcessor();
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
