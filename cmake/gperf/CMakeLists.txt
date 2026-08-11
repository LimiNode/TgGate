cmake_minimum_required(VERSION 3.21)

project(TgGateGperfTool LANGUAGES CXX)

set(TGGATE_GPERF_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../external/gperf" CACHE PATH "GNU gperf source directory")

if(NOT EXISTS "${TGGATE_GPERF_SOURCE_DIR}/src/main.cc")
    message(FATAL_ERROR "TGGATE_GPERF_SOURCE_DIR must point to the initialized external/gperf submodule")
endif()

add_executable(tggate_gperf_tool
    "${TGGATE_GPERF_SOURCE_DIR}/lib/getline.cc"
    "${TGGATE_GPERF_SOURCE_DIR}/lib/getopt.c"
    "${TGGATE_GPERF_SOURCE_DIR}/lib/getopt1.c"
    "${TGGATE_GPERF_SOURCE_DIR}/lib/hash.cc"
    "${TGGATE_GPERF_SOURCE_DIR}/src/bool-array.cc"
    "${TGGATE_GPERF_SOURCE_DIR}/src/hash-table.cc"
    "${TGGATE_GPERF_SOURCE_DIR}/src/input.cc"
    "${TGGATE_GPERF_SOURCE_DIR}/src/keyword-list.cc"
    "${TGGATE_GPERF_SOURCE_DIR}/src/keyword.cc"
    "${TGGATE_GPERF_SOURCE_DIR}/src/main.cc"
    "${TGGATE_GPERF_SOURCE_DIR}/src/options.cc"
    "${TGGATE_GPERF_SOURCE_DIR}/src/output.cc"
    "${TGGATE_GPERF_SOURCE_DIR}/src/positions.cc"
    "${TGGATE_GPERF_SOURCE_DIR}/src/search.cc"
    "${TGGATE_GPERF_SOURCE_DIR}/src/version.cc")
target_compile_features(tggate_gperf_tool PRIVATE cxx_std_17)
target_include_directories(tggate_gperf_tool PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${TGGATE_GPERF_SOURCE_DIR}/lib"
    "${TGGATE_GPERF_SOURCE_DIR}/src")
set_target_properties(tggate_gperf_tool PROPERTIES
    OUTPUT_NAME gperf
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
