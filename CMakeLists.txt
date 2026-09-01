cmake_minimum_required(VERSION 3.20)
project(pjh_platform VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(NOT TARGET pjh_result)
    add_subdirectory(thirdparty/pjh_result EXCLUDE_FROM_ALL)
endif()

add_library(pjh_platform)
add_library(pjh::platform ALIAS pjh_platform)
target_sources(pjh_platform
    PRIVATE
    src/env.cpp
    src/fs.cpp
    src/directory_snapshot.cpp
    src/directory_diff.cpp
    src/directory_status.cpp
    src/error_mapping.cpp
    src/file_watcher/file_watcher.cpp
    src/file_watcher/file_watcher_add.cpp
    src/file_watcher/file_watcher_remove.cpp
    src/file_watcher/file_watcher_poll.cpp
    src/file_watcher/file_watcher_common.cpp
)
target_include_directories(pjh_platform
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(pjh_platform PUBLIC cxx_std_20)
target_link_libraries(pjh_platform PUBLIC pjh_result)

if(APPLE)
    find_library(PJH_PLATFORM_CORESERVICES_FRAMEWORK CoreServices)
    target_link_libraries(pjh_platform PRIVATE ${PJH_PLATFORM_CORESERVICES_FRAMEWORK})
endif()

# CMake < 3.21 无 PROJECT_IS_TOP_LEVEL;顶层判定用版本无关惯用式
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(PJH_PLATFORM_TESTS_DEFAULT ON)
else()
    set(PJH_PLATFORM_TESTS_DEFAULT OFF)
endif()
option(PJH_PLATFORM_BUILD_TESTS "Build tests" ${PJH_PLATFORM_TESTS_DEFAULT})
option(PJH_PLATFORM_BUILD_EXAMPLES "Build examples" OFF)

if(PJH_PLATFORM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PJH_PLATFORM_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

set(pjh_platform_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/pjh_platform)

install(TARGETS pjh_platform EXPORT pjh_platformTargets)
install(EXPORT pjh_platformTargets
    FILE pjh_platformTargets.cmake
    DESTINATION ${pjh_platform_INSTALL_CMAKEDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(cmake/pjh_platformConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/pjh_platformConfig.cmake
    INSTALL_DESTINATION ${pjh_platform_INSTALL_CMAKEDIR})
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/pjh_platformConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/pjh_platformConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/pjh_platformConfigVersion.cmake
    DESTINATION ${pjh_platform_INSTALL_CMAKEDIR})
