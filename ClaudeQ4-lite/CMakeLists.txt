cmake_minimum_required (VERSION 3.22)

if (APPLE)
    set (CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
    set (CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

project (ClaudeQ4 VERSION 1.0.1 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_POSITION_INDEPENDENT_CODE ON)
if (MSVC)
    # static runtime: the plug-in does not depend on the Visual C++ redistributable
    set (CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set (CLAUDEQ_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to the JUCE framework")
set (CLAUDEQ_AAX_SDK "" CACHE PATH "Path to the Avid AAX SDK (optional, enables the AAX format)")
option (CLAUDEQ_BUILD_TESTS "Build the DSP / GUI test executables" OFF)
option (CLAUDEQ_LINUX_MINIMAL "Linux: build without XRandR/Xinerama/XCursor/XInput/ALSA/JACK" OFF)

add_subdirectory ("${CLAUDEQ_JUCE_DIR}" JUCE)

set (CLAUDEQ_FORMATS VST3 Standalone)
if (APPLE)
    list (APPEND CLAUDEQ_FORMATS AU)
endif()
if (CLAUDEQ_AAX_SDK AND EXISTS "${CLAUDEQ_AAX_SDK}")
    juce_set_aax_sdk_path ("${CLAUDEQ_AAX_SDK}")
    list (APPEND CLAUDEQ_FORMATS AAX)
endif()

#------------------------------------------------------------------------------
# pffft (SIMD FFT, BSD-like licence)
add_library (cq_pffft STATIC
    ThirdParty/pffft/src/pffft.c
    ThirdParty/pffft/src/pffft_common.c)
target_include_directories (cq_pffft PUBLIC ThirdParty/pffft/include PRIVATE ThirdParty/pffft/src)
target_compile_definitions (cq_pffft PUBLIC PFFFT_STATIC_DEFINE)
if (APPLE OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64|ARM64")
    # NEON on Apple silicon / ARM (the header checks the architecture itself, so universal builds are fine)
    target_compile_definitions (cq_pffft PRIVATE PFFFT_ENABLE_NEON=1)
endif()
if (MSVC)
    target_compile_options (cq_pffft PRIVATE /W0)
else()
    target_compile_options (cq_pffft PRIVATE -w -O3)
    target_link_libraries (cq_pffft PRIVATE m)
endif()

#------------------------------------------------------------------------------
# DSP core (no JUCE dependency)
set (CLAUDEQ_DSP_SOURCES
    Source/dsp/FilterDesign.cpp
    Source/dsp/IIRBand.cpp
    Source/dsp/Dynamics.cpp
    Source/dsp/LinearPhase.cpp
    Source/dsp/NaturalPhase.cpp
    Source/dsp/Spectral.cpp
    Source/dsp/Character.cpp
    Source/dsp/KernelWorker.cpp
    Source/dsp/EQEngine.cpp)

add_library (cq_dsp STATIC ${CLAUDEQ_DSP_SOURCES})
target_include_directories (cq_dsp PUBLIC Source)
target_link_libraries (cq_dsp PUBLIC cq_pffft)
if (NOT MSVC)
    target_compile_options (cq_dsp PRIVATE -O3)
    find_package (Threads REQUIRED)
    target_link_libraries (cq_dsp PUBLIC Threads::Threads)
endif()

#------------------------------------------------------------------------------
juce_add_binary_data (ClaudeQ4Data
    HEADER_NAME "ClaudeQBinaryData.h"
    NAMESPACE ClaudeQBinaryData
    SOURCES
        Resources/Fonts/Inter-Regular.otf
        Resources/Fonts/Inter-Medium.otf
        Resources/Fonts/Inter-SemiBold.otf)

set (CLAUDEQ_PLUGIN_SOURCES
    Source/plugin/Parameters.cpp
    Source/plugin/Analyzer.cpp
    Source/plugin/InstanceRegistry.cpp
    Source/plugin/MidiLearn.cpp
    Source/plugin/Presets.cpp
    Source/plugin/PluginProcessor.cpp
    Source/gui/Theme.cpp
    Source/gui/Widgets.cpp
    Source/gui/EditorModel.cpp
    Source/gui/EQDisplay.cpp
    Source/gui/BandPanel.cpp
    Source/gui/Bars.cpp
    Source/gui/Panels.cpp
    Source/gui/BandFitter.cpp
    Source/gui/PluginEditor.cpp)

# the VST3 moduleinfo.json helper has to run on the target platform
set (CLAUDEQ_VST3_MANIFEST TRUE)
if (CMAKE_CROSSCOMPILING)
    set (CLAUDEQ_VST3_MANIFEST FALSE)
endif()

juce_add_plugin (ClaudeQ4
    PRODUCT_NAME "Claude-Q 4"
    VERSION 1.0.1
    COMPANY_NAME "Claude Audio"
    COMPANY_WEBSITE "https://claude.ai"
    BUNDLE_ID "com.claudeaudio.claudeq4"
    PLUGIN_MANUFACTURER_CODE Cldq
    PLUGIN_CODE Clq4
    FORMATS ${CLAUDEQ_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx EQ
    VST3_AUTO_MANIFEST ${CLAUDEQ_VST3_MANIFEST}
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    AAX_CATEGORY AAX_ePlugInCategory_EQ
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources (ClaudeQ4 PRIVATE ${CLAUDEQ_PLUGIN_SOURCES})
target_include_directories (ClaudeQ4 PRIVATE Source)

target_compile_definitions (ClaudeQ4
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

if (CLAUDEQ_LINUX_MINIMAL AND UNIX AND NOT APPLE)
    target_compile_definitions (ClaudeQ4 PUBLIC
        JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XINPUT=0 JUCE_ALSA=0 JUCE_JACK=0)
endif()

# exact float comparisons (choice values, change detection) and partial enum switches are intentional
add_library (cq_warning_tweaks INTERFACE)
if (NOT MSVC)
    target_compile_options (cq_warning_tweaks INTERFACE -Wno-float-equal -Wno-switch-enum)
endif()

target_link_libraries (ClaudeQ4
    PRIVATE
        ClaudeQ4Data
        cq_dsp
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
        cq_warning_tweaks)

#------------------------------------------------------------------------------
if (CLAUDEQ_BUILD_TESTS)
    # integration / GUI test harness (also loads the built VST3 through JUCE's plug-in host)
    juce_add_console_app (ClaudeQ4Tests PRODUCT_NAME "ClaudeQ4Tests")
    target_sources (ClaudeQ4Tests PRIVATE Tests/SnapshotMain.cpp ${CLAUDEQ_PLUGIN_SOURCES})
    target_include_directories (ClaudeQ4Tests PRIVATE Source)
    target_compile_definitions (ClaudeQ4Tests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 CLAUDEQ_TEST_BUILD=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    if (CLAUDEQ_LINUX_MINIMAL AND UNIX AND NOT APPLE)
        target_compile_definitions (ClaudeQ4Tests PRIVATE
            JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XINPUT=0 JUCE_ALSA=0 JUCE_JACK=0)
    endif()
    target_link_libraries (ClaudeQ4Tests PRIVATE ClaudeQ4Data cq_dsp
        juce::juce_audio_utils juce::juce_recommended_config_flags juce::juce_recommended_warning_flags cq_warning_tweaks)
endif()
