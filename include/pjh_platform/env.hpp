#ifndef INCLUDE_PJH_PLATFORM_ENV_HPP
#define INCLUDE_PJH_PLATFORM_ENV_HPP

#include <pjh_platform/error.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pjh::platform
{

    class Env
    {
        Env() = delete;

    public:
        [[nodiscard]] static auto get(std::string_view name)
            -> pjh::result::Result<std::string, ErrorCode>;

        static auto set(std::string_view name, std::string_view value)
            -> pjh::result::Result<void, ErrorCode>;

        static auto unset(std::string_view name)
            -> pjh::result::Result<void, ErrorCode>;

        [[nodiscard]] static auto snapshot()
            -> std::unordered_map<std::string, std::string>;

        [[nodiscard]] static auto list()
            -> std::vector<std::pair<std::string, std::string>>;
    };

} // namespace pjh::platform

#endif // INCLUDE_PJH_PLATFORM_ENV_HPP
