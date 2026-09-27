cmake_minimum_required(VERSION 3.22)
if(POLICY CMP0175)
    cmake_policy(SET CMP0175 OLD)
    set(CMAKE_POLICY_DEFAULT_CMP0175 OLD)
endif()
project(MixChecker VERSION 2.0.0)

option(MIXCHECKER_BUILD_VST3 "Build the MixChecker VST3 plugin" ON)
option(MIXCHECKER_BUILD_AAX "Build the MixChecker AAX plugin when AAX SDK/PACE tooling is configured" OFF)
option(MIXCHECKER_ENABLE_RELEASE_SIGNING "Enable release signing steps in packaging scripts" OFF)
option(MIXCHECKER_BUILD_STANDALONE "Build the JUCE standalone app for developer testing only; not shipped in installers" OFF)
option(MIXCHECKER_BUILD_TESTS "Build MixChecker plugin helper tests" ON)

if(APPLE)
    option(MIXCHECKER_BUILD_AU "Build the MixChecker Audio Unit plugin" ON)
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target" FORCE)
    endif()
else()
    option(MIXCHECKER_BUILD_AU "Build the MixChecker Audio Unit plugin" OFF)
endif()

include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0
)
FetchContent_MakeAvailable(juce)

set(MIXCHECKER_PLUGIN_FORMATS)

if(MIXCHECKER_BUILD_VST3)
    list(APPEND MIXCHECKER_PLUGIN_FORMATS VST3)
endif()

if(MIXCHECKER_BUILD_AU)
    if(APPLE)
        list(APPEND MIXCHECKER_PLUGIN_FORMATS AU)
    else()
        message(STATUS "AU skipped: Audio Unit is macOS-only.")
    endif()
endif()

if(MIXCHECKER_BUILD_AAX)
    if(DEFINED AAX_SDK_PATH AND EXISTS "${AAX_SDK_PATH}")
        juce_set_aax_sdk_path("${AAX_SDK_PATH}")
        list(APPEND MIXCHECKER_PLUGIN_FORMATS AAX)
        if(NOT DEFINED PACE_SIGNING_TOOL_PATH OR NOT EXISTS "${PACE_SIGNING_TOOL_PATH}")
            message(WARNING "AAX enabled with SDK, but PACE signing is not configured. Do not ship unsigned AAX builds publicly.")
        endif()
    else()
        message(STATUS "AAX skipped: AAX SDK/PACE signing not configured.")
    endif()
endif()

if(MIXCHECKER_BUILD_STANDALONE)
    list(APPEND MIXCHECKER_PLUGIN_FORMATS Standalone)
endif()

if(NOT MIXCHECKER_PLUGIN_FORMATS)
    message(FATAL_ERROR "No MixChecker plugin formats selected. Enable MIXCHECKER_BUILD_VST3, MIXCHECKER_BUILD_AU, or MIXCHECKER_BUILD_AAX.")
endif()

message(STATUS "MixChecker release formats: ${MIXCHECKER_PLUGIN_FORMATS}")

juce_add_plugin(MixChecker
    COMPANY_NAME "MixChecker"
    BUNDLE_ID "com.mixchecker.plugin"
    VERSION ${PROJECT_VERSION}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE "MixC"
    PLUGIN_CODE "MChk"
    FORMATS ${MIXCHECKER_PLUGIN_FORMATS}
)

target_sources(MixChecker
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PluginUpdateChecker.cpp
        Source/PluginUpdateChecker.h
        Source/UI/Style.h
        Source/UI/qrcodegen.hpp
        Source/UI/qrcodegen.cpp
)

target_compile_definitions(MixChecker
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(MixChecker PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_data_structures
    juce::juce_gui_extra
    juce::juce_graphics
)

if(MIXCHECKER_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(MixCheckerUpdateCheckerTests
        PRODUCT_NAME "MixCheckerUpdateCheckerTests"
    )

    target_sources(MixCheckerUpdateCheckerTests
        PRIVATE
            Tests/PluginUpdateCheckerTests.cpp
            Source/PluginUpdateChecker.cpp
            Source/PluginUpdateChecker.h
    )

    target_compile_definitions(MixCheckerUpdateCheckerTests
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
    )

    target_link_libraries(MixCheckerUpdateCheckerTests PRIVATE
        juce::juce_core
        juce::juce_data_structures
    )

    add_test(NAME MixCheckerUpdateCheckerTests COMMAND MixCheckerUpdateCheckerTests)
endif()

if(APPLE)
    function(strip_appledouble_files target_name)
        if(TARGET ${target_name})
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND /bin/sh -c "find '${CMAKE_BINARY_DIR}/MixChecker_artefacts' '$ENV{HOME}/Library/Audio/Plug-Ins/VST3/MixChecker.vst3' '$ENV{HOME}/Library/Audio/Plug-Ins/Components/MixChecker.component' -name '._*' -delete 2>/dev/null || true"
                VERBATIM
            )
        endif()
    endfunction()

    strip_appledouble_files(MixChecker_Standalone)
    strip_appledouble_files(MixChecker_AU)
    strip_appledouble_files(MixChecker_VST3)
    strip_appledouble_files(MixChecker_AAX)
endif()
