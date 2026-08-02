#pragma once

#include <array>
#include <atomic>
#include <cstddef>

#include <juce_audio_processors/juce_audio_processors.h>
#include "BubbleCloudEngineWrapper.h"

class BubbleCloudAudioProcessor : public juce::AudioProcessor, public juce::AudioProcessorValueTreeState::Listener
{
public:
    BubbleCloudAudioProcessor();
    ~BubbleCloudAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override { return "Bubbles"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int index) override {}
    const juce::String getProgramName (int index) override { return {}; }
    void changeProgramName (int index, const juce::String& newName) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    void captureScene(int sceneIndex);
    void setCaptureHeld(bool shouldHold) noexcept;
    bool isFreezeActive() const noexcept;
    BubbleCloudTelemetry getTelemetrySnapshot() noexcept;
    float getMorphedParameterValue(const juce::String& parameterID) const;

    juce::AudioProcessorValueTreeState treeState;

private:
    static BusesProperties createBusesProperties();
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateHostTransport(int numSamples);
    void handlePerformanceMidi(const juce::MidiBuffer& midiMessages);
    void initialiseScenesFromParameters();
    void updateSceneEndpoint(const juce::String& parameterID, float value);
    void applySceneMorph();
    void forwardParameterToEngine(const juce::String& parameterID, float value);
    void applyEffectiveFreeze();

    static constexpr std::size_t sceneParameterCount = 19;
    
    BubbleCloudEngineWrapper engineWrapper;
    double expectedNextPpq = 0.0;
    bool hasExpectedNextPpq = false;
    bool wasTransportPlaying = false;
    std::array<std::atomic<float>, sceneParameterCount> sceneA;
    std::array<std::atomic<float>, sceneParameterCount> sceneB;
    std::array<float, sceneParameterCount> lastAppliedSceneValues {};
    std::atomic<float> morphTarget { 0.0f };
    std::atomic<float> sceneFreezeValue { 0.0f };
    std::atomic<int> endpointEditScene { 0 };
    std::atomic<int> discreteMorphScene { 0 };
    std::atomic<bool> captureHeld { false };
    std::atomic<bool> effectiveFreezeActive { false };
    std::atomic<bool> sceneApplicationDirty { true };
    std::atomic<bool> midiFreezeActive { false };
    float lastAppliedFreeze = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BubbleCloudAudioProcessor)
};
