cmake_minimum_required(VERSION 3.22)
project(NAM_Rig VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Paths (adjust if your clones are elsewhere) ---
set(JUCE_DIR         "C:/Dev/JUCE")
set(NAM_CORE_DIR     "C:/Dev/NeuralAmpModelerCore")
set(NAM_AA_CORE_DIR  "C:/Dev/NAM-AA-Plugin/core")  # shared engine library

# --- JUCE ---
add_subdirectory(${JUCE_DIR} "${CMAKE_BINARY_DIR}/JUCE")

# --- Shared NAM AA engine (same code the NAM-AA plugin runs) ---
add_subdirectory(${NAM_AA_CORE_DIR} "${CMAKE_BINARY_DIR}/nam_aa_core")

# --- Plugin target ---
juce_add_plugin(NAM_Rig
    COMPANY_NAME               "Robbie"
    PLUGIN_MANUFACTURER_CODE   RbAA
    PLUGIN_CODE                NmRg
    FORMATS                    VST3
    PRODUCT_NAME               "NAM Rig"
    COPY_PLUGIN_AFTER_BUILD    TRUE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_SYNTH                    FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
)

target_sources(NAM_Rig PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
)

target_include_directories(NAM_Rig PRIVATE src)

target_compile_definitions(NAM_Rig PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(NAM_Rig PRIVATE
    nam_aa_core
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(NAM_Rig PRIVATE /arch:AVX2 /fp:fast)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(NAM_Rig PRIVATE $<$<CONFIG:Release>:/GL>)
        target_link_options(NAM_Rig PRIVATE $<$<CONFIG:Release>:/LTCG>)
    else()
        target_compile_options(NAM_Rig PRIVATE $<$<CONFIG:Release>:/clang:-flto=thin>)
        target_link_options(NAM_Rig PRIVATE $<$<CONFIG:Release>:/clang:-flto=thin>)
    endif()
endif()

# --- Offline full-rig processor (console; the rig's verification harness) ---
# Runs the literally-shared RigChain code — same measurement culture as
# NAM-AA's chain_process: every block is verified offline before shipping.
option(BUILD_RIG_HARNESS "Build the offline rig chain processor" ON)
if(BUILD_RIG_HARNESS)
    juce_add_console_app(rig_chain_process PRODUCT_NAME "rig_chain_process")
    target_sources(rig_chain_process PRIVATE tests/rig_chain_process.cpp)
    target_include_directories(rig_chain_process PRIVATE src)
    target_compile_definitions(rig_chain_process PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_link_libraries(rig_chain_process PRIVATE
        nam_aa_core
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags
    )
    if(MSVC)
        target_compile_options(rig_chain_process PRIVATE /arch:AVX2 /fp:fast)
    endif()
endif()
