# pjh_platform

C++20 cross-platform compatibility library: uniform, platform-independent
interfaces for environment variables, filesystem operations, well-known user
paths, file watching, directory snapshots/diffs, OS detection, and string
encoding. Every fallible API returns `pjh::result::Result<T, ErrorCode>` instead
of throwing.

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

## Consuming an installed package

The library also supports `install` / `find_package(pjh_platform)`. The installed
package config declares `find_dependency(pjh_result)`, so an installed
`pjh_result` must be discoverable on the consumer side.

```cmake
find_package(pjh_platform CONFIG REQUIRED)
target_link_libraries(app PRIVATE pjh_platform)
```

The installed package exports the plain `pjh_platform` target; the `pjh::platform`
alias exists only in the source tree (`add_subdirectory`).

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

```cpp
#include <iostream>
#include <pjh_platform.hpp>

int main(int argc, char **argv)
{
    // Windows: switch the process console code page to UTF-8; POSIX: no-op.
    (void)pjh::platform::Console::enable_utf8();

    if (auto s = pjh::platform::Console::size(); s.is_ok())
        std::cout << "terminal: " << s.unwrap().columns << 'x' << s.unwrap().rows << '\n';

    // UTF-8 arguments (on Windows decoded from the wide command line).
    for (const auto &arg : pjh::platform::Console::utf8_arguments(argc, argv))
        std::cout << arg << '\n';
    return 0;
}
```

## Windows UTF-8 entry point (arguments and console output)

On Windows the C runtime builds `argv` by decoding the wide (UTF-16) command
line with the **process ANSI code page**. Non-ASCII arguments such as Chinese
are therefore already lossy by the time `main` runs (`?` or mojibake). Fixing
this requires the *entry point*, which belongs to your program: pjh_platform
is a library and deliberately does not provide or replace `main`, and it
installs no global code-page hook. It offers two building blocks instead.

### Option 1 — keep `main`, recover UTF-8 with `Console::utf8_arguments`

Recommended and portable: one `main` on every platform. On Windows the
function ignores `argc`/`argv` and re-parses the wide command line.

```cpp
#include <iostream>
#include <pjh_platform/console.hpp>

int main(int argc, char **argv)
{
    // Windows: switch the console code page to UTF-8 when stdout is a real
    // console (a successful no-op for redirected output and on POSIX).
    (void)pjh::platform::Console::enable_utf8();

    // Windows: UTF-8 arguments recovered from the wide command line;
    // POSIX: a verbatim copy of argv.
    for (const auto &arg : pjh::platform::Console::utf8_arguments(argc, argv))
        std::cout << arg << '\n';
    return 0;
}
```

A complete runnable version is `examples/example_utf8_entry.cpp`.

### Option 2 — use the native wide entry point (`wmain`, MSVC)

If you already use the MSVC wide entry point, convert each wide argument
directly; no shell library is involved on this route.

```cpp
#include <iostream>
#include <pjh_platform/console.hpp>
#include <pjh_platform/encoding.hpp>
#include <pjh_platform/platform.hpp>

#if PJH_PLATFORM_WINDOWS
// MSVC CRT extension. A program has one entry point: do not also define main.
int wmain(int argc, wchar_t **argv)
{
    (void)pjh::platform::Console::enable_utf8();
    for (int i = 0; i < argc; ++i)
        std::cout << pjh::platform::Encoding::to_utf8(argv[i]) << '\n';
    return 0;
}
#else
// POSIX has no wmain; use main + Console::utf8_arguments (Option 1).
#endif
```

`wmain` is an MSVC CRT extension, not standard C++; with MinGW it requires
`-municode`, and it does not exist on Linux or macOS.

### `CommandLineToArgvW`, Shell32, and the default libraries

`Console::utf8_arguments` uses `CommandLineToArgvW` (**Shell32**) and releases
the returned buffer with `LocalFree` (**Kernel32**), on Windows. A normal MSVC
console build links Shell32 through the toolchain's default libraries
(CMake's MSVC platform modules put
`shell32.lib` in `CMAKE_*_STANDARD_LIBRARIES`), so no extra CMake line is
needed. Link `shell32.lib` explicitly if you trim the default libraries
(`/NODEFAULTLIB`, `-nostdlib`), use a non-CMake MSVC build that does not add
the Win32 default set, or build with MinGW (`-lshell32`). Option 2 (`wmain`)
has no Shell32 dependency at all.

### Source encoding under MSVC

`enable_utf8` only tells the console to interpret the bytes you write as
UTF-8; your program must actually write UTF-8. If a source file contains
non-ASCII narrow literals such as `"中文"`, compile MSVC with `/utf-8` (or save
the file as UTF-8 with a BOM); otherwise MSVC decodes the source with the
system code page and mangles the literal. In C++20, `u8"..."` has type
`const char8_t[]` and is not directly printable, so byte-exact `\x` escapes
are the portable way to keep a source file ASCII — see
`examples/example_utf8_entry.cpp`.

## Features

| Module | Header | Description |
|--------|--------|-------------|
| `Platform` | `pjh_platform/platform.hpp` | `PJH_PLATFORM_*` detection macros (not included by the umbrella; include it directly) |
| `Os` | `pjh_platform/os.hpp` | Compile-time OS, architecture, and endianness constants |
| `Encoding` | `pjh_platform/encoding.hpp` | UTF-8 ↔ wide-string conversion |
| `Env` | `pjh_platform/env.hpp` | Environment variable get/set/unset/snapshot/list |
| `Fs` | `pjh_platform/fs.hpp` | Filesystem operations and lexical path utilities |
| `Paths` | `pjh_platform/paths.hpp` | Executable path and per-user data/config/cache directories (XDG / %APPDATA% / ~/Library) |
| `Console` | `pjh_platform/console.hpp` | Console UTF-8 enablement, TTY/size/ANSI probes, wide-argv → UTF-8 |
| `Clock` | `pjh_platform/clock.hpp` | Wall-clock and monotonic time; UTC ISO 8601 timestamps (millisecond precision) |
| `FileWatcher` | `pjh_platform/file_watcher.hpp` | Poll-based file/directory change monitoring |
| `DirectorySnapshot` | `pjh_platform/directory_snapshot.hpp` | Point-in-time directory capture with optional content hashing |
| `DirectoryDiff` | `pjh_platform/directory_diff.hpp` | Snapshot comparison: Created/Deleted/Modified and rename detection |
| `DirectoryStatus` | `pjh_platform/directory_status.hpp` | Size/extension/largest-file aggregation over a snapshot |
| `ErrorCode` | `pjh_platform/error.hpp` | The 12-code error vocabulary used by every fallible API |

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Tests are built by default; they use [doctest](https://github.com/doctest/doctest),
fetched by CMake at configure time via FetchContent (tag `v2.5.0`,
`tests/CMakeLists.txt`) — no submodule initialization is needed. When the library
is consumed as a subproject, tests default OFF and doctest is not fetched.

To also build the sample programs, configure with `-DPJH_PLATFORM_BUILD_EXAMPLES=ON`;
the `example_env`, `example_fs`, `example_console`, `example_paths`, and
`example_utf8_entry` executables are then built alongside the library and
tests. Full runnable programs: see `examples/`.

## License

MIT — see [LICENSE](LICENSE).
