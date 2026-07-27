cmake_minimum_required(VERSION 3.20)
project(pjh_platform LANGUAGES CXX)

add_library(pjh_platform)
target_sources(pjh_platform
    PRIVATE
    src/env.cpp
    src/fs.cpp
)
target_include_directories(pjh_platform
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(pjh_platform PUBLIC cxx_std_20)

option(PJH_PLATFORM_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(PJH_PLATFORM_BUILD_EXAMPLES "Build examples" OFF)

if(PJH_PLATFORM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PJH_PLATFORM_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
