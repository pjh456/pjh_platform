# pjh_platform

C++20 cross-platform compatibility library: uniform, platform-independent
interfaces for environment variables, filesystem operations, file watching,
directory snapshots/diffs, OS detection, and string encoding. Every fallible API
returns `pjh::result::Result<T, ErrorCode>` instead of throwing.

Consumed as a CMake subdirectory. Windows, Linux, and macOS are all tested in
CI (Linux GCC, Linux Clang, macOS, Windows MSVC).

## Requirements

- C++20 compiler
- CMake 3.20+
- One of: Windows, Linux, macOS (all three are CI-tested)

## Integration

```cmake
add_subdirectory(thirdparty/pjh_platform)
target_link_libraries(your_target PRIVATE pjh_platform)
```

When consumed as a subdirectory, tests and examples are **not** built by default.

## Quick start

```cpp
#include <iostream>
#include <pjh_platform.hpp>

namespace plat = pjh::platform;

int main()
{
    // Environment variables
    if (auto r = plat::Env::set("MY_VAR", "value"); r.is_err())
        return 1;
    if (auto v = plat::Env::get("MY_VAR"); v.is_ok())
        std::cout << "MY_VAR = " << v.unwrap() << '\n';

    // Filesystem (fallible operations return Result and never throw)
    std::cout << "cwd: " << plat::Fs::current_path() << '\n';
    if (auto r = plat::Fs::create_directories(plat::Fs::temp_directory() / "mydir");
        r.is_err())
        return 1;

    // OS detection (compile-time constants)
    if constexpr (plat::Os::is_linux)
        std::cout << "running on Linux\n";

    return 0;
}
```

```cpp
#include <chrono>
#include <iostream>
#include <pjh_platform.hpp>

namespace plat = pjh::platform;

int main()
{
    plat::FileWatcher watcher;
    // Watch the top level of the system temp directory (always exists).
    if (auto r = watcher.add(plat::Fs::temp_directory(), false); r.is_err())
        return 1;

    // Blocks up to 100 ms; timing out is not an error (empty result).
    if (auto e = watcher.poll(std::chrono::milliseconds(100)); e.is_ok())
        for (const auto &event : e.unwrap())
            std::cout << "event: " << event.path << '\n';

    return 0;
}
```

`FileWatcher` is poll-based and not thread-safe; on macOS the watcher must
be created and polled on the same thread (its run loop). See
`pjh_platform/file_watcher.hpp` for per-platform notes.

## Features

| Module | Header | Description |
|--------|--------|-------------|
| `Platform` | `pjh_platform/platform.hpp` | `PJH_PLATFORM_*` detection macros (not included by the umbrella; include it directly) |
| `Os` | `pjh_platform/os.hpp` | Compile-time OS, architecture, and endianness constants |
| `Encoding` | `pjh_platform/encoding.hpp` | UTF-8 ↔ wide-string conversion |
| `Env` | `pjh_platform/env.hpp` | Environment variable get/set/unset/snapshot/list |
| `Fs` | `pjh_platform/fs.hpp` | Filesystem operations and lexical path utilities |
| `FileWatcher` | `pjh_platform/file_watcher.hpp` | Poll-based file/directory change monitoring |
| `DirectorySnapshot` | `pjh_platform/directory_snapshot.hpp` | Point-in-time directory capture with optional content hashing |
| `DirectoryDiff` | `pjh_platform/directory_diff.hpp` | Snapshot comparison: Created/Deleted/Modified and rename detection |
| `DirectoryStatus` | `pjh_platform/directory_status.hpp` | Size/extension/largest-file aggregation over a snapshot |
| `ErrorCode` | `pjh_platform/error.hpp` | The 11-code error vocabulary used by every fallible API |

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Tests are built by default; they use [doctest](https://github.com/doctest/doctest),
vendored as a git submodule — run `git submodule update --init --depth 1` first
after cloning.

To also build the sample programs, configure with `-DPJH_PLATFORM_BUILD_EXAMPLES=ON`;
the `example_env` and `example_fs` executables are then built alongside the library
and tests. Full runnable programs: see `examples/`.

## License

MIT — see [LICENSE](LICENSE).
