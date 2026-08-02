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
                sumLeft += left * left;
                sumRight += right * right;
                sumMono += mono * mono;
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
        metrics.correlation = sumCross / std::sqrt(std::max(1.0e-18, sumLeft * sumRight));
        metrics.peak = peak;
        metrics.renderSeconds = elapsed;
        return metrics;
    }

    void testSampleRateAndBlockSizeMatrix()
    {
        constexpr std::array<double, 4> sampleRates {{ 44100.0, 48000.0, 88200.0, 96000.0 }};
        constexpr std::array<int, 5> blockSizes {{ 32, 64, 127, 512, 2048 }};

        for (const double sampleRate : sampleRates) {
            for (const int blockSize : blockSizes) {
                BubbleCloudAudioProcessor processor;
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
}

int main()
{
    try {
        juce::ScopedJuceInitialiser_GUI initialiseJuce;
        BubbleCloudAudioProcessor processor;
        processor.setRateAndBufferSizeDetails(48000.0, 128);
        processor.prepareToPlay(48000.0, 128);

        require(processor.acceptsMidi(), "processor must advertise MIDI input");
        require(processor.getTailLengthSeconds() >= 2.0, "granular tail must be reported to the host");

        testSampleRateAndBlockSizeMatrix();

        juce::AudioBuffer<float> stereoProbe(2, 128);
        stereoProbe.clear();
        for (int i = 0; i < stereoProbe.getNumSamples(); ++i)
            stereoProbe.setSample(0, i, 0.75f);
        juce::MidiBuffer noMidi;
        processor.processBlock(stereoProbe, noMidi);

        double leftEnergy = 0.0;
        double rightEnergy = 0.0;
        for (int i = 0; i < stereoProbe.getNumSamples(); ++i) {
            leftEnergy += std::abs(stereoProbe.getSample(0, i));
            rightEnergy += std::abs(stereoProbe.getSample(1, i));
        }
        require(leftEnergy > 0.01, "left-only probe produced no left output");
        require(rightEnergy < 1.0e-7, "left-only probe leaked into the right engine input");

        const auto stereoTelemetry = processor.getTelemetrySnapshot();
        require(stereoTelemetry.peakLeft > 0.01f, "telemetry did not report the left output peak");
        require(stereoTelemetry.peakRight < 1.0e-7f, "telemetry reported a false right output peak");
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

        processor.setCaptureHeld(true);
        processSilence(processor, noMidi);
        require(processor.isFreezeActive(), "held Capture did not engage Freeze");
        processor.setCaptureHeld(false);
        processSilence(processor, noMidi);
        require(!processor.isFreezeActive(), "releasing Capture did not restore the scene Freeze value");

        setParameter(processor, "FREEZE_MIDI_MODE", 1.0f);
        setParameter(processor, "FREEZE_MIDI_NOTE", 60.0f);
        juce::MidiBuffer noteOn;
        noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
        processSilence(processor, noteOn);
        require(processor.isFreezeActive(), "momentary MIDI note-on did not engage Freeze");
        juce::MidiBuffer noteOff;
        noteOff.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        processSilence(processor, noteOff);
        require(!processor.isFreezeActive(), "momentary MIDI note-off did not release Freeze");

        setParameter(processor, "FREEZE_MIDI_MODE", 0.0f);
        processSilence(processor, noteOn);
        require(processor.isFreezeActive(), "latch MIDI note-on did not engage Freeze");
        processSilence(processor, noteOff);
        require(processor.isFreezeActive(), "latch MIDI note-off unexpectedly released Freeze");
        processSilence(processor, noteOn);
        require(!processor.isFreezeActive(), "second latch MIDI note-on did not release Freeze");

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
        require(processor.isFreezeActive(),
                "Freeze morph did not engage above the upper threshold");

        setParameter(processor, "MORPH", 0.50f);
        processSilence(processor, noMidi);
        require(processor.getMorphedParameterValue("RHYTHM_DIVISION") == 3.0f,
                "discrete morph chattered inside its hysteresis band");
        require(processor.isFreezeActive(),
                "Freeze chattered inside its hysteresis band");

        setParameter(processor, "MORPH", 0.44f);
        processSilence(processor, noMidi);
        require(processor.getMorphedParameterValue("RHYTHM_DIVISION") == 0.0f,
                "discrete morph did not return to scene A below the lower threshold");
        require(!processor.isFreezeActive(),
                "Freeze did not release below the lower hysteresis threshold");

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
            require(editor->getWidth() == 1080 && editor->getHeight() == 760,
                    "editor opened with an unexpected size");
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
            const double renderedAudioSeconds = 0.6 * presetBox->getNumItems();
            std::cout << "calibration spread=" << (loudestRms / quietestRms)
                      << "x, render speed=" << (renderedAudioSeconds / std::max(0.001, totalRenderTime))
                      << "x realtime\n";
        }

        BubbleCloudAudioProcessor restored;
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
