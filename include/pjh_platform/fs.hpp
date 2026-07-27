#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace pjh::platform::fs {

auto current_path() -> std::filesystem::path;

auto create_directories(const std::filesystem::path& p) -> bool;

auto remove_all(const std::filesystem::path& p) -> std::uintmax_t;

auto exists(const std::filesystem::path& p) -> bool;

auto is_regular_file(const std::filesystem::path& p) -> bool;

auto is_directory(const std::filesystem::path& p) -> bool;

auto file_size(const std::filesystem::path& p) -> std::uintmax_t;

auto read_file(const std::filesystem::path& p) -> std::optional<std::string>;

auto write_file(const std::filesystem::path& p, std::string_view content) -> std::error_code;

auto list_directory(const std::filesystem::path& p) -> std::optional<std::vector<std::filesystem::path>>;

auto temp_directory() -> std::filesystem::path;

auto home_directory() -> std::optional<std::filesystem::path>;

} // namespace pjh::platform::fs
