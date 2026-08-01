#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <memory>
#include <vector>

extern "C" {
#include "bubble_preset.h"
}

namespace
{
    constexpr const char* productParameterIds[] = {
        "DENSITY",
        "BLOOM",
        "MOTION",
        "TEXTURE",
        "SPACE",
        "GRAVITY",
        "MEMORY",
        "CLARITY",
        "FREEZE",
        "SPARKLE",
        "WARMTH",
        "MIX",
        "QUALITY_PROFILE",
        "TEMPO_SYNC",
        "RHYTHM_DIVISION",
        "BURST_MODE",
        "RHYTHM_PATTERN",
        "PITCH_MODE_OVERRIDE",
        "MOTION_SHAPE",
    };

    std::unique_ptr<juce::AudioParameterFloat> makeMacroParameter(const char* id,
                                                                  const char* name,
                                                                  float defaultValue)
    {
        return std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 },
            name,
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
            defaultValue);
    }

    std::unique_ptr<juce::AudioParameterChoice> makeQualityParameter()
    {
        return std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { "QUALITY_PROFILE", 1 },
            "Quality Profile",
            juce::StringArray { "Eco", "Balanced", "Studio", "Ultra" },
            2);
    }

    std::unique_ptr<juce::AudioParameterBool> makeAdvancedToggle(const char* id,
                                                                  const char* name,
                                                                  bool defaultValue)
    {
        return std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, name, defaultValue);
    }

    std::unique_ptr<juce::AudioParameterChoice> makeAdvancedChoice(const char* id,
                                                                    const char* name,
                                                                    juce::StringArray choices,
                                                                    int defaultIndex)
    {
        return std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { id, 1 }, name, std::move(choices), defaultIndex);
    }

}

BubbleCloudAudioProcessor::BubbleCloudAudioProcessor()
     : AudioProcessor (createBusesProperties()),
       treeState(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (const auto* name : productParameterIds) {
        treeState.addParameterListener(name, this);
    }
}

BubbleCloudAudioProcessor::~BubbleCloudAudioProcessor()
{
    for (const auto* name : productParameterIds) {
        treeState.removeParameterListener(name, this);
    }
}

juce::AudioProcessor::BusesProperties BubbleCloudAudioProcessor::createBusesProperties()
{
    const bool inputActiveByDefault =
        juce::PluginHostType::getPluginLoadedAs() != juce::AudioProcessor::wrapperType_Standalone;

    return BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), inputActiveByDefault)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

juce::AudioProcessorValueTreeState::ParameterLayout BubbleCloudAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(makeMacroParameter("DENSITY", "Density", 0.5f));
    params.push_back(makeMacroParameter("BLOOM", "Bloom", 0.5f));
    params.push_back(makeMacroParameter("MOTION", "Motion", 0.5f));
    params.push_back(makeMacroParameter("TEXTURE", "Texture", 0.5f));
    params.push_back(makeMacroParameter("SPACE", "Space", 0.5f));
    params.push_back(makeMacroParameter("GRAVITY", "Gravity", 0.5f));
    params.push_back(makeMacroParameter("MEMORY", "Memory", 0.5f));
    params.push_back(makeMacroParameter("CLARITY", "Clarity", 0.5f));
    params.push_back(makeMacroParameter("FREEZE", "Freeze", 0.0f));
    params.push_back(makeMacroParameter("SPARKLE", "Sparkle", 0.0f));
    params.push_back(makeMacroParameter("WARMTH", "Warmth", 0.5f));
    params.push_back(makeMacroParameter("MIX", "Mix", 0.5f));
    params.push_back(makeQualityParameter());
    params.push_back(makeAdvancedToggle("TEMPO_SYNC", "Tempo Sync", false));
    params.push_back(makeAdvancedChoice(
        "RHYTHM_DIVISION", "Rhythm Division",
        juce::StringArray { "1/4", "1/8", "1/16", "1/32" }, 2));
    params.push_back(makeAdvancedChoice(
        "BURST_MODE", "Burst Mode",
        juce::StringArray { "Single", "Spray", "Strum", "Swarm", "Reverse Swell" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "RHYTHM_PATTERN", 1 }, "Rhythm Pattern", 0, 65535, 4369));
    params.push_back(makeAdvancedChoice(
        "PITCH_MODE_OVERRIDE", "Pitch Mode",
        juce::StringArray { "Macro", "Octave Up", "Octave Down", "Fifth" }, 0));
    params.push_back(makeAdvancedChoice(
        "MOTION_SHAPE", "Motion Shape",
        juce::StringArray { "Triangle", "Smooth", "Hold" }, 0));

    return { params.begin(), params.end() };
}

void BubbleCloudAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engineWrapper.prepare(sampleRate, samplesPerBlock);
    expectedNextPpq = 0.0;
    hasExpectedNextPpq = false;
    wasTransportPlaying = false;

    for (const auto* name : productParameterIds) {
        if (auto* p = treeState.getParameter(name)) {
            parameterChanged(name, p->convertFrom0to1(p->getValue()));
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

    const auto inputLayout = layouts.getMainInputChannelSet();

    if (inputLayout == juce::AudioChannelSet::disabled())
        return juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone;

    if (inputLayout != juce::AudioChannelSet::stereo() &&
        inputLayout != juce::AudioChannelSet::mono())
        return false;

    return true;
}

void BubbleCloudAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int bufferChannels = buffer.getNumChannels();

    if (buffer.getNumSamples() <= 0 || bufferChannels <= 0) {
        return;
    }

    if (bufferChannels < 2 || totalNumOutputChannels < 2 || totalNumInputChannels <= 0) {
        buffer.clear();
        return;
    }

    // Clear output channels that don't contain input data
    for (auto i = totalNumInputChannels; i < juce::jmin(totalNumOutputChannels, bufferChannels); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const float* inLeft = buffer.getReadPointer(0);
    const float* inRight = totalNumInputChannels > 1 && bufferChannels > 1 ? buffer.getReadPointer(1) : inLeft;

    float* outLeft = buffer.getWritePointer(0);
    float* outRight = buffer.getWritePointer(1);

    updateHostTransport(buffer.getNumSamples());
    engineWrapper.process(inLeft, inRight, outLeft, outRight, buffer.getNumSamples());
}

void BubbleCloudAudioProcessor::updateHostTransport(int numSamples)
{
    const auto* syncValue = treeState.getRawParameterValue("TEMPO_SYNC");
    if (syncValue == nullptr || syncValue->load() < 0.5f) {
        hasExpectedNextPpq = false;
        wasTransportPlaying = false;
        return;
    }

    auto* playHead = getPlayHead();
    if (playHead == nullptr) {
        hasExpectedNextPpq = false;
        wasTransportPlaying = false;
        return;
    }

    const auto position = playHead->getPosition();
    if (!position.hasValue()) {
        hasExpectedNextPpq = false;
        wasTransportPlaying = false;
        return;
    }

    double effectiveBpm = 120.0;
    if (const auto bpm = position->getBpm(); bpm.hasValue() && std::isfinite(*bpm)) {
        effectiveBpm = juce::jlimit(20.0, 300.0, *bpm);
        engineWrapper.setHostTempo((float)effectiveBpm);
    }

    const bool isPlaying = position->getIsPlaying();
    const auto ppq = position->getPpqPosition();
    if (isPlaying && ppq.hasValue() && std::isfinite(*ppq)) {
        const bool transportJumped = !wasTransportPlaying
            || !hasExpectedNextPpq
            || std::abs(*ppq - expectedNextPpq) > 0.125;
        if (transportJumped) {
            engineWrapper.syncRhythmPhase(*ppq);
        }

        const double sampleRate = getSampleRate();
        const double blockQuarterNotes = sampleRate > 0.0
            ? ((double)numSamples / sampleRate) * (effectiveBpm / 60.0)
            : 0.0;
        expectedNextPpq = *ppq + blockQuarterNotes;
        hasExpectedNextPpq = true;
    } else {
        hasExpectedNextPpq = false;
    }

    wasTransportPlaying = isPlaying;
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
    else if (parameterID == "BLOOM") paramId = BUBBLE_PARAM_BLOOM;
    else if (parameterID == "MOTION") paramId = BUBBLE_PARAM_MOTION;
    else if (parameterID == "TEXTURE") paramId = BUBBLE_PARAM_TEXTURE;
    else if (parameterID == "SPACE") paramId = BUBBLE_PARAM_SPACE;
    else if (parameterID == "GRAVITY") paramId = BUBBLE_PARAM_GRAVITY;
    else if (parameterID == "MEMORY") paramId = BUBBLE_PARAM_MEMORY;
    else if (parameterID == "CLARITY") paramId = BUBBLE_PARAM_CLARITY;
    else if (parameterID == "FREEZE") paramId = BUBBLE_PARAM_FREEZE;
    else if (parameterID == "SPARKLE") paramId = BUBBLE_PARAM_SPARKLE;
    else if (parameterID == "WARMTH") paramId = BUBBLE_PARAM_WARMTH;
    else if (parameterID == "MIX") paramId = BUBBLE_PARAM_MIX;
    else if (parameterID == "QUALITY_PROFILE") {
        paramId = BUBBLE_ENGINE_PARAM_QUALITY_PROFILE;
        // AudioProcessorValueTreeState listeners receive the denormalised
        // AudioParameterChoice index, so 0..3 can be forwarded directly.
        newValue = (float)juce::roundToInt(juce::jlimit(0.0f, 3.0f, newValue));
    }
    else if (parameterID == "TEMPO_SYNC") {
        paramId = BUBBLE_ENGINE_PARAM_TEMPO_SYNC_ENABLED;
        newValue = newValue >= 0.5f ? 1.0f : 0.0f;
    }
    else if (parameterID == "RHYTHM_DIVISION") {
        paramId = BUBBLE_ENGINE_PARAM_RHYTHM_DIVISION;
        newValue = (float)juce::roundToInt(juce::jlimit(0.0f, 3.0f, newValue));
    }
    else if (parameterID == "BURST_MODE") {
        paramId = BUBBLE_ENGINE_PARAM_BURST_MODE;
        newValue = (float)juce::roundToInt(juce::jlimit(0.0f, 4.0f, newValue));
    }
    else if (parameterID == "RHYTHM_PATTERN") {
        paramId = BUBBLE_ENGINE_PARAM_RHYTHM_PATTERN;
        newValue = (float)juce::roundToInt(juce::jlimit(0.0f, 65535.0f, newValue));
    }
    else if (parameterID == "PITCH_MODE_OVERRIDE") {
        paramId = BUBBLE_ENGINE_PARAM_PITCH_MODE;
        newValue = (float)juce::roundToInt(juce::jlimit(0.0f, 3.0f, newValue));
    }
    else if (parameterID == "MOTION_SHAPE") {
        paramId = BUBBLE_ENGINE_PARAM_MOTION_SHAPE;
        newValue = (float)juce::roundToInt(juce::jlimit(0.0f, 2.0f, newValue));
    }
    else return;
    
    engineWrapper.setParameter(paramId, newValue);
}

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BubbleCloudAudioProcessor();
}
