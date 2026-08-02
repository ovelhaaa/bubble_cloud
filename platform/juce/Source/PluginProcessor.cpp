#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <limits>
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
        "MORPH",
        "FREEZE_MIDI_MODE",
        "FREEZE_MIDI_NOTE",
    };

    constexpr std::array<const char*, 19> sceneParameterIds {{
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
    }};

    int findSceneParameterIndex(const juce::String& parameterID)
    {
        for (std::size_t i = 0; i < sceneParameterIds.size(); ++i) {
            if (parameterID == sceneParameterIds[i])
                return (int)i;
        }
        return -1;
    }

    bool sceneParameterIsContinuous(std::size_t index)
    {
        return index < 12;
    }

    float smoothStep(float value)
    {
        const float x = juce::jlimit(0.0f, 1.0f, value);
        return x * x * (3.0f - 2.0f * x);
    }

    float morphedContinuousValue(std::size_t index, float a, float b, float morph)
    {
        const float t = juce::jlimit(0.0f, 1.0f, morph);
        if (t <= 0.0f)
            return a;
        if (t >= 1.0f)
            return b;

        if (index == 0) { // Density is perceived approximately as an event-rate ratio.
            constexpr float floor = 0.02f;
            const float shaped = smoothStep(t);
            return std::exp(std::log(a + floor) * (1.0f - shaped)
                            + std::log(b + floor) * shaped) - floor;
        }

        if (index == 4) { // Space eases gently at both ends for stable stereo images.
            const float shaped = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * t);
            return a + (b - a) * shaped;
        }

        if (index == 11) { // Mix moves in power space to avoid an audible midpoint dip.
            const float angle = juce::MathConstants<float>::halfPi * t;
            const float aWeight = std::cos(angle);
            const float bWeight = std::sin(angle);
            return std::sqrt(std::max(0.0f,
                a * a * aWeight * aWeight + b * b * bWeight * bWeight));
        }

        return a + (b - a) * smoothStep(t);
    }

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
    jassert(sceneParameterIds.size() == sceneParameterCount);
    const float notApplied = std::numeric_limits<float>::quiet_NaN();
    lastAppliedSceneValues.fill(notApplied);
    for (std::size_t i = 0; i < sceneParameterCount; ++i) {
        sceneA[i].store(0.0f);
        sceneB[i].store(0.0f);
    }

    for (const auto* name : productParameterIds) {
        treeState.addParameterListener(name, this);
    }

    initialiseScenesFromParameters();
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
    params.push_back(makeMacroParameter("MORPH", "Scene Morph", 0.0f));
    params.push_back(makeAdvancedChoice(
        "FREEZE_MIDI_MODE", "Freeze MIDI Mode",
        juce::StringArray { "Latch", "Momentary" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "FREEZE_MIDI_NOTE", 1 }, "Freeze MIDI Note", 0, 127, 60));

    return { params.begin(), params.end() };
}

void BubbleCloudAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engineWrapper.prepare(sampleRate, samplesPerBlock);
    expectedNextPpq = 0.0;
    hasExpectedNextPpq = false;
    wasTransportPlaying = false;
    midiFreezeActive.store(false);
    captureHeld.store(false);
    effectiveFreezeActive.store(false);
    lastAppliedFreeze = -1.0f;
    sceneApplicationDirty.store(true);

    if (const auto* morph = treeState.getRawParameterValue("MORPH"))
    {
        const float value = juce::jlimit(0.0f, 1.0f, morph->load());
        morphTarget.store(value);
        endpointEditScene.store(value >= 0.5f ? 1 : 0);
        discreteMorphScene.store(value >= 0.5f ? 1 : 0);
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

void BubbleCloudAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
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

    handlePerformanceMidi(midiMessages);
    applySceneMorph();
    applyEffectiveFreeze();
    updateHostTransport(buffer.getNumSamples());
    engineWrapper.process(inLeft, inRight, outLeft, outRight, buffer.getNumSamples());
}

