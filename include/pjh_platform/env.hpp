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
         *          safe. Returns `Failure(NotFound)` when the variable is not
         *          set. On Windows the lookup is case-insensitive; on POSIX it
         *          is case-sensitive. The returned value is a snapshot and
         *          remains valid even if the environment changes afterwards.
         *
         * @param name Variable name.
         *
         * @return `Ok(value)` on success; `Failure(NotFound)` if @p name is
         *         not set.
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
         *          overwrite enabled, so an existing variable is replaced.
         *
         * @param name Variable name.
         * @param value New value.
         *
         * @return `Ok()` on success; `Failure(IoError)` if the underlying
         *         call fails.
         *
         * @exception Never throws.
         *
         * @sideeffect Mutates the process environment; visible to the current
         *            process and to subsequently created child processes.
         *
         * @platform All supported platforms.
         */
        static auto set(std::string_view name, std::string_view value)
            -> pjh::result::Result<void, ErrorCode>;

        /**
         * @brief Removes the environment variable @p name.
         *
         * @details Windows: `SetEnvironmentVariableW` with a null value.
         *          POSIX: `unsetenv`. Removing a variable that does not exist
         *          succeeds.
         *
         * @param name Variable to remove.
         *
         * @return `Ok()` on success; `Failure(IoError)` if the underlying
         *         call fails.
         *
         * @exception Never throws.
         *
         * @sideeffect Mutates the process environment; visible to the current
         *            process and to subsequently created child processes.
         *
         * @platform All supported platforms.
         */
        static auto unset(std::string_view name)
            -> pjh::result::Result<void, ErrorCode>;

        /**
         * @brief Returns a copy of the entire environment as a name-to-value
         *        map.
         *
         * @details Windows: `GetEnvironmentStringsW`. POSIX: iterates the
         *          `environ` array. Entries without a `=` are skipped. Values
         *          are decoded to UTF-8 on Windows.
         *
         * @return `std::unordered_map` of all variables; empty when the
         *         environment block is unavailable.
         *
         * @exception Never throws (may allocate).
         *
         * @sideeffect None.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto snapshot()
            -> std::unordered_map<std::string, std::string>;

        /**
         * @brief Returns the entire environment as an ordered list.
         *
         * @details Same data source as `snapshot()` but preserves iteration
         *          order, which is useful for reproducing the environment
         *          block or for serialization.
         *
         * @return Vector of `(name, value)` pairs.
         *
         * @exception Never throws (may allocate).
         *
         * @sideeffect None.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto list()
            -> std::vector<std::pair<std::string, std::string>>;
    };

} // namespace pjh::platform

#endif // INCLUDE_PJH_PLATFORM_ENV_HPP
