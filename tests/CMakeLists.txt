include(FetchContent)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.0)

set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)

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

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(pjh_platform_tests)
