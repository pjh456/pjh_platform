#ifndef INCLUDE_PJH_PLATFORM_ENV_HPP
#define INCLUDE_PJH_PLATFORM_ENV_HPP

#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace pjh::platform
{

    class Env
    {
        Env() = delete;

    public:
        [[nodiscard]] static auto get(std::string_view name)
            -> std::optional<std::string>;

        static auto set(std::string_view name, std::string_view value) -> std::error_code;

        static auto unset(std::string_view name) -> std::error_code;

        [[nodiscard]] static auto snapshot()
            -> std::unordered_map<std::string, std::string>;

        [[nodiscard]] static auto list()
            -> std::vector<std::pair<std::string, std::string>>;
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_ENV_HPP
