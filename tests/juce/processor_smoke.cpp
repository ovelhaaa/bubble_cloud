#include "PluginProcessor.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

// Processors contain large inline DSP buffers. Keep them on the heap so
// nested test calls fit within the default Windows executable stack.
namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void setParameter(BubbleCloudAudioProcessor& processor, const char* parameterId, float value)
    {
        auto* parameter = processor.treeState.getParameter(parameterId);
        require(parameter != nullptr, "missing APVTS parameter");
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    }

    // Minimal scriptable host playhead so the transport fallback logic can be
    // driven deterministically without a DAW wrapper.
    class FakePlayHead : public juce::AudioPlayHead
    {
    public:
        juce::Optional<PositionInfo> getPosition() const override { return position; }

        PositionInfo position;
    };

    void processSilence(BubbleCloudAudioProcessor& processor, const juce::MidiBuffer& midi)
    {
        juce::AudioBuffer<float> buffer(2, 128);
        buffer.clear();
        auto mutableMidi = midi;
        processor.processBlock(buffer, mutableMidi);
    }

    struct RenderMetrics
    {
        double rmsLeft = 0.0;
        double rmsRight = 0.0;
        double rmsMono = 0.0;
        double rmsSide = 0.0;
        double correlation = 0.0;
        double peak = 0.0;
        double renderSeconds = 0.0;
    };

    RenderMetrics renderMusicalProbe(BubbleCloudAudioProcessor& processor,
                                     double sampleRate,
                                     int blockSize,
                                     double durationSeconds)
    {
        const int64_t totalSamples = (int64_t)std::ceil(sampleRate * durationSeconds);
        double sumLeft = 0.0;
        double sumRight = 0.0;
        double sumMono = 0.0;
        double sumSide = 0.0;
        double sumCross = 0.0;
        double peak = 0.0;
        int64_t measuredSamples = 0;
        juce::MidiBuffer noMidi;
        const auto started = std::chrono::steady_clock::now();

        for (int64_t offset = 0; offset < totalSamples; offset += blockSize) {
            const int frames = (int)std::min<int64_t>(blockSize, totalSamples - offset);
            juce::AudioBuffer<float> buffer(2, frames);
            for (int i = 0; i < frames; ++i) {
                const double time = (double)(offset + i) / sampleRate;
                const double phrase = std::fmod(time, 0.25) / 0.25;
                const double envelope = std::exp(-5.5 * phrase);
                const float left = (float)(0.24 * envelope
                    * (std::sin(juce::MathConstants<double>::twoPi * 220.0 * time)
                       + 0.45 * std::sin(juce::MathConstants<double>::twoPi * 329.63 * time)));
                const float right = (float)(0.22 * envelope
                    * (std::sin(juce::MathConstants<double>::twoPi * 220.0 * time + 0.035)
                       + 0.45 * std::sin(juce::MathConstants<double>::twoPi * 277.18 * time)));
                buffer.setSample(0, i, left);
                buffer.setSample(1, i, right);
            }

            processor.processBlock(buffer, noMidi);
            for (int i = 0; i < frames; ++i) {
                const double left = buffer.getSample(0, i);
                const double right = buffer.getSample(1, i);
                require(std::isfinite(left) && std::isfinite(right),
                        "processor emitted a non-finite sample");
                const double mono = 0.5 * (left + right);
                const double side = 0.5 * (left - right);
                sumLeft += left * left;
                sumRight += right * right;
                sumMono += mono * mono;
                sumSide += side * side;
                sumCross += left * right;
                peak = std::max(peak, std::max(std::abs(left), std::abs(right)));
            }
            measuredSamples += frames;
        }

        const auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        const double safeCount = (double)std::max<int64_t>(1, measuredSamples);
        RenderMetrics metrics;
        metrics.rmsLeft = std::sqrt(sumLeft / safeCount);
        metrics.rmsRight = std::sqrt(sumRight / safeCount);
        metrics.rmsMono = std::sqrt(sumMono / safeCount);
        metrics.rmsSide = std::sqrt(sumSide / safeCount);
        metrics.correlation = sumCross / std::sqrt(std::max(1.0e-18, sumLeft * sumRight));
        metrics.peak = peak;
        metrics.renderSeconds = elapsed;
        return metrics;
    }

    void testSampleRateAndBlockSizeMatrix()
    {
        constexpr std::array<double, 4> sampleRates {{ 44100.0, 48000.0, 88200.0, 96000.0 }};
        constexpr std::array<int, 6> blockSizes {{ 32, 64, 127, 256, 512, 2048 }};

        for (const double sampleRate : sampleRates) {
            for (const int blockSize : blockSizes) {
                auto processorOwner = std::make_unique<BubbleCloudAudioProcessor>();
                auto& processor = *processorOwner;
                processor.setRateAndBufferSizeDetails(sampleRate, blockSize);
                processor.prepareToPlay(sampleRate, blockSize);
                const auto metrics = renderMusicalProbe(
                    processor, sampleRate, blockSize + 17, 0.035);
                require(metrics.rmsLeft > 0.001 && metrics.rmsRight > 0.001,
                        "sample-rate/block-size matrix produced silence");
                require(metrics.peak <= 0.9,
                        "sample-rate/block-size matrix exceeded the final limiter ceiling");
            }
        }
    }

    double renderLeftOnlyEnergy(BubbleCloudAudioProcessor& processor, int blocks, float amplitude)
    {
        constexpr int blockFrames = 256;
        double rightEnergy = 0.0;
        juce::MidiBuffer noMidi;
        for (int block = 0; block < blocks; ++block) {
            juce::AudioBuffer<float> buffer(2, blockFrames);
            buffer.clear();
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample(0, i, amplitude);
            processor.processBlock(buffer, noMidi);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                rightEnergy += std::abs(buffer.getSample(1, i));
        }
        return rightEnergy / std::max(1, blocks * blockFrames);
    }

    void testStereoContractKeepsDryLocalAndLetsWetCrossChannel()
    {
        auto processorOwner = std::make_unique<BubbleCloudAudioProcessor>();
        auto& processor = *processorOwner;
        processor.setRateAndBufferSizeDetails(48000.0, 128);
        processor.prepareToPlay(48000.0, 128);

        // MIX fully dry: dry must stay on its own channel (no dry leakage to R).
        setParameter(processor, "MIX", 0.0f);
        setParameter(processor, "SPACE", 1.0f);
        renderLeftOnlyEnergy(processor, 64, 0.0f); // let macro smoothing settle
        const double dryRightEnergy = renderLeftOnlyEnergy(processor, 8, 0.5f);
        require(dryRightEnergy < 1.0e-5,
                "dry-only left probe leaked into the right output channel");

        // MIX fully wet: the core spatial field must now reach the right channel.
        setParameter(processor, "MIX", 1.0f);
        renderLeftOnlyEnergy(processor, 64, 0.0f); // let macro smoothing settle
        const double wetRightEnergy = renderLeftOnlyEnergy(processor, 32, 0.5f);
        require(wetRightEnergy > 1.0e-4,
                "left-only probe produced no spatially panned wet energy on the right");
    }

    // Identical L/R input, so the side channel is produced only by the engine's
    // spatial wet field (dry contributes no side energy).
    double renderMonoWidthProbe(BubbleCloudAudioProcessor& processor, double sampleRate, double seconds)
    {
        constexpr int blockSize = 256;
        const int64_t total = (int64_t)std::ceil(sampleRate * seconds);
        double sumSide = 0.0;
        double sumMid = 0.0;
        juce::MidiBuffer noMidi;

        for (int64_t offset = 0; offset < total; offset += blockSize) {
            const int frames = (int)std::min<int64_t>(blockSize, total - offset);
            juce::AudioBuffer<float> buffer(2, frames);
            for (int i = 0; i < frames; ++i) {
                const double time = (double)(offset + i) / sampleRate;
                const double phrase = std::fmod(time, 0.25) / 0.25;
                const double envelope = std::exp(-5.5 * phrase);
                const float sample = (float)(0.24 * envelope
                    * (std::sin(juce::MathConstants<double>::twoPi * 220.0 * time)
                       + 0.45 * std::sin(juce::MathConstants<double>::twoPi * 329.63 * time)));
                buffer.setSample(0, i, sample);
                buffer.setSample(1, i, sample);
            }
            processor.processBlock(buffer, noMidi);
            for (int i = 0; i < frames; ++i) {
                const double l = (double)buffer.getSample(0, i);
                const double r = (double)buffer.getSample(1, i);
                const double side = 0.5 * (l - r);
                const double mid = 0.5 * (l + r);
                sumSide += side * side;
                sumMid += mid * mid;
            }
        }
        return std::sqrt(sumSide) / std::sqrt(std::max(1.0e-18, sumMid));
    }

    void testSpaceMacroChangesStereoWidth()
    {
        auto narrowOwner = std::make_unique<BubbleCloudAudioProcessor>();
        auto& narrow = *narrowOwner;
        narrow.setRateAndBufferSizeDetails(48000.0, 256);
        narrow.prepareToPlay(48000.0, 256);
        setParameter(narrow, "SPACE", 0.0f);
        setParameter(narrow, "MIX", 1.0f);
        renderMonoWidthProbe(narrow, 48000.0, 0.2); // settle macros
        const double narrowWidth = renderMonoWidthProbe(narrow, 48000.0, 0.8);

        auto wideOwner = std::make_unique<BubbleCloudAudioProcessor>();
        auto& wide = *wideOwner;
        wide.setRateAndBufferSizeDetails(48000.0, 256);
        wide.prepareToPlay(48000.0, 256);
        setParameter(wide, "SPACE", 1.0f);
        setParameter(wide, "MIX", 1.0f);
        renderMonoWidthProbe(wide, 48000.0, 0.2);
        const double wideWidth = renderMonoWidthProbe(wide, 48000.0, 0.8);

        std::cout << "SPACE width narrow=" << narrowWidth << " wide=" << wideWidth
                  << " ratio=" << (wideWidth / std::max(1.0e-9, narrowWidth)) << '\n';
        require(wideWidth > narrowWidth * 1.5,
                "SPACE=1 did not produce a measurably wider stereo field than SPACE=0");
    }

    void testHostTempoFallbackSurvivesMissingBpm()
    {
        // Simulates a real host transport: playing at 90 BPM with valid PPQ, a
        // temporary BPM loss while PPQ keeps advancing, then BPM reappearing.
        // The fallback must keep 90 BPM, advance expectedNextPpq at 90 BPM and
        // never mistake the missing BPM for a transport jump.
        auto processorOwner = std::make_unique<BubbleCloudAudioProcessor>();
        auto& processor = *processorOwner;
        processor.setRateAndBufferSizeDetails(48000.0, 512);
        processor.prepareToPlay(48000.0, 512);
        setParameter(processor, "TEMPO_SYNC", 1.0f);

        FakePlayHead playHead;
        processor.setPlayHead(&playHead);

        constexpr int blockSize = 512;
        constexpr double bpm = 90.0;
        const double blockQuarterNotes = ((double)blockSize / 48000.0) * (bpm / 60.0);
        double ppq = 0.0;

        auto processBlockAdvancing = [&](bool provideBpm) {
            playHead.position.setIsPlaying(true);
            if (provideBpm)
                playHead.position.setBpm(bpm);
            else
                playHead.position.setBpm(juce::Optional<double>{});
            playHead.position.setPpqPosition(ppq);
            juce::AudioBuffer<float> buffer(2, blockSize);
            buffer.clear();
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);
            ppq += blockQuarterNotes;
        };

        processBlockAdvancing(true);
        auto state = processor.getTransportTestState();
        require(state.syncRhythmPhaseCalls == 1,
                "the first playing block did not phase-sync exactly once");
        require(std::abs(state.lastValidHostBpm - bpm) < 1e-6,
                "the 90 BPM host value was not captured");
        require(state.hasExpectedNextPpq
                    && std::abs(state.expectedNextPpq - blockQuarterNotes) < 1e-9,
                "expectedNextPpq was not seeded from the first block");

        for (int i = 0; i < 24; ++i)
            processBlockAdvancing(true);
        state = processor.getTransportTestState();
        require(state.syncRhythmPhaseCalls == 1,
                "a steady 90 BPM transport caused a false transport-jump resync");
        require(std::abs(state.lastValidHostBpm - bpm) < 1e-6,
                "lastValidHostBpm drifted during steady playback");

        const double ppqBeforeAbsence = state.expectedNextPpq;
        for (int i = 0; i < 24; ++i)
            processBlockAdvancing(false); // BPM missing, PPQ still valid
        state = processor.getTransportTestState();
        require(std::abs(state.lastValidHostBpm - bpm) < 1e-6,
                "lastValidHostBpm did not survive the missing host BPM");
        require(state.syncRhythmPhaseCalls == 1,
                "a missing host BPM was mistaken for a transport jump");
        const double absenceAdvance = state.expectedNextPpq - ppqBeforeAbsence;
        require(std::abs(absenceAdvance - 24.0 * blockQuarterNotes) < 1e-6,
                "expectedNextPpq did not advance at the retained 90 BPM");

        for (int i = 0; i < 8; ++i)
            processBlockAdvancing(true); // BPM reappears
        state = processor.getTransportTestState();
        require(std::abs(state.lastValidHostBpm - bpm) < 1e-6,
                "lastValidHostBpm changed when the BPM reappeared unchanged");
        require(state.syncRhythmPhaseCalls == 1,
                "BPM reappearance triggered an unnecessary resync");

        ppq += 4.0; // genuine transport jump
        processBlockAdvancing(true);
        state = processor.getTransportTestState();
        require(state.syncRhythmPhaseCalls == 2,
                "a genuine transport jump did not resync exactly once");

        playHead.position.setIsPlaying(false);
        playHead.position.setPpqPosition(juce::Optional<double>{});
        juce::AudioBuffer<float> buffer(2, blockSize);
        buffer.clear();
        juce::MidiBuffer midi;
        processor.processBlock(buffer, midi);
        state = processor.getTransportTestState();
        require(!state.hasExpectedNextPpq, "stopping the transport did not clear the PPQ latch");
        require(std::abs(state.lastValidHostBpm - bpm) < 1e-6,
                "stopping the transport discarded the last valid BPM");
    }

    // M3.2B: the JUCE path must actually reach the shared core interpolator. The
    // quality profile is only a selector; this proves WEB_* executes Hermite and
    // MCU_* executes linear through PluginProcessor -> EngineWrapper -> DSP.
    void testQualityProfileSelectsInterpolationPath()
    {
        const auto render = [](BubbleCloudAudioProcessor& processor) {
            (void)renderMusicalProbe(processor, 48000.0, 128, 0.3);
        };

        auto processorOwner = std::make_unique<BubbleCloudAudioProcessor>();
        auto& processor = *processorOwner;
        processor.setRateAndBufferSizeDetails(48000.0, 128);
        processor.prepareToPlay(48000.0, 128);

        // A fresh instance must open at the highest tier: Ultra (3), which maps
        // to WEB_ULTRA / Hermite in the shared core.
        const auto* defaultQuality = processor.treeState.getRawParameterValue("QUALITY_PROFILE");
        require(defaultQuality != nullptr && std::abs(defaultQuality->load() - 3.0f) < 0.001f,
                "a fresh VST instance must default to the Ultra quality profile");
        render(processor);
        require(processor.getEngineInterpolationMode() == 1,
                "the default Ultra profile did not activate the Hermite path");

        // WEB_STANDARD: Hermite selected and executed, linear untouched.
        setParameter(processor, "QUALITY_PROFILE", 2.0f);
        render(processor);
        require(processor.getEngineInterpolationMode() == 1,
                "WEB_STANDARD did not select the Hermite interpolator");
        unsigned long long linearBefore = 0;
        unsigned long long hermiteBefore = 0;
        processor.getEngineInterpolationCallCounts(linearBefore, hermiteBefore);
        require(hermiteBefore > 0, "WEB_STANDARD did not execute the Hermite path");
        require(linearBefore == 0, "WEB_STANDARD unexpectedly executed the linear path");

        // WEB_ULTRA: still Hermite.
        setParameter(processor, "QUALITY_PROFILE", 3.0f);
        render(processor);
        require(processor.getEngineInterpolationMode() == 1,
                "WEB_ULTRA did not select the Hermite interpolator");
        unsigned long long linearUltra = 0;
        unsigned long long hermiteUltra = 0;
        processor.getEngineInterpolationCallCounts(linearUltra, hermiteUltra);
        require(hermiteUltra > hermiteBefore, "WEB_ULTRA did not execute the Hermite path");
        require(linearUltra == 0, "WEB_ULTRA unexpectedly executed the linear path");

        // MCU_SAFE: linear selected and executed; Hermite counter must not grow.
        setParameter(processor, "QUALITY_PROFILE", 0.0f);
        render(processor);
        require(processor.getEngineInterpolationMode() == 0,
                "MCU_SAFE did not select the linear interpolator");
        unsigned long long linearSafe = 0;
        unsigned long long hermiteSafe = 0;
        processor.getEngineInterpolationCallCounts(linearSafe, hermiteSafe);
        require(linearSafe > 0, "MCU_SAFE did not execute the linear path");
        require(hermiteSafe == hermiteUltra,
                "MCU_SAFE must not execute the Hermite path after downgrade");

        // MCU_PLUS: still linear.
        setParameter(processor, "QUALITY_PROFILE", 1.0f);
        render(processor);
        require(processor.getEngineInterpolationMode() == 0,
                "MCU_PLUS did not select the linear interpolator");
        unsigned long long linearPlus = 0;
        unsigned long long hermitePlus = 0;
        processor.getEngineInterpolationCallCounts(linearPlus, hermitePlus);
        require(linearPlus > linearSafe, "MCU_PLUS did not execute the linear path");
        require(hermitePlus == hermiteUltra,
                "MCU_PLUS must not execute the Hermite path");

        std::cout << "interpolation path: WEB hermite_samples=" << hermiteUltra
                  << ", MCU linear_samples=" << linearPlus << '\n';
    }

    // M3.2C: QUALITY_PROFILE is an instance-global preference, not a scene
    // parameter. Morph, Scene A/B capture and scene-endpoint edits must never
    // change it, while a fresh instance, a factory preset, a manual change and a
    // DAW state restore must all reach the shared DSP core.
    void testQualityProfileIsGlobalNotSceneMorph()
    {
        const auto currentQuality = [](BubbleCloudAudioProcessor& p) {
            const auto* q = p.treeState.getRawParameterValue("QUALITY_PROFILE");
            return q != nullptr ? q->load() : -1.0f;
        };
        const auto render = [](BubbleCloudAudioProcessor& p) {
            juce::MidiBuffer noMidi;
            juce::AudioBuffer<float> buffer(2, 128);
            for (int block = 0; block < 16; ++block) {
                buffer.clear();
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample(0, i, 0.25f);
                p.processBlock(buffer, noMidi);
            }
        };

        auto processorOwner = std::make_unique<BubbleCloudAudioProcessor>();
        auto& processor = *processorOwner;
        processor.setRateAndBufferSizeDetails(48000.0, 128);
        processor.prepareToPlay(48000.0, 128);

        // A. Fresh instance must be Ultra / Hermite / 32 voices.
        require(std::abs(currentQuality(processor) - 3.0f) < 0.001f,
                "fresh VST instance must default to Ultra quality");
        render(processor);
        require(processor.getEngineInterpolationMode() == 1,
                "fresh VST instance did not run the Hermite path");
        require(processor.getEngineActiveVoiceLimit() == 32,
                "fresh VST instance did not resolve the 32-voice Ultra limit");

        // Build two contrasting scenes, then morph 0 -> 1 -> 0 and prove quality
        // is untouched by scene capture or morphing.
        setParameter(processor, "MORPH", 0.0f);
        setParameter(processor, "DENSITY", 0.2f);
        setParameter(processor, "MIX", 0.2f);
        processor.captureScene(0);
        setParameter(processor, "MORPH", 1.0f);
        setParameter(processor, "DENSITY", 0.8f);
        setParameter(processor, "MIX", 0.8f);
        processor.captureScene(1);
        setParameter(processor, "MORPH", 0.56f);
        render(processor);
        setParameter(processor, "MORPH", 1.0f);
        render(processor);
        setParameter(processor, "MORPH", 0.0f);
        render(processor);
        require(std::abs(currentQuality(processor) - 3.0f) < 0.001f,
                "morphing scenes changed the global quality profile");
        require(processor.getEngineInterpolationMode() == 1,
                "morphing scenes left the Hermite interpolator");
        require(processor.getEngineActiveVoiceLimit() == 32,
                "morphing scenes changed the Ultra voice limit");

        // B. A manual lower tier must survive scene capture and morphing.
        setParameter(processor, "QUALITY_PROFILE", 1.0f); // Balanced / MCU_PLUS
        render(processor);
        require(std::abs(currentQuality(processor) - 1.0f) < 0.001f,
                "manual Balanced quality did not stick");
        require(processor.getEngineInterpolationMode() == 0,
                "Balanced quality did not select the linear interpolator");
        require(processor.getEngineActiveVoiceLimit() == 16,
                "Balanced quality did not resolve the 16-voice limit");
        processor.captureScene(0);
        processor.captureScene(1);
        setParameter(processor, "MORPH", 1.0f);
        render(processor);
        setParameter(processor, "MORPH", 0.0f);
        render(processor);
        require(std::abs(currentQuality(processor) - 1.0f) < 0.001f,
                "scene capture changed a manually chosen lower quality tier");
        require(processor.getEngineInterpolationMode() == 0,
                "scene capture reset the Balanced linear path");
        require(processor.getEngineActiveVoiceLimit() == 16,
                "scene capture reset the Balanced voice limit");

        // C. A DAW state restore must round-trip the user's choice (Studio).
        setParameter(processor, "QUALITY_PROFILE", 2.0f); // Studio / WEB_STANDARD
        render(processor);
        juce::MemoryBlock state;
        processor.getStateInformation(state);

        auto restoredOwner = std::make_unique<BubbleCloudAudioProcessor>();
        auto& restored = *restoredOwner;
        restored.setRateAndBufferSizeDetails(48000.0, 128);
        restored.prepareToPlay(48000.0, 128);
        restored.setStateInformation(state.getData(), (int)state.getSize());
        render(restored);
        require(std::abs(currentQuality(restored) - 2.0f) < 0.001f,
                "state restore did not restore the Studio quality profile");
        require(restored.getEngineInterpolationMode() == 1,
                "restored Studio quality did not run Hermite");
        require(restored.getEngineActiveVoiceLimit() == 24,
                "restored Studio quality did not resolve the 24-voice limit");

        // D. Selecting a factory preset must land on Ultra / Hermite.
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(restored.createEditor());
            require(editor != nullptr, "quality test could not create an editor");
            juce::ComboBox* presetBox = nullptr;
            for (int i = 0; i < editor->getNumChildComponents(); ++i) {
                auto* candidate = dynamic_cast<juce::ComboBox*>(editor->getChildComponent(i));
                if (candidate != nullptr && candidate->getNumItems() == 20) {
                    presetBox = candidate;
                    break;
                }
            }
            require(presetBox != nullptr, "quality test could not find the factory preset selector");
            presetBox->setSelectedItemIndex(7, juce::sendNotificationSync);
            render(restored);
            require(std::abs(currentQuality(restored) - 3.0f) < 0.001f,
                    "loading a factory preset did not select Ultra quality");
            require(restored.getEngineInterpolationMode() == 1,
                    "loading a factory preset did not run the Hermite path");
            require(restored.getEngineActiveVoiceLimit() == 32,
                    "loading a factory preset did not resolve the Ultra voice limit");
        }

        std::cout << "quality global: fresh/preset/morph/restore verified\n";
    }

    // M3.2C: an old state that still stores QUALITY_PROFILE inside
    // PERFORMANCE_SCENES must restore every real parameter and safely ignore the
    // legacy quality slot, while the global APVTS quality survives untouched.
    void testLegacySceneStateIgnoresQualitySlot()
    {
        const auto readMorphed = [](BubbleCloudAudioProcessor& p, const char* id) {
            return p.getMorphedParameterValue(id);
        };

        auto sourceOwner = std::make_unique<BubbleCloudAudioProcessor>();
        auto& source = *sourceOwner;
        source.setRateAndBufferSizeDetails(48000.0, 128);
        source.prepareToPlay(48000.0, 128);
        setParameter(source, "QUALITY_PROFILE", 2.0f); // Studio, must survive legacy slot
        setParameter(source, "MORPH", 0.0f);
        juce::MidiBuffer noMidi;
        {
            juce::AudioBuffer<float> buffer(2, 128);
            buffer.clear();
            source.processBlock(buffer, noMidi);
        }
        juce::MemoryBlock newState;
        source.getStateInformation(newState);

        // Rewrite PERFORMANCE_SCENES using the historical 19-slot layout:
        // slot 12 = legacy QUALITY_PROFILE, slot 14 = RHYTHM_DIVISION.
        std::unique_ptr<juce::XmlElement> xml(juce::AudioProcessor::getXmlFromBinary(
            newState.getData(), (int)newState.getSize()));
        require(xml != nullptr, "legacy test could not parse the saved state");
        auto tree = juce::ValueTree::fromXml(*xml);
        if (const auto previous = tree.getChildWithName("PERFORMANCE_SCENES"); previous.isValid())
            tree.removeChild(previous, nullptr);
        juce::ValueTree legacyScenes("PERFORMANCE_SCENES");
        legacyScenes.setProperty("version", 1, nullptr);
        for (int slot = 0; slot <= 18; ++slot) {
            legacyScenes.setProperty("a" + juce::String(slot), 0.0f, nullptr);
            legacyScenes.setProperty("b" + juce::String(slot), 0.0f, nullptr);
        }
        legacyScenes.setProperty("a12", 1.0f, nullptr); // legacy quality -> ignored
        legacyScenes.setProperty("b12", 1.0f, nullptr);
        legacyScenes.setProperty("a14", 3.0f, nullptr); // old RHYTHM_DIVISION slot
        legacyScenes.setProperty("b14", 3.0f, nullptr);
        tree.addChild(legacyScenes, -1, nullptr);

        std::unique_ptr<juce::XmlElement> legacyXml(tree.createXml());
        juce::MemoryBlock legacyState;
        juce::AudioProcessor::copyXmlToBinary(*legacyXml, legacyState);

        auto restoredOwner = std::make_unique<BubbleCloudAudioProcessor>();
        auto& restored = *restoredOwner;
        restored.setRateAndBufferSizeDetails(48000.0, 128);
        restored.prepareToPlay(48000.0, 128);
        restored.setStateInformation(legacyState.getData(), (int)legacyState.getSize());
        setParameter(restored, "MORPH", 0.0f);
        {
            juce::AudioBuffer<float> buffer(2, 128);
            buffer.clear();
            restored.processBlock(buffer, noMidi);
        }

        const auto* quality = restored.treeState.getRawParameterValue("QUALITY_PROFILE");
        require(quality != nullptr && std::abs(quality->load() - 2.0f) < 0.001f,
                "legacy scene restore overwrote the global quality preference");
        require(std::abs(readMorphed(restored, "RHYTHM_DIVISION") - 3.0f) < 0.001f,
                "legacy 19-slot scene state did not restore a shifted real parameter");
        require(std::isnan(readMorphed(restored, "QUALITY_PROFILE")),
                "QUALITY_PROFILE must not be a morphed scene parameter");
        std::cout << "quality global: legacy 19-slot state ignored the quality slot\n";
    }
}

