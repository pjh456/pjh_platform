#ifndef INCLUDE_PJH_PLATFORM_ENV_HPP
#define INCLUDE_PJH_PLATFORM_ENV_HPP

#include <pjh_platform/error.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pjh::platform
{

    /// @brief Cross-platform access to the process environment variables.
    ///
    /// @details Static-only utility class. On Windows all operations go through
    ///          the wide-char API and convert between UTF-16 and UTF-8; on
    ///          POSIX they use `getenv`/`setenv`/`unsetenv` and `environ`.
    ///          The library treats UTF-8 as the canonical string encoding.
    ///          There is no hidden caching: `get` re-reads the live environment
    ///          on every call, while `snapshot` and `list` return independent
    ///          point-in-time copies.
    ///
    /// @warning The process environment is shared, unsynchronized global state.
    ///          `Env` performs no internal locking: calling any member
    ///          concurrently with `set`/`unset` (or with the platform's own
    ///          environment functions) from another thread is a data race. On
    ///          POSIX, `snapshot`/`list` iterate the `environ` array, which a
    ///          concurrent `setenv`/`unsetenv` may reallocate. Callers that
    ///          share the environment across threads must synchronize
    ///          externally.
    ///
    /// @platform Windows, Linux, macOS.
    class Env
    {
        Env() = delete;

    public:
        /**
         * @brief Returns the value of the environment variable @p name.
         *
         * @details Windows: `GetEnvironmentVariableW`. POSIX: `getenv`. The
         *          name is copied to a null-terminated buffer before use, so
         *          passing a `std::string_view` that is not null-terminated is
         *          safe. A value cannot contain an embedded NUL byte: the value is
         *          truncated at the first NUL, because the environment is NUL-terminated.
         *          The value is read from the live environment at call time; it
         *          is not served from a previously captured `snapshot()`, and
         *          the returned string is an independent copy that stays valid
         *          after the environment changes.
         *
         *          A variable that is set with an empty value is distinct from a
         *          missing variable: the former returns `Ok("")`, the latter
         *          `Failure(NotFound)`. An empty @p name is not a valid variable
         *          name and returns `Failure(NotFound)` on all platforms.
         *
         *          On Windows the lookup is case-insensitive; on POSIX it is
         *          case-sensitive. The map returned by `snapshot()` never folds
         *          case on any platform.
         *
         * @param name Variable name; must be non-empty.
         *
         * @return `Ok(value)` on success (possibly `Ok("")` for a set-but-empty
         *         variable); `Failure(NotFound)` if @p name is empty or not set.
         *
         * @exception Never throws.
         *
         * @sideeffect None; read-only.
         *
         * @platform All supported platforms. UTF-16 to UTF-8 conversion on
         *           Windows.
         */
        [[nodiscard]] static auto get(std::string_view name)
            -> pjh::result::Result<std::string, ErrorCode>;

        /**
         * @brief Sets the environment variable @p name to @p value.
         *
         * @details Windows: `SetEnvironmentVariableW`. POSIX: `setenv` with
         *          overwrite enabled, so an existing variable is replaced. An
         *          empty @p name is rejected, returning `Failure(IoError)`, and
         *          the environment is left unchanged. An empty @p value is
         *          stored as a present variable with an empty value (it does not
         *          remove the variable); `get` then returns `Ok("")` rather than
         *          `Failure(NotFound)`.
         *
         * @param name Variable name; must be non-empty.
         * @param value New value; an empty value is stored as-is.
         *
         * @return `Ok()` on success; `Failure(IoError)` if @p name is empty or
         *         the underlying call fails.
         *
         * @exception Never throws.
         *
         * @sideeffect Mutates the process environment; visible to the current
         *            process and to subsequently created child processes.
         *            Concurrent with other `Env` members from another thread is
         *            a data race (see the class-level `@warning`).
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto set(std::string_view name, std::string_view value)
            -> pjh::result::Result<void, ErrorCode>;

        /**
         * @brief Removes the environment variable @p name.
         *
         * @details Windows: `SetEnvironmentVariableW` with a null value.
         *          POSIX: `unsetenv`. Removing a variable that does not exist
         *          succeeds. Removing a set-but-empty variable removes it, after
         *          which `get` returns `Failure(NotFound)`; this differs from
         *          `set(name, "")`, which keeps the variable present with an
         *          empty value. An empty @p name is rejected, returning
         *          `Failure(IoError)`, and the environment is left unchanged.
         *
         * @param name Variable to remove; must be non-empty.
         *
         * @return `Ok()` on success; `Failure(IoError)` if @p name is empty or
         *         the underlying call fails.
         *
         * @exception Never throws.
         *
         * @sideeffect Mutates the process environment; visible to the current
         *            process and to subsequently created child processes.
         *            Concurrent with other `Env` members from another thread is
         *            a data race (see the class-level `@warning`).
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto unset(std::string_view name)
            -> pjh::result::Result<void, ErrorCode>;

        /**
         * @brief Returns a copy of the entire environment as a name-to-value
         *        map.
         *
         * @details Windows: `GetEnvironmentStringsW`. POSIX: iterates the
         *          `environ` array. Entries without a `=` are skipped. Values
         *          are decoded to UTF-8 on Windows. The result is an independent
         *          point-in-time copy: later `set`/`unset` calls do not change
         *          it, and it does not track the live environment (use `get`
         *          for a live read).
         *
         *          Keys keep the native spelling of the variable names and map
         *          lookups are case-sensitive on every platform, including
         *          Windows. Windows name resolution through `get` folds case;
         *          the map does not, so a consumer that needs the Windows
         *          case-insensitive semantic must resolve names through `get`
         *          even when it captured the values here. On Windows the
         *          environment block also contains drive current-directory
         *          pseudo-entries shaped `=C:=C:\...`; because such an entry has
         *          `=` at index 0, it is surfaced with an empty-string key.
         *          POSIX does not use these pseudo-entries.
         *
         * @return `std::unordered_map` of all variables; empty when the
         *         environment block is unavailable.
         *
         * @exception Never throws (may allocate).
         *
         * @sideeffect None. Concurrent mutation by another thread is a data
         *            race (see the class-level `@warning`): a snapshot taken
         *            during mutation may observe a partial environment.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto snapshot() -> std::unordered_map<std::string, std::string>;

        /**
         * @brief Returns the entire environment as an ordered list.
         *
         * @details Same data source and same point-in-time isolation as
         *          `snapshot()`, but preserves iteration order, which is useful
         *          for reproducing the environment block or for serialization.
         *          Keys keep their native spelling and are not case-folded;
         *          Windows drive current-directory pseudo-entries appear under
         *          an empty-string key (see `snapshot()`).
         *
         * @return Vector of `(name, value)` pairs.
         *
         * @exception Never throws (may allocate).
         *
         * @sideeffect None. Concurrent mutation by another thread is a data
         *            race (see the class-level `@warning`).
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto list() -> std::vector<std::pair<std::string, std::string>>;
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_ENV_HPP
