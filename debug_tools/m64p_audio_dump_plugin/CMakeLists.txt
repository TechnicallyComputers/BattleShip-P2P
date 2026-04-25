cmake_minimum_required(VERSION 3.16)
project(mupen64plus-audio-dump C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(mupen64plus-audio-dump SHARED audio_dump_plugin.c)

target_include_directories(mupen64plus-audio-dump PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../m64p_trace_plugin
)

# macOS: produce a .dylib without "lib" prefix to match m64p plugin convention
set_target_properties(mupen64plus-audio-dump PROPERTIES
    PREFIX ""
    OUTPUT_NAME "mupen64plus-audio-dump"
)
if (APPLE)
    set_target_properties(mupen64plus-audio-dump PROPERTIES SUFFIX ".dylib")
endif()
