cmake_minimum_required(VERSION 3.15)
project(BubbleCloud VERSION 1.1.0)

option(BUBBLES_COPY_PLUGIN_AFTER_BUILD "Copy the built VST3 into the configured plug-in directory" ON)
option(BUBBLES_BUILD_PROCESSOR_TESTS "Build the JUCE processor smoke test" OFF)
set(BUBBLES_VST3_COPY_DIR "C:/VST" CACHE PATH "Destination directory for the compiled VST3 bundle")

# Add JUCE using FetchContent
include(FetchContent)
FetchContent_Declare(
    juce
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/7.0.12.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(juce)

juce_add_plugin(BubbleCloud
    COMPANY_NAME "Bubbles Audio"
    BUNDLE_ID "audio.bubbles.Bubbles"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${BUBBLES_COPY_PLUGIN_AFTER_BUILD}
    VST3_COPY_DIR "${BUBBLES_VST3_COPY_DIR}"
    PLUGIN_MANUFACTURER_CODE Jmab
    PLUGIN_CODE Bbcl
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Bubbles"
)

target_sources(BubbleCloud PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/BubbleCloudEngineWrapper.cpp
    
    # Core C sources
    ../../core/dsp/sound_bubbles_dsp.c
    ../../core/engine/bubble_engine.c
    ../../core/engine/bubble_macro_map.c
    ../../core/presets/bubble_preset.c
)

target_include_directories(BubbleCloud PRIVATE
    Source
    ../../core
    ../../core/dsp
)

target_compile_definitions(BubbleCloud PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

juce_generate_juce_header(BubbleCloud)

target_link_libraries(BubbleCloud PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
)

if(BUBBLES_BUILD_PROCESSOR_TESTS)
    enable_testing()
    add_executable(BubbleCloudProcessorTests
        ../../tests/juce/processor_smoke.cpp
    )
    target_include_directories(BubbleCloudProcessorTests PRIVATE
        Source
        ../../core
        ../../core/dsp
    )
    target_link_libraries(BubbleCloudProcessorTests PRIVATE
        BubbleCloud
        juce::juce_audio_utils
        juce::juce_audio_processors
    )
    add_test(NAME bubbles_processor_smoke COMMAND BubbleCloudProcessorTests)
endif()