int main()
{
    try {
        juce::ScopedJuceInitialiser_GUI initialiseJuce;
        auto processorOwner = std::make_unique<BubbleCloudAudioProcessor>();
        auto& processor = *processorOwner;
        processor.setRateAndBufferSizeDetails(48000.0, 128);
        processor.prepareToPlay(48000.0, 128);

        require(processor.acceptsMidi(), "processor must advertise MIDI input");
        require(processor.getTailLengthSeconds() >= 2.0, "granular tail must be reported to the host");

        testSampleRateAndBlockSizeMatrix();
        testStereoContractKeepsDryLocalAndLetsWetCrossChannel();
        testSpaceMacroChangesStereoWidth();
        testHostTempoFallbackSurvivesMissingBpm();
        testQualityProfileSelectsInterpolationPath();
        testQualityProfileIsGlobalNotSceneMorph();
        testLegacySceneStateIgnoresQualitySlot();

        // Left-only probe with spatial settings: the left channel must carry
        // output and telemetry must publish left-engine voices. The wet bus may
        // legitimately cross to the right by spatialization, so right energy is
        // no longer treated as leakage.
        setParameter(processor, "SPACE", 1.0f);
        setParameter(processor, "MIX", 1.0f);
        juce::MidiBuffer noMidi;
        juce::AudioBuffer<float> stereoProbe(2, 128);
        double leftEnergy = 0.0;
        for (int block = 0; block < 16; ++block) {
            stereoProbe.clear();
            for (int i = 0; i < stereoProbe.getNumSamples(); ++i)
                stereoProbe.setSample(0, i, 0.5f);
            processor.processBlock(stereoProbe, noMidi);
            for (int i = 0; i < stereoProbe.getNumSamples(); ++i)
                leftEnergy += std::abs(stereoProbe.getSample(0, i));
        }
        require(leftEnergy > 0.01, "left-only probe produced no left output");

        const auto stereoTelemetry = processor.getTelemetrySnapshot();
        require(stereoTelemetry.peakLeft > 0.01f, "telemetry did not report the left output peak");
        require(stereoTelemetry.activeVoices > 0, "telemetry did not publish active granular voices");
        bool foundLeftVoice = false;
        for (const auto& voice : stereoTelemetry.voices) {
            if (voice.active && voice.channel == 0 && voice.pan < 0.0f) {
                foundLeftVoice = true;
                break;
            }
        }
        require(foundLeftVoice, "telemetry did not publish the left engine voice field");

        setParameter(processor, "TEMPO_SYNC", 1.0f);
        stereoProbe.clear();
        for (int i = 0; i < stereoProbe.getNumSamples(); ++i)
            stereoProbe.setSample(0, i, 0.2f);
        processor.processBlock(stereoProbe, noMidi);
        const auto rhythmTelemetry = processor.getTelemetrySnapshot();
        require(rhythmTelemetry.tempoSync, "telemetry did not report tempo sync");
        require(rhythmTelemetry.rhythmStep >= 0 && rhythmTelemetry.rhythmStep < 16,
                "telemetry rhythm playhead is outside the 16-step pattern");

        // -------------------------------------------------------------------
        // M3.1: Capture/Hold performance override & scene automation
        // -------------------------------------------------------------------
        setParameter(processor, "FREEZE", 0.42f);
        processSilence(processor, noMidi);
        require(std::abs(processor.getEffectiveFreeze() - 0.42f) < 0.001f,
                "initial scene freeze target must be 0.42");
        require(!processor.isFreezeActive(), "override initially inactive");

        // captureHeld = true -> target effectively 1.0
        processor.setCaptureHeld(true);
        processSilence(processor, noMidi);
        require(processor.isFreezeActive(), "held Capture did not engage Freeze");
        require(std::abs(processor.getEffectiveFreeze() - 1.0f) < 0.001f,
                "held Capture must force effective freeze to 1.0");
        require(processor.getTelemetrySnapshot().frozen,
                "telemetry snapshot.frozen must reflect performance override");

        // scene changes to 0.70 while held -> effective freeze continues at 1.0
        setParameter(processor, "FREEZE", 0.70f);
        processSilence(processor, noMidi);
        require(std::abs(processor.getEffectiveFreeze() - 1.0f) < 0.001f,
                "effective freeze must remain 1.0 while scene changes during hold");

        // captureHeld = false -> returns smoothly to 0.70 (not old 0.42!)
        processor.setCaptureHeld(false);
        processSilence(processor, noMidi);
        require(!processor.isFreezeActive(), "releasing Capture did not restore Freeze state");
        require(std::abs(processor.getEffectiveFreeze() - 0.70f) < 0.001f,
                "releasing Capture must restore updated scene freeze value 0.70");
        require(!processor.getTelemetrySnapshot().frozen,
                "telemetry snapshot.frozen must clear when override released");

        // -------------------------------------------------------------------
        // M3.1: Real JUCE MIDI Momentary Mode Validation
        // -------------------------------------------------------------------
        setParameter(processor, "FREEZE", 0.30f);
        setParameter(processor, "FREEZE_MIDI_MODE", 1.0f); // Momentary
        setParameter(processor, "FREEZE_MIDI_NOTE", 60.0f);
        processSilence(processor, noMidi);
        require(std::abs(processor.getEffectiveFreeze() - 0.30f) < 0.001f,
                "scene freeze must be 0.30 before Note On");
        require(!processor.isFreezeActive(), "override initially inactive");

        juce::MidiBuffer noteOn;
        noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
        processSilence(processor, noteOn);
        require(processor.isFreezeActive(), "momentary MIDI note-on did not engage Freeze");
        require(std::abs(processor.getEffectiveFreeze() - 1.0f) < 0.001f,
                "momentary MIDI note-on did not set effective freeze to 1.0");
        require(processor.getTelemetrySnapshot().frozen,
                "telemetry snapshot.frozen must be true on momentary Note On");

        // Momentary Note Off: returns to 0.30
        juce::MidiBuffer noteOff;
        noteOff.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        processSilence(processor, noteOff);
        require(!processor.isFreezeActive(), "momentary MIDI note-off did not release Freeze");
        require(std::abs(processor.getEffectiveFreeze() - 0.30f) < 0.001f,
                "momentary MIDI note-off did not return effective freeze to 0.30");
        require(!processor.getTelemetrySnapshot().frozen,
                "telemetry snapshot.frozen must clear on momentary Note Off");

        // Return transition audio probe to verify DSP smoothing without clicks
        juce::AudioBuffer<float> returnBuffer(2, 256);
        returnBuffer.clear();
        processor.processBlock(returnBuffer, noMidi);
        float maxReturnDelta = 0.0f;
        for (int ch = 0; ch < 2; ++ch) {
            const float* chData = returnBuffer.getReadPointer(ch);
            for (int i = 1; i < 256; ++i) {
                float d = std::abs(chData[i] - chData[i - 1]);
                if (d > maxReturnDelta) maxReturnDelta = d;
            }
        }
        require(maxReturnDelta < 0.35f, "DSP smoothing on unfreeze return must be click-free");

        // -------------------------------------------------------------------
        // M3.1: Real JUCE MIDI Latch Mode Validation
        // -------------------------------------------------------------------
        setParameter(processor, "FREEZE", 0.30f);
        setParameter(processor, "FREEZE_MIDI_MODE", 0.0f); // Latch
        processSilence(processor, noMidi);
        require(std::abs(processor.getEffectiveFreeze() - 0.30f) < 0.001f,
                "scene freeze must be 0.30 before latch Note On");

        // Note On #1 -> override ON -> 1.0
        processSilence(processor, noteOn);
        require(processor.isFreezeActive(), "latch MIDI note-on did not engage Freeze");
        require(std::abs(processor.getEffectiveFreeze() - 1.0f) < 0.001f,
                "latch MIDI note-on did not set effective freeze to 1.0");

        // Note Off -> continues ON -> 1.0
        processSilence(processor, noteOff);
        require(processor.isFreezeActive(), "latch MIDI note-off unexpectedly released Freeze");
        require(std::abs(processor.getEffectiveFreeze() - 1.0f) < 0.001f,
                "latch MIDI note-off must maintain effective freeze at 1.0");

        // Note On #2 -> override OFF -> returns to 0.30
        processSilence(processor, noteOn);
        require(!processor.isFreezeActive(), "second latch MIDI note-on did not release Freeze");
        require(std::abs(processor.getEffectiveFreeze() - 0.30f) < 0.001f,
                "second latch MIDI note-on did not return effective freeze to 0.30");

        // -------------------------------------------------------------------
        // M3.1: Coexistence MIDI override + Scene Automation
        // -------------------------------------------------------------------
        setParameter(processor, "FREEZE", 0.30f);
        setParameter(processor, "FREEZE_MIDI_MODE", 1.0f); // Momentary
        processSilence(processor, noMidi);

        // Note On -> 1.0
        processSilence(processor, noteOn);
        require(std::abs(processor.getEffectiveFreeze() - 1.0f) < 0.001f,
                "override must force 1.0");

        // During override, scene automation changes FREEZE: 0.30 -> 0.65
        setParameter(processor, "FREEZE", 0.65f);
        processSilence(processor, noMidi);
        require(std::abs(processor.getEffectiveFreeze() - 1.0f) < 0.001f,
                "effective freeze must remain 1.0 during override while scene changes");

        // Releasing override (Note Off) -> must return to 0.65, not old 0.30
        processSilence(processor, noteOff);
        require(std::abs(processor.getEffectiveFreeze() - 0.65f) < 0.001f,
                "releasing override must return to 0.65, not old 0.30");
        require(!processor.isFreezeActive(), "override released");

        // -------------------------------------------------------------------
        // M3.1: Scene Morphing & Continuous Freeze Telemetry (No 0.5 Threshold)
        // -------------------------------------------------------------------
        setParameter(processor, "MORPH", 0.0f);
        setParameter(processor, "DENSITY", 0.2f);
        setParameter(processor, "MIX", 0.2f);
        setParameter(processor, "FREEZE", 0.0f);
        setParameter(processor, "RHYTHM_DIVISION", 0.0f);
        processor.captureScene(0);
        setParameter(processor, "MORPH", 1.0f);
        setParameter(processor, "DENSITY", 0.8f);
        setParameter(processor, "MIX", 0.8f);
        setParameter(processor, "FREEZE", 1.0f);
        setParameter(processor, "RHYTHM_DIVISION", 3.0f);
        processor.captureScene(1);

        setParameter(processor, "MORPH", 0.56f);
        processSilence(processor, noMidi);
        require(processor.getMorphedParameterValue("DENSITY") > 0.39f
                    && processor.getMorphedParameterValue("DENSITY") < 0.50f,
                "Density morph is not following its perceptual event-rate curve");
        require(processor.getMorphedParameterValue("MIX") > 0.57f
                    && processor.getMorphedParameterValue("MIX") < 0.65f,
                "Mix morph is not following its constant-power curve");
        require(processor.getMorphedParameterValue("RHYTHM_DIVISION") == 3.0f,
                "discrete morph did not switch to scene B above the upper threshold");
        require(!processor.isFreezeActive(),
                "continuous Freeze morph must not activate discrete performance override");
        require(std::abs(processor.getEffectiveFreeze() - processor.getMorphedParameterValue("FREEZE")) < 0.01f,
                "effective freeze must track morphed continuous scene value");
        require(!processor.getTelemetrySnapshot().frozen,
                "telemetry snapshot.frozen must not trigger on continuous morph >= 0.5");

        setParameter(processor, "MORPH", 0.50f);
        processSilence(processor, noMidi);
        require(processor.getMorphedParameterValue("RHYTHM_DIVISION") == 3.0f,
                "discrete morph chattered inside its hysteresis band");
        require(!processor.isFreezeActive(),
                "continuous Freeze morph at 0.50 must not activate discrete override");
        require(std::abs(processor.getEffectiveFreeze() - 0.50f) < 0.01f,
                "effective freeze at morph 0.50 must be exactly 0.50");
        require(!processor.getTelemetrySnapshot().frozen,
                "telemetry snapshot.frozen must remain false at morph 0.50");

        setParameter(processor, "MORPH", 0.44f);
        processSilence(processor, noMidi);
        require(processor.getMorphedParameterValue("RHYTHM_DIVISION") == 0.0f,
                "discrete morph did not return to scene A below the lower threshold");
        require(!processor.isFreezeActive(),
                "continuous Freeze morph at 0.44 must not activate discrete override");
        require(std::abs(processor.getEffectiveFreeze() - processor.getMorphedParameterValue("FREEZE")) < 0.01f,
                "effective freeze must track morphed continuous scene value below 0.5");
        require(!processor.getTelemetrySnapshot().frozen,
                "telemetry snapshot.frozen must remain false below 0.5");

        setParameter(processor, "MORPH", 0.35f);
        processSilence(processor, noMidi);

        juce::MemoryBlock state;
        processor.getStateInformation(state);
        const std::string stateBytes((const char*)state.getData(), state.getSize());
        require(stateBytes.find("PERFORMANCE_SCENES") != std::string::npos,
                "serialized state does not contain performance scenes");

        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            require(editor != nullptr, "processor did not create an editor");
            require(editor->getWidth() == 1160 && editor->getHeight() == 900,
                    "editor opened with an unexpected size");
            int sliders = 0, selectors = 0, steps = 0;
            for (int i=0; i<editor->getNumChildComponents(); ++i) {
                auto* child = editor->getChildComponent(i);
                if (!child->isVisible()) continue;
                require(!child->getBounds().isEmpty() && editor->getLocalBounds().contains(child->getBounds()),
                        "visible editor control is empty or clipped");
                if (dynamic_cast<juce::Slider*>(child)) ++sliders;
                if (dynamic_cast<juce::ComboBox*>(child)) ++selectors;
                if (auto* button=dynamic_cast<juce::TextButton*>(child))
                    if (button->getButtonText().getIntValue()>0) {
                        require(button->getHeight() >= 26, "rhythm step is too short to read or click");
                        ++steps;
                    }
            }
            require(sliders == 12 && selectors == 8 && steps == 16,
                    "editor must expose all macros, morph, selectors and rhythm steps");
            const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
            require(snapshot.isValid(), "editor snapshot could not be rendered");
            const auto screenshot = juce::File::getCurrentWorkingDirectory()
                .getChildFile("bubbles_editor_smoke.png");
            screenshot.deleteFile();
            auto stream = screenshot.createOutputStream();
            require(stream != nullptr, "editor snapshot output could not be created");
            juce::PNGImageFormat png;
            require(png.writeImageToStream(snapshot, *stream), "editor snapshot could not be encoded");

            juce::ComboBox* presetBox = nullptr;
            for (int i = 0; i < editor->getNumChildComponents(); ++i) {
                auto* candidate = dynamic_cast<juce::ComboBox*>(editor->getChildComponent(i));
                if (candidate != nullptr && candidate->getNumItems() == 20) {
                    presetBox = candidate;
                    break;
                }
            }
            require(presetBox != nullptr, "factory preset selector was not found for calibration");

            setParameter(processor, "MORPH", 0.0f);
            double quietestRms = 1.0;
            double loudestRms = 0.0;
            double totalRenderTime = 0.0;
            std::cout << std::fixed << std::setprecision(4);
            for (int preset = 0; preset < presetBox->getNumItems(); ++preset) {
                presetBox->setSelectedItemIndex(preset, juce::sendNotificationSync);
                processor.setRateAndBufferSizeDetails(48000.0, 256);
                processor.prepareToPlay(48000.0, 256);
                const auto metrics = renderMusicalProbe(processor, 48000.0, 256, 0.6);
                const double stereoRms = std::sqrt(
                    0.5 * (metrics.rmsLeft * metrics.rmsLeft + metrics.rmsRight * metrics.rmsRight));
                require(stereoRms > 0.008, "factory preset calibration found a near-silent preset");
                require(metrics.peak <= 0.9, "factory preset exceeded the final limiter ceiling");
                require(metrics.correlation > -0.8,
                        "factory preset produced unsafe stereo anti-correlation");
                require(std::max(metrics.rmsLeft, metrics.rmsRight)
                            / std::max(1.0e-9, std::min(metrics.rmsLeft, metrics.rmsRight)) < 2.5,
                        "factory preset produced an unsafe left/right level imbalance");
                require(metrics.rmsMono > 0.15 * std::max(metrics.rmsLeft, metrics.rmsRight),
                        "factory preset collapsed excessively in mono");
                quietestRms = std::min(quietestRms, stereoRms);
                loudestRms = std::max(loudestRms, stereoRms);
                totalRenderTime += metrics.renderSeconds;
                std::cout << "preset[" << std::setw(2) << preset << "] "
                          << presetBox->getItemText(preset) << ": rms=" << stereoRms
                          << " peak=" << metrics.peak << " corr=" << metrics.correlation << '\n';
            }
            require(loudestRms / quietestRms < 6.0,
                    "factory preset loudness spread is too large for a levelled catalog");
            {
                std::unique_ptr<juce::AudioProcessorEditor> liveEditor(processor.createEditor());
                // The new editor supplies fresh telemetry; label the probe's current preset without reapplying it.
                for (int i = 0; i < liveEditor->getNumChildComponents(); ++i)
                    if (auto* selector = dynamic_cast<juce::ComboBox*>(liveEditor->getChildComponent(i)))
                        if (selector->getNumItems() == 20)
                            selector->setSelectedItemIndex(presetBox->getSelectedItemIndex(), juce::dontSendNotification);
                auto image=liveEditor->createComponentSnapshot(liveEditor->getLocalBounds());
                auto file=juce::File::getCurrentWorkingDirectory().getChildFile("bubbles_editor_live.png");
                file.deleteFile(); auto output=file.createOutputStream(); juce::PNGImageFormat format;
                require(output != nullptr && format.writeImageToStream(image,*output), "live editor snapshot failed");
            }
            const double renderedAudioSeconds = 0.6 * presetBox->getNumItems();
            std::cout << "calibration spread=" << (loudestRms / quietestRms)
                      << "x, render speed=" << (renderedAudioSeconds / std::max(0.001, totalRenderTime))
                      << "x realtime\n";
        }

        auto restoredOwner = std::make_unique<BubbleCloudAudioProcessor>();
        auto& restored = *restoredOwner;
        restored.setStateInformation(state.getData(), (int)state.getSize());
        const auto* restoredMorph = restored.treeState.getRawParameterValue("MORPH");
        require(restoredMorph != nullptr && std::abs(restoredMorph->load() - 0.35f) < 0.01f,
                "scene morph parameter did not survive state restore");

        std::cout << "Bubbles processor smoke test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Bubbles processor smoke test failed: " << error.what() << '\n';
        return 1;
    }
}
