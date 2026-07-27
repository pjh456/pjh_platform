# pjh_platform

C++20 cross-platform compatibility layer. Provides uniform, platform-independent
interfaces for environment variables, filesystem operations, and OS detection.

Designed as a header-first library (with minimal compilation units for platform-
specific logic) that can be consumed as a CMake subdirectory or installed as a
system library.

## Requirements

- C++20 compiler
- CMake 3.20+

## Integration

```cmake
add_subdirectory(thirdparty/pjh_platform)
target_link_libraries(your_target PRIVATE pjh_platform)
```

When consumed as a subdirectory, tests and examples are **not** built by default.

## Quick start

```cpp
#include "pjh_platform.hpp"

namespace plat = pjh::platform;

// Environment variables
auto home = plat::env::get("HOME");
if (home) {
    // use *home
}

plat::env::set("MY_VAR", "value");

// Filesystem
auto cwd = plat::fs::current_path();
bool ok = plat::fs::create_directories("/tmp/mydir");

// OS detection
if constexpr (plat::Os::is_linux) {
    // Linux-specific path
}
```

## Features

| Module | Header | Description |
|--------|--------|-------------|
| `pjh::platform::env` | `pjh_platform/env.hpp` | Environment variable get/set/iterate |
| `pjh::platform::fs` | `pjh_platform/fs.hpp` | Filesystem path & file operations |
| `pjh::platform::Os` | `pjh_platform/os.hpp` | Compile-time & runtime OS detection |
| `pjh::platform::error` | `pjh_platform/error.hpp` | Platform error codes & result type |

## Building tests & examples

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Tests use [doctest](https://github.com/doctest/doctest), vendored as a git submodule
under `thirdparty/doctest` — run `git submodule update --init --depth 1` first.

## License

MIT — see [LICENSE](LICENSE).
