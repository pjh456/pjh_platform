#ifndef INCLUDE_PJH_PLATFORM_PATHS_HPP
#define INCLUDE_PJH_PLATFORM_PATHS_HPP

#include <filesystem>
#include <pjh_platform/error.hpp>
#include <string_view>

namespace pjh::platform
{

    /// @brief Well-known paths: the running executable and the per-user
    ///        data / config / cache directories.
    ///
    /// @details Static-only utility class. Every accessor is a pure query: it
    ///          resolves a path from the environment and the operating system
    ///          but never creates, probes, or otherwise modifies the filesystem;
    ///          use `Fs::create_directories` before writing into a returned
    ///          directory. The per-user accessors append @p app_name as one path
    ///          component when it is non-empty and return the base directory
    ///          unchanged when it is empty.
    ///
    ///          Environment convention: a variable that is unset **or set to an
    ///          empty value** counts as unset and triggers the next fallback
    ///          (the XDG Base Directory Specification behavior); when no
    ///          fallback source exists the accessor fails with
    ///          `Failure(NotFound)`. Values are used verbatim and are not
    ///          normalized, so the result is absolute whenever the environment
    ///          supplies absolute locations (the normal case).
    ///
    ///          `executable_dir` is always exactly
    ///          `executable_path().parent_path()`.
    ///
    /// @platform Windows, Linux, macOS.
    class Paths
    {
        Paths() = delete;

    public:
        /**
         * @brief Absolute path of the running executable.
         *
         * @details Linux: reads the `/proc/self/exe` symlink into a growing
         *          buffer until it is not truncated (the kernel resolves the
         *          link, so a symlink invocation yields the real binary path).
         *          macOS: `_NSGetExecutablePath` into a growing buffer, then
         *          `weakly_canonical` (falling back to `lexically_normal` when
         *          canonicalization fails), so symlinks in the reported path are
         *          resolved. Windows: `GetModuleFileNameW(nullptr, ...)` with a
         *          growing buffer; the result is the path used to load the
         *          module, and long paths beyond `MAX_PATH` require the process
         *          to be long-path aware. The returned `std::filesystem::path`
         *          is native; `u8string()` yields UTF-8 on every platform.
         *
         * @return `Ok(path)` with a non-empty absolute path; `Failure(NotFound)`
         *         when the kernel cannot report the executable (for example
         *         `/proc` is not mounted); `Failure(LimitReached)` when the path
         *         exceeds the library's internal buffer cap; another mapped code
         *         for other platform failures.
         *
         * @exception Never throws (may allocate).
         *
         * @sideeffect None; read-only.
         *
         * @platform Windows, Linux, macOS. The exact resolution differs per
         *           platform (see @details).
         */
        [[nodiscard]] static auto executable_path()
            -> pjh::result::Result<std::filesystem::path, ErrorCode>;

        /**
         * @brief Parent directory of `executable_path()`.
         *
         * @details Computed from the same query as `executable_path()`, so the
         *          result is exactly `executable_path().parent_path()` on every
         *          platform. Use it to locate resources shipped next to the
         *          binary instead of resolving them relative to the current
         *          working directory.
         *
         * @return `Ok(dir)`; the same `Failure` outcomes as `executable_path()`.
         *
         * @exception Never throws (may allocate).
         *
         * @sideeffect None; read-only.
         *
         * @platform Windows, Linux, macOS.
         */
        [[nodiscard]] static auto executable_dir()
            -> pjh::result::Result<std::filesystem::path, ErrorCode>;

        /**
         * @brief Per-user configuration directory for @p app_name.
         *
         * @details Linux/other POSIX: `$XDG_CONFIG_HOME` when set and non-empty,
         *          otherwise `$HOME/.config`. macOS: `$HOME/Library/Application
         *          Support` (macOS has no separate per-app config root; property
         *          lists under `~/Library/Preferences` are managed by the system
         *          preferences daemon and are deliberately not used here).
         *          Windows: `%APPDATA%` (the roaming profile) — see `data_dir`.
         *          @p app_name is appended as one component when non-empty.
         *
         * @param app_name UTF-8 application name; empty returns the base
         *                 directory without an application component.
         *
         * @return `Ok(dir)`; `Failure(NotFound)` when the base variable(s) are
         *         unset or empty; never creates the directory.
         *
         * @exception Never throws (may allocate).
         *
         * @sideeffect None; read-only.
         *
         * @platform Windows, Linux, macOS.
         */
        [[nodiscard]] static auto config_dir(std::string_view app_name)
            -> pjh::result::Result<std::filesystem::path, ErrorCode>;

        /**
         * @brief Per-user data directory for @p app_name.
         *
         * @details Linux/other POSIX: `$XDG_DATA_HOME` when set and non-empty,
         *          otherwise `$HOME/.local/share` (the user-writable XDG data
         *          root; the `$XDG_DATA_DIRS` search list is not consulted).
         *          macOS: `$HOME/Library/Application Support`. Windows:
         *          `%APPDATA%` (the roaming profile). @p app_name is appended
         *          as one component when non-empty.
         *
         * @param app_name UTF-8 application name; empty returns the base
         *                 directory without an application component.
         *
         * @return `Ok(dir)`; `Failure(NotFound)` when the base variable(s) are
         *         unset or empty; never creates the directory.
         *
         * @exception Never throws (may allocate).
         *
         * @sideeffect None; read-only.
         *
         * @platform Windows, Linux, macOS.
         */
        [[nodiscard]] static auto data_dir(std::string_view app_name)
            -> pjh::result::Result<std::filesystem::path, ErrorCode>;

        /**
         * @brief Per-user cache directory for @p app_name.
         *
         * @details Linux/other POSIX: `$XDG_CACHE_HOME` when set and non-empty,
         *          otherwise `$HOME/.cache`. macOS: `$HOME/Library/Caches`.
         *          Windows: `%LOCALAPPDATA%` (the non-roaming local profile),
         *          falling back to `%APPDATA%` when `LOCALAPPDATA` is unset or
         *          empty, then appending @p app_name (when non-empty) and the
         *          literal component `Cache`. Caches are machine-local and must
         *          not roam, hence the Windows local-profile preference.
         *
         * @param app_name UTF-8 application name; empty yields the base
         *                 directory (Windows: `<base>\Cache`).
         *
         * @return `Ok(dir)`; `Failure(NotFound)` when the base variable(s) are
         *         unset or empty; never creates the directory.
         *
         * @exception Never throws (may allocate).
         *
         * @sideeffect None; read-only.
         *
         * @platform Windows, Linux, macOS.
         */
        [[nodiscard]] static auto cache_dir(std::string_view app_name)
            -> pjh::result::Result<std::filesystem::path, ErrorCode>;
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_PATHS_HPP