void BubbleCloudAudioProcessor::handlePerformanceMidi(const juce::MidiBuffer& midiMessages)
{
    const auto* noteValue = treeState.getRawParameterValue("FREEZE_MIDI_NOTE");
    const auto* modeValue = treeState.getRawParameterValue("FREEZE_MIDI_MODE");
    if (noteValue == nullptr || modeValue == nullptr)
        return;

    const int triggerNote = juce::roundToInt(juce::jlimit(0.0f, 127.0f, noteValue->load()));
    const bool momentary = modeValue->load() >= 0.5f;

    for (const auto metadata : midiMessages) {
        const auto message = metadata.getMessage();
        if (!message.isNoteOnOrOff() || message.getNoteNumber() != triggerNote)
            continue;

        if (message.isNoteOn()) {
            midiFreezeActive.store(momentary ? true : !midiFreezeActive.load());
        } else if (message.isNoteOff() && momentary) {
            midiFreezeActive.store(false);
        }
    }
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

void BubbleCloudAudioProcessor::initialiseScenesFromParameters()
{
    for (std::size_t i = 0; i < sceneParameterCount; ++i) {
        float value = 0.0f;
        if (const auto* parameter = treeState.getRawParameterValue(sceneParameterIds[i]))
            value = parameter->load();
        sceneA[i].store(value);
        sceneB[i].store(value);
    }
    sceneApplicationDirty.store(true);
}

void BubbleCloudAudioProcessor::captureScene(int sceneIndex)
{
    if (sceneIndex != 0 && sceneIndex != 1)
        return;

    auto& destination = sceneIndex == 0 ? sceneA : sceneB;
    for (std::size_t i = 0; i < sceneParameterCount; ++i) {
        if (const auto* parameter = treeState.getRawParameterValue(sceneParameterIds[i]))
            destination[i].store(parameter->load());
    }
    sceneApplicationDirty.store(true);
}

void BubbleCloudAudioProcessor::setCaptureHeld(bool shouldHold) noexcept
{
    captureHeld.store(shouldHold);
}

bool BubbleCloudAudioProcessor::isFreezeActive() const noexcept
{
    return effectiveFreezeActive.load();
}

BubbleCloudTelemetry BubbleCloudAudioProcessor::getTelemetrySnapshot() noexcept
{
    auto snapshot = engineWrapper.getTelemetrySnapshot();
    snapshot.frozen = isFreezeActive();
    return snapshot;
}

float BubbleCloudAudioProcessor::getMorphedParameterValue(const juce::String& parameterID) const
{
    const int index = findSceneParameterIndex(parameterID);
    if (index < 0)
        return std::numeric_limits<float>::quiet_NaN();

    const auto sceneIndex = (std::size_t)index;
    const float a = sceneA[sceneIndex].load();
    const float b = sceneB[sceneIndex].load();
    if (sceneParameterIsContinuous(sceneIndex))
        return morphedContinuousValue(sceneIndex, a, b, morphTarget.load());
    return discreteMorphScene.load() == 0 ? a : b;
}

void BubbleCloudAudioProcessor::updateSceneEndpoint(const juce::String& parameterID, float value)
{
    const int index = findSceneParameterIndex(parameterID);
    if (index < 0)
        return;

    auto& destination = endpointEditScene.load() == 0 ? sceneA : sceneB;
    destination[(std::size_t)index].store(value);
    sceneApplicationDirty.store(true);
}

void BubbleCloudAudioProcessor::applySceneMorph()
{
    if (sceneApplicationDirty.exchange(false))
        lastAppliedSceneValues.fill(std::numeric_limits<float>::quiet_NaN());

    const float morph = juce::jlimit(0.0f, 1.0f, morphTarget.load());
    int discreteScene = discreteMorphScene.load();
    if (morph >= 0.55f)
        discreteScene = 1;
    else if (morph <= 0.45f)
        discreteScene = 0;
    discreteMorphScene.store(discreteScene);

    for (std::size_t i = 0; i < sceneParameterCount; ++i) {
        const float a = sceneA[i].load();
        const float b = sceneB[i].load();
        const float value = sceneParameterIsContinuous(i)
            ? morphedContinuousValue(i, a, b, morph)
            : (discreteScene == 0 ? a : b);

        if (std::isfinite(lastAppliedSceneValues[i])
            && std::abs(value - lastAppliedSceneValues[i]) < 0.0001f)
            continue;

        lastAppliedSceneValues[i] = value;
        if (juce::String(sceneParameterIds[i]) == "FREEZE")
            sceneFreezeValue.store(value);
        else
            forwardParameterToEngine(sceneParameterIds[i], value);
    }
}

void BubbleCloudAudioProcessor::applyEffectiveFreeze()
{
    const float sceneValue = juce::jlimit(0.0f, 1.0f, sceneFreezeValue.load());
    const bool performanceOverride = captureHeld.load() || midiFreezeActive.load();
    bool freezeActive = effectiveFreezeActive.load();
    if (performanceOverride)
        freezeActive = true;
    else if (freezeActive && sceneValue <= 0.45f)
        freezeActive = false;
    else if (!freezeActive && sceneValue >= 0.55f)
        freezeActive = true;
    effectiveFreezeActive.store(freezeActive);

    const float effectiveValue = freezeActive ? 1.0f : 0.0f;

    if (std::abs(effectiveValue - lastAppliedFreeze) >= 0.0001f) {
        engineWrapper.setParameter(BUBBLE_PARAM_FREEZE, effectiveValue);
        lastAppliedFreeze = effectiveValue;
    }
}

void BubbleCloudAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = treeState.copyState();
    if (const auto previousScenes = state.getChildWithName("PERFORMANCE_SCENES"); previousScenes.isValid())
        state.removeChild(previousScenes, nullptr);
    juce::ValueTree scenes("PERFORMANCE_SCENES");
    scenes.setProperty("version", 1, nullptr);
    for (std::size_t i = 0; i < sceneParameterCount; ++i) {
        scenes.setProperty("a" + juce::String((int)i), sceneA[i].load(), nullptr);
        scenes.setProperty("b" + juce::String((int)i), sceneB[i].load(), nullptr);
    }
    state.addChild(scenes, -1, nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void BubbleCloudAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState == nullptr || !xmlState->hasTagName(treeState.state.getType()))
        return;

    auto restoredState = juce::ValueTree::fromXml(*xmlState);
    treeState.replaceState(restoredState);
    if (const auto* morph = treeState.getRawParameterValue("MORPH")) {
        const float value = juce::jlimit(0.0f, 1.0f, morph->load());
        morphTarget.store(value);
        endpointEditScene.store(value >= 0.5f ? 1 : 0);
        discreteMorphScene.store(value >= 0.5f ? 1 : 0);
    }

    const auto scenes = restoredState.getChildWithName("PERFORMANCE_SCENES");
    if (scenes.isValid()) {
        for (std::size_t i = 0; i < sceneParameterCount; ++i) {
            const auto aName = "a" + juce::String((int)i);
            const auto bName = "b" + juce::String((int)i);
            if (scenes.hasProperty(aName))
                sceneA[i].store((float)scenes[aName]);
            if (scenes.hasProperty(bName))
                sceneB[i].store((float)scenes[bName]);
        }
    } else {
        initialiseScenesFromParameters();
    }

    midiFreezeActive.store(false);
    captureHeld.store(false);
    sceneApplicationDirty.store(true);
}

void BubbleCloudAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == "MORPH") {
        const float value = juce::jlimit(0.0f, 1.0f, newValue);
        morphTarget.store(value);
        int editingScene = endpointEditScene.load();
        if (value >= 0.55f)
            editingScene = 1;
        else if (value <= 0.45f)
            editingScene = 0;
        endpointEditScene.store(editingScene);
        return;
    }

    if (parameterID == "FREEZE_MIDI_MODE" || parameterID == "FREEZE_MIDI_NOTE")
        return;

    updateSceneEndpoint(parameterID, newValue);
}

void BubbleCloudAudioProcessor::forwardParameterToEngine(const juce::String& parameterID, float newValue)
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
