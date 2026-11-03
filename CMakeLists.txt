cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# Target platform: Intel Mac, macOS 10.12 Sierra, Logic Pro X 10.2.3, AUv2.
# These must be set BEFORE project() so CMake picks them up for the toolchain.
# They are cache variables so they can still be overridden with -D if needed.
# ---------------------------------------------------------------------------
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.12" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES     "x86_64" CACHE STRING "Build architectures (Intel only)")

project(LoopStationAU VERSION 0.1.0 LANGUAGES C CXX)

# JUCE 7 requires C++17. We stay on C++17 (not C++20) because the Sierra
# system libc++ is old; see README.md "Sierra compatibility rules".
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE 7.0.12: last JUCE 7 release, deployment targets down to macOS 10.9.
# To use a local checkout instead of downloading:
#   cmake -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# juce_gui_extra always links WebKit, even with JUCE_WEB_BROWSER=0, and
# -dead_strip_dylibs doesn't remove it. We never use it, and every extra system
# framework is a load-time risk on the Sierra machine (see README.md), so remove it
# from the module's link interface. If anything did need WebKit, linking would fail
# loudly with undefined symbols.
get_target_property(_juce_gui_extra_libs juce_gui_extra INTERFACE_LINK_LIBRARIES)
if(_juce_gui_extra_libs)
    list(FILTER _juce_gui_extra_libs EXCLUDE REGEX "WebKit")
    set_target_properties(juce_gui_extra PROPERTIES INTERFACE_LINK_LIBRARIES "${_juce_gui_extra_libs}")
endif()

juce_add_plugin(LoopStationGain
    COMPANY_NAME              "LoopStation"
    BUNDLE_ID                 "com.loopstation.gain"
    PLUGIN_NAME               "LoopStation Gain"
    PRODUCT_NAME              "LoopStationGain"   # -> LoopStationGain.component (no spaces)
    PLUGIN_MANUFACTURER_CODE  Lsau                # 4 chars, at least one upper-case letter
    PLUGIN_CODE               Lsg1                # 4 chars, unique per plugin
    FORMATS                   AU
    AU_MAIN_TYPE              kAudioUnitType_Effect   # 'aufx'
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE)

target_sources(LoopStationGain PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(LoopStationGain PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # Allowed because this project is GPLv3 (see LICENSE).
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(LoopStationGain
    PRIVATE
        juce::juce_audio_processors   # not juce_audio_utils: that one drags in DiscRecording
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(APPLE)
    # Drop every framework the binary doesn't actually use (e.g. WebKit, which
    # juce_gui_extra always links even with JUCE_WEB_BROWSER=0). Each system framework
    # we load is one more thing that can be broken on an old/mismatched macOS install;
    # see README.md, "Keep the framework list short".
    target_link_options(LoopStationGain_AU PRIVATE -Wl,-dead_strip_dylibs)

    # Warn whenever an API newer than the deployment target is used without an
    # @available / __builtin_available guard.
    target_compile_options(LoopStationGain PRIVATE -Wunguarded-availability)
endif()
