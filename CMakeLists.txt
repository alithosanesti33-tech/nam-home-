cmake_minimum_required(VERSION 3.22)
project(NAMHome VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_PATH "" CACHE PATH "Path to the JUCE source folder")
set(NAM_CORE_PATH "" CACHE PATH "Path to NeuralAmpModelerCore")
add_subdirectory(${JUCE_PATH} JUCE EXCLUDE_FROM_ALL)

juce_add_plugin(NAMHome
    COMPANY_NAME "NAM Home"
    BUNDLE_ID com.namhome.app
    PLUGIN_MANUFACTURER_CODE NmHm
    PLUGIN_CODE NmH1
    FORMATS Standalone
    PRODUCT_NAME "NAM Home"
    DESCRIPTION "A home for NAM captures and cabinet IRs"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "NAM Home needs your audio interface input so you can play your guitar through it."
    PLIST_TO_MERGE "<plist><dict><key>NSDownloadsFolderUsageDescription</key><string>NAM Home watches your Downloads folder so new captures and IRs are added to your library automatically.</string></dict></plist>"
)

file(GLOB NAM_SOURCES "${NAM_CORE_PATH}/NAM/*.cpp" "${NAM_CORE_PATH}/NAM/wavenet/*.cpp")

target_sources(NAMHome PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Library.cpp
    ${NAM_SOURCES}
)

target_include_directories(NAMHome PRIVATE
    Source
    ${NAM_CORE_PATH}
    ${NAM_CORE_PATH}/NAM
    ${NAM_CORE_PATH}/Dependencies/eigen
    ${NAM_CORE_PATH}/Dependencies/nlohmann
)

target_compile_definitions(NAMHome PUBLIC
    NAM_SAMPLE_FLOAT
    NAM_ENABLE_A2_FAST
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(NAMHome PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
)
