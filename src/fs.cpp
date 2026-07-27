#include <pjh_platform/env.hpp>
#include <pjh_platform/error.hpp>
#include <pjh_platform/fs.hpp>
#include <pjh_platform/platform.hpp>

#include <fstream>
#include <iterator>

namespace pjh::platform::fs {

auto current_path() -> std::filesystem::path {
    return std::filesystem::current_path();
}

auto create_directories(const std::filesystem::path& p) -> bool {
    return std::filesystem::create_directories(p);
}

auto remove_all(const std::filesystem::path& p) -> std::uintmax_t {
    std::error_code ec;
    auto count = std::filesystem::remove_all(p, ec);
    return ec ? 0 : count;
}

auto exists(const std::filesystem::path& p) -> bool {
    return std::filesystem::exists(p);
}

auto is_regular_file(const std::filesystem::path& p) -> bool {
    return std::filesystem::is_regular_file(p);
}

auto is_directory(const std::filesystem::path& p) -> bool {
    return std::filesystem::is_directory(p);
}

auto file_size(const std::filesystem::path& p) -> std::uintmax_t {
    std::error_code ec;
    auto sz = std::filesystem::file_size(p, ec);
    return ec ? static_cast<std::uintmax_t>(-1) : sz;
}

auto read_file(const std::filesystem::path& p) -> std::optional<std::string> {
    std::ifstream file(p, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;
    auto size = file.tellg();
    file.seekg(0);
    std::string content(static_cast<std::size_t>(size), '\0');
    file.read(content.data(), size);
    return content;
}

auto write_file(const std::filesystem::path& p, std::string_view content) -> std::error_code {
    std::ofstream file(p, std::ios::binary);
    if (!file) return errc::io_error;
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return file ? std::error_code{} : errc::io_error;
}

auto list_directory(const std::filesystem::path& p) -> std::optional<std::vector<std::filesystem::path>> {
    std::error_code ec;
    std::vector<std::filesystem::path> entries;
    for (const auto& entry : std::filesystem::directory_iterator(p, ec)) {
        entries.push_back(entry.path());
    }
    if (ec) return std::nullopt;
    return entries;
}

auto temp_directory() -> std::filesystem::path {
    return std::filesystem::temp_directory_path();
}

auto home_directory() -> std::optional<std::filesystem::path> {
    auto home = Env::get("HOME");
    if (home) return std::filesystem::path(*home);
#if PJH_PLATFORM_WINDOWS
    auto userprofile = Env::get("USERPROFILE");
    if (userprofile) return std::filesystem::path(*userprofile);
#endif
    return std::nullopt;
}

} // namespace pjh::platform::fs
