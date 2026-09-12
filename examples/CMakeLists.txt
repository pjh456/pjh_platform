add_executable(example_env example_env.cpp)
target_link_libraries(example_env PRIVATE pjh_platform)

add_executable(example_fs example_fs.cpp)
target_link_libraries(example_fs PRIVATE pjh_platform)

add_executable(example_console example_console.cpp)
target_link_libraries(example_console PRIVATE pjh_platform)
