cmake_minimum_required(VERSION 3.22)
project(NAMHome VERSION 0.3.0)

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
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "NAM Home needs your audio interface input so you can play your guitar through it."
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "NAM Home uses Bluetooth to connect wireless MIDI pedals such as an expression pedal."
    PLIST_TO_MERGE "<plist><dict><key>NSDownloadsFolderUsageDescription</key><string>NAM Home watches your Downloads folder so new captures and IRs are added to your library automatically.</string><key>CFBundleURLTypes</key><array><dict><key>CFBundleURLName</key><string>com.namhome.app</string><key>CFBundleURLSchemes</key><array><string>namhome</string></array></dict></array></dict></plist>"
)

juce_generate_juce_header(NAMHome)

juce_add_binary_data(NAMHomeData SOURCES
    Resources/Inter-Regular.otf
    Resources/Inter-Medium.otf
    Resources/Inter-SemiBold.otf
)

target_sources(NAMHome PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/StandaloneApp.cpp
    Source/Library.cpp
    Source/Blocks.cpp
    Source/Effects.cpp
    Source/NamBlocks.cpp
    Source/Engine.cpp
    Source/Tone3000.cpp
    Source/ui/Look.cpp
    Source/ui/Widgets.cpp
    Source/ui/BlockEditors.cpp
    Source/ui/GridView.cpp
    Source/ui/Panels.cpp
    Source/ui/Tuner.cpp
)

# NAM core is built as its own object library and linked straight into the app.
# Its model types register themselves from static objects; linking the objects directly
# (not through a static archive) keeps every type, e.g. A2 "SlimmableContainer" captures.
file(GLOB NAM_SOURCES "${NAM_CORE_PATH}/NAM/*.cpp" "${NAM_CORE_PATH}/NAM/wavenet/*.cpp")
add_library(NAMCore OBJECT ${NAM_SOURCES})
target_include_directories(NAMCore PUBLIC
    ${NAM_CORE_PATH}
    ${NAM_CORE_PATH}/NAM
    ${NAM_CORE_PATH}/Dependencies/eigen
    ${NAM_CORE_PATH}/Dependencies/nlohmann
)
target_compile_definitions(NAMCore PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
set_target_properties(NAMCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(NAMCore PRIVATE -O3)

target_include_directories(NAMHome PRIVATE Source)
target_compile_definitions(NAMHome PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
)

target_link_libraries(NAMHome PRIVATE
    NAMHomeData
    NAMCore
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_cryptography
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
)
# put the NAM objects directly into the app executable as well
target_sources(NAMHome_Standalone PRIVATE $<TARGET_OBJECTS:NAMCore>)
