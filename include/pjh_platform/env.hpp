#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace pjh::platform::env {

auto get(std::string_view name) -> std::optional<std::string>;

auto set(std::string_view name, std::string_view value) -> std::error_code;

auto unset(std::string_view name) -> std::error_code;

} // namespace pjh::platform::env
