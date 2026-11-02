cmake_minimum_required(VERSION 3.22)

# Universal-friendly macOS minimum (Apple Silicon slices are built for 11.0+ automatically)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(MixInspector VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(MIXINSPECTOR_BUILD_TESTS    "Build the analysis unit tests"                      ON)
option(MIXINSPECTOR_BUILD_SNAPSHOT "Build the screenshot tool (renders the UI to PNG)" OFF)

#==============================================================================
# Analysis core: plain C++17, no JUCE, unit-testable on its own
#==============================================================================
add_library(MixInspectorCore STATIC Source/Core/Analyzer.cpp)
target_include_directories(MixInspectorCore PUBLIC Source/Core)
target_compile_features(MixInspectorCore PUBLIC cxx_std_17)
set_target_properties(MixInspectorCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if (MIXINSPECTOR_BUILD_TESTS)
    enable_testing()
    add_executable(MixInspectorTests tests/CoreTests.cpp)
    target_include_directories(MixInspectorTests PRIVATE tools)
    target_link_libraries(MixInspectorTests PRIVATE MixInspectorCore)
    add_test(NAME analysis-core COMMAND MixInspectorTests)
endif()

#==============================================================================
# JUCE (downloaded automatically; pass -DFETCHCONTENT_SOURCE_DIR_JUCE=/path to use a local copy)
#==============================================================================
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.4
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(MixInspectorAssets
    SOURCES
        Resources/Fonts/BarlowSemiCondensed-Regular.ttf
        Resources/Fonts/BarlowSemiCondensed-Medium.ttf
        Resources/Fonts/BarlowSemiCondensed-SemiBold.ttf)
set_target_properties(MixInspectorAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(MIXINSPECTOR_UI_SOURCES
    Source/UI/Theme.cpp
    Source/UI/Panels.cpp
    Source/UI/MainView.cpp)

set(MIXINSPECTOR_JUCE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0)

#==============================================================================
# The plugin
#==============================================================================
juce_add_plugin(MixInspector
    PRODUCT_NAME                "MixInspector"
    COMPANY_NAME                "MixInspector"
    BUNDLE_ID                   "io.github.mixinspector"
    DESCRIPTION                 "Correlation, phase, stereo width and harmonic dissonance checker"
    PLUGIN_MANUFACTURER_CODE    Mxin
    PLUGIN_CODE                 Mxi1
    FORMATS                     VST3 AU Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Analyzer
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(MixInspector
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        ${MIXINSPECTOR_UI_SOURCES})

target_include_directories(MixInspector PRIVATE Source Source/UI)
target_compile_definitions(MixInspector PUBLIC ${MIXINSPECTOR_JUCE_DEFINITIONS})

target_link_libraries(MixInspector
    PRIVATE
        MixInspectorCore
        MixInspectorAssets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
# Optional: render the UI with synthetic test audio to PNG (used for the README screenshots)
#==============================================================================
if (MIXINSPECTOR_BUILD_SNAPSHOT)
    juce_add_console_app(MixInspectorSnapshot PRODUCT_NAME "MixInspectorSnapshot")
    target_sources(MixInspectorSnapshot PRIVATE tools/Snapshot.cpp ${MIXINSPECTOR_UI_SOURCES})
    target_include_directories(MixInspectorSnapshot PRIVATE Source Source/UI tools)
    target_compile_definitions(MixInspectorSnapshot PRIVATE ${MIXINSPECTOR_JUCE_DEFINITIONS})
    target_link_libraries(MixInspectorSnapshot
        PRIVATE
            MixInspectorCore
            MixInspectorAssets
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
