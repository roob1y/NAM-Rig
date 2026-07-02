# nam_aa_core — shared NAM AA engine library.
#
# Consumed by the NAM-AA plugin (this repo) and the NAM-Rig plugin (separate
# repo, via add_subdirectory on this directory). Owns:
#   - the NAM core sources (incl. the repo-owned a2_fast fast path, synced into
#     the NAM core tree at configure time)
#   - the engine headers in include/nam_aa/: RoutingMath.h, PolyphaseResampler.h,
#     SrcLayer.h, AaEngine.h
#
# The compiled library itself is JUCE-free. SrcLayer.h / AaEngine.h DO need
# JUCE: the consuming target must link juce_audio_basics + juce_dsp (every
# current consumer already does).
#
# Required before add_subdirectory: set(NAM_CORE_DIR ".../NeuralAmpModelerCore")

if(NOT DEFINED NAM_CORE_DIR)
    message(FATAL_ERROR "nam_aa_core: set NAM_CORE_DIR before add_subdirectory(core)")
endif()

# Sync the repo-owned A2 fast path into the NAM core tree (compiled from there
# so its relative includes of NAM internals resolve).
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/NAM/wavenet/a2_fast.h"
     DESTINATION "${NAM_CORE_DIR}/NAM/wavenet/")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/NAM/wavenet/a2_fast.cpp"
     DESTINATION "${NAM_CORE_DIR}/NAM/wavenet/")

set(NAM_AA_CORE_SOURCES
    ${NAM_CORE_DIR}/NAM/activations.cpp
    ${NAM_CORE_DIR}/NAM/container.cpp
    ${NAM_CORE_DIR}/NAM/conv1d.cpp
    ${NAM_CORE_DIR}/NAM/convnet.cpp
    ${NAM_CORE_DIR}/NAM/dsp.cpp
    ${NAM_CORE_DIR}/NAM/get_dsp.cpp
    ${NAM_CORE_DIR}/NAM/lstm.cpp
    ${NAM_CORE_DIR}/NAM/ring_buffer.cpp
    ${NAM_CORE_DIR}/NAM/util.cpp
    ${NAM_CORE_DIR}/NAM/wavenet/a2_fast.cpp
    ${NAM_CORE_DIR}/NAM/wavenet/model.cpp
    ${NAM_CORE_DIR}/NAM/wavenet/slimmable.cpp
)

add_library(nam_aa_core STATIC ${NAM_AA_CORE_SOURCES})

set_target_properties(nam_aa_core PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON   # linked into VST3 shared libraries
)

target_include_directories(nam_aa_core PUBLIC
    ${NAM_CORE_DIR}                       # #include "NAM/get_dsp.h"
    ${NAM_CORE_DIR}/Dependencies/eigen    # Eigen headers
    ${NAM_CORE_DIR}/Dependencies/nlohmann # json.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include          # #include "nam_aa/AaEngine.h"
    ${CMAKE_CURRENT_SOURCE_DIR}/include/nam_aa   # #include "AaEngine.h" (legacy style)
)

# PUBLIC: these affect NAM/Eigen headers compiled into consumer TUs too.
target_compile_definitions(nam_aa_core PUBLIC
    NAM_SAMPLE_FLOAT=1   # NAM uses float to match JUCE AudioBuffer
    NAM_ENABLE_A2_FAST=1 # Enable the optimised A2 WaveNet path
    EIGEN_NO_DEBUG
    EIGEN_FAST_MATH=1
)
if(WIN32)
    target_compile_definitions(nam_aa_core PUBLIC
        NOMINMAX            # Prevent Windows min/max macros
        WIN32_LEAN_AND_MEAN # Leaner Windows headers
    )
endif()

if(MSVC)
    # AVX2 + FMA and fast FP, matching the plugin targets (consumers compile
    # Eigen-heavy headers in their own TUs and set these themselves).
    target_compile_options(nam_aa_core PRIVATE /arch:AVX2 /fp:fast)
else()
    target_compile_options(nam_aa_core PRIVATE -mavx2 -mfma -ffast-math)
endif()
