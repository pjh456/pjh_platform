set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/doctest ${CMAKE_BINARY_DIR}/thirdparty/doctest EXCLUDE_FROM_ALL)

set(PJH_PLATFORM_TEST_SOURCES
    test_main.cpp
    test_platform.cpp
    test_os.cpp
    test_env.cpp
    test_fs.cpp
    test_encoding.cpp
    test_file_watcher.cpp
    test_directory_snapshot.cpp
    test_directory_diff.cpp
    test_directory_status.cpp
)

add_executable(pjh_platform_tests ${PJH_PLATFORM_TEST_SOURCES})
target_link_libraries(pjh_platform_tests PRIVATE pjh_platform doctest)

include(${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/doctest/scripts/cmake/doctest.cmake)
doctest_discover_tests(pjh_platform_tests)
