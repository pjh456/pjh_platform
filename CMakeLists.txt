cmake_minimum_required(VERSION 3.20)
project(pjh_platform LANGUAGES CXX)

if(NOT TARGET pjh_result)
    add_subdirectory(thirdparty/pjh_result EXCLUDE_FROM_ALL)
endif()

add_library(pjh_platform)
target_sources(pjh_platform
    PRIVATE
    src/env.cpp
    src/fs.cpp
    src/directory_snapshot.cpp
    src/directory_diff.cpp
    src/directory_status.cpp
    src/file_watcher/file_watcher.cpp
    src/file_watcher/file_watcher_add.cpp
    src/file_watcher/file_watcher_remove.cpp
    src/file_watcher/file_watcher_poll.cpp
    src/file_watcher/file_watcher_common.cpp
)
target_include_directories(pjh_platform
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(pjh_platform PUBLIC cxx_std_20)
target_link_libraries(pjh_platform PUBLIC pjh_result)

if(APPLE)
    find_library(PJH_PLATFORM_CORESERVICES_FRAMEWORK CoreServices)
    target_link_libraries(pjh_platform PUBLIC ${PJH_PLATFORM_CORESERVICES_FRAMEWORK})
endif()

option(PJH_PLATFORM_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(PJH_PLATFORM_BUILD_EXAMPLES "Build examples" OFF)

if(PJH_PLATFORM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PJH_PLATFORM_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
