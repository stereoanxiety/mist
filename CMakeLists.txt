cmake_minimum_required(VERSION 3.22)

project(Mist VERSION 1.0.0 LANGUAGES C CXX)

# --- JUCE --------------------------------------------------------------------
# Prefer the workspace's local checkout (fast, offline). If it's absent — e.g. a
# fresh clone or CI — fetch JUCE 8.0.13 via FetchContent so the repo builds
# stand-alone with no extra setup.
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../sources/JUCE/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../sources/JUCE
                     ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.13
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# --- CLAP support via clap-juce-extensions -----------------------------------
# Adds a Mist_CLAP target that shares the plugin's code with the VST3/AU. Fetched
# from upstream so the repo builds CLAP stand-alone with no extra setup.
include(FetchContent)
FetchContent_Declare(clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    # main @ 2026-04-24 — JUCE 8 fix (guards the removed VST mac wrapper include);
    # the 0.26.0 tag predates it and fails to build against JUCE >= 7.0.6.
    GIT_TAG        e8de9e8571626633b8541a54c2406fccc4272767)
FetchContent_MakeAvailable(clap-juce-extensions)

# --- UI assets (panel background + knob + LED indicators + power button) -----
# Embedded as BinaryData so the bundle is self-contained. PIC so it links into
# the shared plugin format targets.
juce_add_binary_data(MistData SOURCES
    Resources/background.png
    Resources/led_teal.png
    Resources/led_chrome.png
    Resources/button_on.png
    Resources/button_off.png
    Resources/knob.png
    Resources/logo.png
    Resources/Geist-Regular.ttf
    Resources/Abuget.ttf)
set_target_properties(MistData PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# Copy built plugins into the user plugin folders after a full build (handy for
# local A/B). Turn OFF in CI — the Windows runner's system VST3 dir needs admin
# and would fail the build.
option(MIST_COPY_AFTER_BUILD "Install built plugins to user plugin folders" ON)

juce_add_plugin(Mist
    COMPANY_NAME              "Stereo Anxiety"
    BUNDLE_ID                 com.stereoanxiety.mist
    PLUGIN_MANUFACTURER_CODE  Stan
    PLUGIN_CODE               Mis1
    FORMATS                   AU VST3 Standalone
    PRODUCT_NAME              "Mist"
    VST3_CATEGORIES           "Fx" "Reverb"
    AU_MAIN_TYPE              "kAudioUnitType_Effect"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    COPY_PLUGIN_AFTER_BUILD   ${MIST_COPY_AFTER_BUILD})   # install to user plugin folders for A/B

juce_generate_juce_header(Mist)

target_sources(Mist PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_features(Mist PRIVATE cxx_std_17)

target_compile_definitions(Mist PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Mist PRIVATE
    MistData
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# CLAP build of the same plugin (shares Mist's sources/processor). Must come
# AFTER the target is fully configured — clap_juce_extensions_plugin clones the
# target's compile features/definitions when it creates Mist_CLAP, so calling it
# before cxx_std_17 + the JUCE defines are set yields a CLAP target missing C++17.
clap_juce_extensions_plugin(TARGET Mist
    CLAP_ID       "com.stereoanxiety.mist"
    CLAP_FEATURES audio-effect reverb)

# --- Tests -------------------------------------------------------------------
option(MIST_BUILD_TESTS "Build the Mist test executable" ON)
option(MIST_COVERAGE    "Instrument the test exe for llvm-cov" OFF)

if (MIST_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(Mist_tests PRODUCT_NAME "Mist_tests")
    target_sources(Mist_tests PRIVATE
        Tests/test_main.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

    juce_generate_juce_header(Mist_tests)
    target_include_directories(Mist_tests PRIVATE Source)
    target_compile_features(Mist_tests PRIVATE cxx_std_17)
    target_compile_definitions(Mist_tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(Mist_tests PRIVATE
        MistData
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    if (MIST_COVERAGE)
        target_compile_options(Mist_tests PRIVATE
            -fprofile-instr-generate -fcoverage-mapping -O0 -g)
        target_link_options(Mist_tests PRIVATE
            -fprofile-instr-generate -fcoverage-mapping)
    endif()

    add_test(NAME Mist_tests COMMAND Mist_tests)
endif()
