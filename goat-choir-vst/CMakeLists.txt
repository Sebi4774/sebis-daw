cmake_minimum_required(VERSION 3.22)
project(GoatChoir VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 7.0.12: the last JUCE that builds with MinGW, which is how the Windows VST3 is cross-compiled
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} juce)
else()
    include(FetchContent)
    FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 7.0.12 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

# GOAT_DEMO=ON builds the free demo: strips 9-16, MIDI export and goat dressing are locked
option(GOAT_DEMO "Build the demo edition" OFF)
set(GOAT_TARGET GoatChoir)
set(GOAT_NAME "Goat Choir")
set(GOAT_CODE GtCh)
if(GOAT_DEMO)
    set(GOAT_TARGET GoatChoirDemo)
    set(GOAT_NAME "Goat Choir Demo")
    set(GOAT_CODE GtCd)
endif()

juce_add_plugin(${GOAT_TARGET}
    COMPANY_NAME "Sebi Inc"
    PRODUCT_NAME "${GOAT_NAME}"
    PLUGIN_NAME "${GOAT_NAME}"
    PLUGIN_MANUFACTURER_CODE SbiI
    PLUGIN_CODE ${GOAT_CODE}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Synth
    FORMATS Standalone VST3
    VST3_AUTO_MANIFEST FALSE   # the manifest helper can't run when cross-compiling; hosts don't need it
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(${GOAT_TARGET})
target_sources(${GOAT_TARGET} PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(${GOAT_TARGET} PUBLIC
    GOAT_DEMO=$<BOOL:${GOAT_DEMO}>
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=1)   # JUCE 7 Personal licence: the splash stays on
target_link_libraries(${GOAT_TARGET}
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Test tool: renders the choir to WAV, checks MIDI out and export, snapshots the editor to PNG
option(GOAT_BUILD_TEST "Build the test tool" OFF)
if(GOAT_BUILD_TEST)
    juce_add_console_app(GoatTest PRODUCT_NAME "goat_test")
    juce_generate_juce_header(GoatTest)
    target_sources(GoatTest PRIVATE tools/goat_test.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(GoatTest PRIVATE Source)
    target_compile_definitions(GoatTest PRIVATE GOAT_DEMO=$<BOOL:${GOAT_DEMO}> JucePlugin_Name="Goat Choir" JucePlugin_VersionString="1.0.0"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(GoatTest PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
endif()

# Host check: loads a built .vst3 through JUCE's VST3 hosting, plays chords into it, checks audio and MIDI come out
option(GOAT_BUILD_HOSTCHECK "Build the VST3 host check" OFF)
if(GOAT_BUILD_HOSTCHECK)
    juce_add_console_app(HostCheck PRODUCT_NAME "host_check")
    juce_generate_juce_header(HostCheck)
    target_sources(HostCheck PRIVATE tools/host_check.cpp)
    target_compile_definitions(HostCheck PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(HostCheck PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
endif()
