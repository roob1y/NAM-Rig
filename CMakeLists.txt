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
    FORMATS                    VST3 Standalone
    PRODUCT_NAME               "NAM Rig"
    COPY_PLUGIN_AFTER_BUILD    TRUE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_SYNTH                    FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
)

juce_add_binary_data(NAM_RigData SOURCES
    resources/plate_early_kernel.wav
    # --- derived-IR hybrid plate/room (brand-free, derive_ir.py) ---
    resources/derived_irs/plate_derived.wav
    resources/derived_irs/plate_derived_short.wav
    resources/derived_irs/room_derived.wav
    resources/derived_irs/room_derived_short.wav
    # --- plate decay bank: 6 decay-reshaped versions of one capture (Greg Hopkins, CC-BY 4.0) -> continuous Decay ---
    resources/plate_irs/plate_ir_1.wav
    resources/plate_irs/plate_ir_2.wav
    resources/plate_irs/plate_ir_3.wav
    resources/plate_irs/plate_ir_4.wav
    resources/plate_irs/plate_ir_5.wav
    resources/plate_irs/plate_ir_6.wav
    # --- spring decay bank: 6 decay-reshaped studio-spring captures -> continuous Decay ---
    resources/spring_irs/spring_ir_1.wav
    resources/spring_irs/spring_ir_2.wav
    resources/spring_irs/spring_ir_3.wav
    resources/spring_irs/spring_ir_4.wav
    resources/spring_irs/spring_ir_5.wav
    resources/spring_irs/spring_ir_6.wav
    # --- Space Tank spring bank: 6 decay-reshaped versions of one vintage tape-echo
    #     spring-tank capture (brand-free, gen via /tmp/gen_tank.py) -> continuous Decay ---
    resources/spring_irs/spring_tank_1.wav
    resources/spring_irs/spring_tank_2.wav
    resources/spring_irs/spring_tank_3.wav
    resources/spring_irs/spring_tank_4.wav
    resources/spring_irs/spring_tank_5.wav
    resources/spring_irs/spring_tank_6.wav
    # --- UI fonts (SIL OFL 1.1 — see resources/fonts/*-OFL.txt) ---
    resources/fonts/Archivo-Regular.ttf
    resources/fonts/Archivo-Medium.ttf
    resources/fonts/Archivo-SemiBold.ttf
    resources/fonts/Archivo-Bold.ttf
    resources/fonts/Archivo-ExtraBold.ttf
    resources/fonts/JetBrainsMono-Regular.ttf
    resources/fonts/JetBrainsMono-Medium.ttf
    resources/fonts/JetBrainsMono-SemiBold.ttf)

target_sources(NAM_Rig PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/PluginEditor.h
    src/CalNorm.h
    src/PresetFile.h
    src/FactoryPresets.h
    src/PresetManager.h
    src/ui/RigLookAndFeel.h
    src/ui/Meter.h
    src/ui/HeaderPanel.h
    src/ui/BlockStrip.h
    src/ui/Panels.h
    src/ui/PresetBar.h
)

target_include_directories(NAM_Rig PRIVATE src)

target_compile_definitions(NAM_Rig PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(NAM_Rig PRIVATE
    NAM_RigData
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

    # cal_test — input calibration + output normalization (CalNorm.h + AaEngine
    # metadata path). Links the engine to load metadata-bearing .nam models.
    juce_add_console_app(cal_test PRODUCT_NAME "cal_test")
    target_sources(cal_test PRIVATE tests/cal_test.cpp)
    target_include_directories(cal_test PRIVATE src "${NAM_AA_CORE_DIR}/include/nam_aa")
    target_compile_definitions(cal_test PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_link_libraries(cal_test PRIVATE
        nam_aa_core
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags
    )
    if(MSVC)
        target_compile_options(cal_test PRIVATE /arch:AVX2 /fp:fast)
    endif()

    # dualrig_test — dual-voice RigChain core (SoloA bit-exact regression, mixer/
    # mode/pan, latency invariant). Links the engine (loads a model for T1).
    juce_add_console_app(dualrig_test PRODUCT_NAME "dualrig_test")
    target_sources(dualrig_test PRIVATE tests/dualrig_test.cpp)
    target_include_directories(dualrig_test PRIVATE src "${NAM_AA_CORE_DIR}/include/nam_aa")
    target_compile_definitions(dualrig_test PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_link_libraries(dualrig_test PRIVATE
        nam_aa_core
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags
    )
    if(MSVC)
        target_compile_options(dualrig_test PRIVATE /arch:AVX2 /fp:fast)
    endif()

    # GateBlock measurement suite (PASS/FAIL, exits nonzero on failure)
    juce_add_console_app(gate_test PRODUCT_NAME "gate_test")
    target_sources(gate_test PRIVATE tests/gate_test.cpp)
    target_include_directories(gate_test PRIVATE src)
    target_compile_definitions(gate_test PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        _USE_MATH_DEFINES   # M_PI on MSVC
    )
    target_link_libraries(gate_test PRIVATE
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
    )
    if(MSVC)
        target_compile_options(gate_test PRIVATE /arch:AVX2 /fp:fast)
    endif()

    # DriveBlock measurement suite (Off/Boost exactness, ADAA anti-aliasing)
    juce_add_console_app(drive_test PRODUCT_NAME "drive_test")
    target_sources(drive_test PRIVATE tests/drive_test.cpp)
    target_include_directories(drive_test PRIVATE src)
    target_compile_definitions(drive_test PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        _USE_MATH_DEFINES   # M_PI on MSVC
    )
    target_link_libraries(drive_test PRIVATE
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
    )
    if(MSVC)
        target_compile_options(drive_test PRIVATE /arch:AVX2 /fp:fast)
    endif()

    # CompBlock measurement suite (PASS/FAIL, exits nonzero on failure)
    juce_add_console_app(comp_test PRODUCT_NAME "comp_test")
    target_sources(comp_test PRIVATE tests/comp_test.cpp)
    target_include_directories(comp_test PRIVATE src)
    target_compile_definitions(comp_test PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        _USE_MATH_DEFINES   # M_PI on MSVC
    )
    target_link_libraries(comp_test PRIVATE
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
    )
    if(MSVC)
        target_compile_options(comp_test PRIVATE /arch:AVX2 /fp:fast)
    endif()

    # EqBlock + CutFilters measurement suite (PASS/FAIL, exits nonzero on failure)
    juce_add_console_app(eq_test PRODUCT_NAME "eq_test")
    target_sources(eq_test PRIVATE tests/eq_test.cpp)
    target_include_directories(eq_test PRIVATE src)
    target_compile_definitions(eq_test PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        _USE_MATH_DEFINES   # M_PI on MSVC
    )
    target_link_libraries(eq_test PRIVATE
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
    )
    if(MSVC)
        target_compile_options(eq_test PRIVATE /arch:AVX2 /fp:fast)
    endif()

    # Stereo-section + preset suites (PASS/FAIL, exit nonzero on failure)
    foreach(t IN ITEMS mod_test delay_test reverb_test preset_test)
        juce_add_console_app(${t} PRODUCT_NAME "${t}")
        target_sources(${t} PRIVATE tests/${t}.cpp)
        target_include_directories(${t} PRIVATE src)
        target_compile_definitions(${t} PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            _USE_MATH_DEFINES   # M_PI on MSVC
        )
        target_link_libraries(${t} PRIVATE
            juce::juce_audio_basics
            juce::juce_recommended_config_flags
        )
        if(MSVC)
            target_compile_options(${t} PRIVATE /arch:AVX2 /fp:fast)
        endif()
    endforeach()
endif()
