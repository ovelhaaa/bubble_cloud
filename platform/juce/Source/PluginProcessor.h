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
    float getEffectiveFreeze() const noexcept;

#if defined(BUBBLES_BUILD_PROCESSOR_TESTS)
    // Test-only observation of the host transport fallback state. Compiled out
    // of production builds (the macro is set only when the test target is on).
    struct TransportTestState
    {
        double lastValidHostBpm = 120.0;
        double expectedNextPpq = 0.0;
        bool hasExpectedNextPpq = false;
        bool wasTransportPlaying = false;
        int syncRhythmPhaseCalls = 0;
    };
    TransportTestState getTransportTestState() const noexcept;

    // M3.2B: observe that the requested quality profile reached the shared DSP
    // core and that the expected interpolation path actually executed.
    int getEngineInterpolationMode() const noexcept;
    void getEngineInterpolationCallCounts(unsigned long long& linearSamples,
                                          unsigned long long& hermiteSamples) const noexcept;

    // M3.2C: observe the voice limit actually resolved by the global quality
    // preference in the shared DSP core (per engine, not the summed stereo bus).
    int getEngineActiveVoiceLimit() const noexcept;
#endif

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

    // M3.2C: QUALITY_PROFILE is an instance-global preference, deliberately kept
    // out of the scene morph. This re-applies it to the shared DSP core from the
    // value tree every block so a fresh instance, a factory preset, a manual
    // change and a DAW state restore are all honoured.
    void applyGlobalPreferences();

    // QUALITY_PROFILE no longer participates in Scene A/B (M3.2C), so the active
    // scene list is 18 parameters. The persisted PERFORMANCE_SCENES slot layout is
    // kept identical to the historical 19-slot order (slot 12 reserved for the
    // legacy quality entry) via scenePersistenceSlots so old states still restore
    // every real parameter and the legacy quality slot is ignored on read.
    static constexpr std::size_t sceneParameterCount = 18;
    
    BubbleCloudEngineWrapper engineWrapper;
    double expectedNextPpq = 0.0;
    bool hasExpectedNextPpq = false;
    bool wasTransportPlaying = false;
    double lastValidHostBpm = 120.0;
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
    float lastAppliedQuality = -1.0f;

#if defined(BUBBLES_BUILD_PROCESSOR_TESTS)
    int transportSyncRhythmPhaseCalls = 0;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BubbleCloudAudioProcessor)
};
