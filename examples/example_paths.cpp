// example_paths.cpp — non-interactive Paths capability demo.
#include <filesystem>
#include <iostream>
#include <pjh_platform/error.hpp>
#include <pjh_platform/paths.hpp>
#include <string>

namespace
{
    auto to_utf8(const std::filesystem::path &p) -> std::string
    {
        auto u8 = p.u8string();
        return std::string(u8.begin(), u8.end());
    }

    template <typename Result>
    void print_path(const char *label, const Result &r)
    {
        if (r.is_ok())
            std::cout << label << ": " << to_utf8(r.unwrap()) << '\n';
        else
            std::cout << label << ": code " << static_cast<int>(r.unwrap_err()) << '\n';
    }
}  // namespace

int main()
{
    using pjh::platform::Paths;

    print_path("executable_path", Paths::executable_path());
    print_path("executable_dir", Paths::executable_dir());
    print_path("config_dir", Paths::config_dir("pjh_example"));
    print_path("data_dir", Paths::data_dir("pjh_example"));
    print_path("cache_dir", Paths::cache_dir("pjh_example"));

    std::cout << "example_paths: ok\n";
    return 0;
}
