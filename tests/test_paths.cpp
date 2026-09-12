#include <doctest/doctest.h>

#include <filesystem>
#include <pjh_platform/env.hpp>
#include <pjh_platform/error.hpp>
#include <pjh_platform/fs.hpp>
#include <pjh_platform/paths.hpp>
#include <pjh_platform/platform.hpp>
#include <string>

using pjh::platform::Env;
using pjh::platform::ErrorCode;
using pjh::platform::Fs;
using pjh::platform::Paths;

namespace
{
    // Restores one environment variable to its pre-test state (its value if it
    // was set, absence if it was not) on scope exit, so a REQUIRE failure's
    // unwind cannot leak a mutated HOME/XDG_*/APPDATA/LOCALAPPDATA into later
    // cases. Same RAII shape as tests/test_fs.cpp (task 34 precedent).
    struct EnvRestore
    {
        std::string name;
        bool had;
        std::string value;

        ~EnvRestore()
        {
            if (had)
                (void)Env::set(name, value);
            else
                (void)Env::unset(name);
        }
    };

    auto capture_env(const char *name) -> EnvRestore
    {
        auto cur = Env::get(name);
        if (cur.is_ok())
            return EnvRestore{name, true, cur.unwrap()};
        return EnvRestore{name, false, {}};
    }

    // Native path -> UTF-8 for Env::set (which always takes UTF-8). Plain
    // .string() would use the active code page on Windows.
    auto path_to_utf8(const std::filesystem::path &p) -> std::string
    {
        auto u8 = p.u8string();
        return std::string(u8.begin(), u8.end());
    }
}  // namespace

TEST_CASE("Paths::executable_path returns a non-empty absolute regular file")
{
    auto r = Paths::executable_path();
    REQUIRE(r.is_ok());
    auto path = r.unwrap();
    CHECK(!path.empty());
    CHECK(path.is_absolute());
    CHECK(Fs::is_regular_file(path));
}

TEST_CASE("Paths::executable_dir is the parent of executable_path")
{
    auto path_result = Paths::executable_path();
    auto dir_result = Paths::executable_dir();
    REQUIRE(path_result.is_ok());
    REQUIRE(dir_result.is_ok());
    auto path = path_result.unwrap();
    auto dir = dir_result.unwrap();
    CHECK(!dir.empty());
    CHECK_EQ(dir, path.parent_path());
    CHECK(Fs::is_directory(dir));
}

#if PJH_PLATFORM_LINUX

TEST_CASE("Paths::data_dir honors XDG_DATA_HOME when set")
{
    auto base = Fs::temp_directory() / "pjh_platform_paths_data_xdg";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    auto restore = capture_env("XDG_DATA_HOME");
    REQUIRE(Env::set("XDG_DATA_HOME", base.string()).is_ok());

    auto r = Paths::data_dir("app");
    REQUIRE(r.is_ok());
    CHECK_EQ(r.unwrap(), base / "app");
}

TEST_CASE("Paths::config_dir honors XDG_CONFIG_HOME when set")
{
    auto base = Fs::temp_directory() / "pjh_platform_paths_config_xdg";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    auto restore = capture_env("XDG_CONFIG_HOME");
    REQUIRE(Env::set("XDG_CONFIG_HOME", base.string()).is_ok());

    auto r = Paths::config_dir("app");
    REQUIRE(r.is_ok());
    CHECK_EQ(r.unwrap(), base / "app");
}

TEST_CASE("Paths::cache_dir honors XDG_CACHE_HOME when set")
{
    auto base = Fs::temp_directory() / "pjh_platform_paths_cache_xdg";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    auto restore = capture_env("XDG_CACHE_HOME");
    REQUIRE(Env::set("XDG_CACHE_HOME", base.string()).is_ok());

    auto r = Paths::cache_dir("app");
    REQUIRE(r.is_ok());
    CHECK_EQ(r.unwrap(), base / "app");
}

TEST_CASE("Paths::data_dir falls back to HOME/.local/share when XDG_DATA_HOME is unset or empty")
{
    auto home = Fs::temp_directory() / "pjh_platform_paths_data_home";
    std::error_code ec;
    std::filesystem::remove_all(home, ec);
    auto home_restore = capture_env("HOME");
    auto xdg_restore = capture_env("XDG_DATA_HOME");
    REQUIRE(Env::set("HOME", home.string()).is_ok());
    REQUIRE(Env::unset("XDG_DATA_HOME").is_ok());

    auto unset_result = Paths::data_dir("app");
    REQUIRE(unset_result.is_ok());
    CHECK_EQ(unset_result.unwrap(), home / ".local" / "share" / "app");

    // XDG: set-but-empty counts as unset.
    REQUIRE(Env::set("XDG_DATA_HOME", "").is_ok());
    auto empty_result = Paths::data_dir("app");
    REQUIRE(empty_result.is_ok());
    CHECK_EQ(empty_result.unwrap(), home / ".local" / "share" / "app");
}

TEST_CASE("Paths::config_dir falls back to HOME/.config when XDG_CONFIG_HOME is unset")
{
    auto home = Fs::temp_directory() / "pjh_platform_paths_config_home";
    std::error_code ec;
    std::filesystem::remove_all(home, ec);
    auto home_restore = capture_env("HOME");
    auto xdg_restore = capture_env("XDG_CONFIG_HOME");
    REQUIRE(Env::set("HOME", home.string()).is_ok());
    REQUIRE(Env::unset("XDG_CONFIG_HOME").is_ok());

    auto unset_result = Paths::config_dir("app");
    REQUIRE(unset_result.is_ok());
    CHECK_EQ(unset_result.unwrap(), home / ".config" / "app");

    REQUIRE(Env::set("XDG_CONFIG_HOME", "").is_ok());
    auto empty_result = Paths::config_dir("app");
    REQUIRE(empty_result.is_ok());
    CHECK_EQ(empty_result.unwrap(), home / ".config" / "app");
}

TEST_CASE("Paths::cache_dir falls back to HOME/.cache when XDG_CACHE_HOME is unset")
{
    auto home = Fs::temp_directory() / "pjh_platform_paths_cache_home";
    std::error_code ec;
    std::filesystem::remove_all(home, ec);
    auto home_restore = capture_env("HOME");
    auto xdg_restore = capture_env("XDG_CACHE_HOME");
    REQUIRE(Env::set("HOME", home.string()).is_ok());
    REQUIRE(Env::unset("XDG_CACHE_HOME").is_ok());

    auto unset_result = Paths::cache_dir("app");
    REQUIRE(unset_result.is_ok());
    CHECK_EQ(unset_result.unwrap(), home / ".cache" / "app");

    REQUIRE(Env::set("XDG_CACHE_HOME", "").is_ok());
    auto empty_result = Paths::cache_dir("app");
    REQUIRE(empty_result.is_ok());
    CHECK_EQ(empty_result.unwrap(), home / ".cache" / "app");
}

TEST_CASE("Paths user directories return NotFound when HOME and the XDG variables are unset")
{
    auto home_restore = capture_env("HOME");
    auto config_restore = capture_env("XDG_CONFIG_HOME");
    auto data_restore = capture_env("XDG_DATA_HOME");
    auto cache_restore = capture_env("XDG_CACHE_HOME");
    REQUIRE(Env::unset("HOME").is_ok());
    REQUIRE(Env::unset("XDG_CONFIG_HOME").is_ok());
    REQUIRE(Env::unset("XDG_DATA_HOME").is_ok());
    REQUIRE(Env::unset("XDG_CACHE_HOME").is_ok());

    auto config = Paths::config_dir("app");
    auto data = Paths::data_dir("app");
    auto cache = Paths::cache_dir("app");
    REQUIRE(config.is_err());
    REQUIRE(data.is_err());
    REQUIRE(cache.is_err());
    CHECK_EQ(config.unwrap_err(), ErrorCode::NotFound);
    CHECK_EQ(data.unwrap_err(), ErrorCode::NotFound);
    CHECK_EQ(cache.unwrap_err(), ErrorCode::NotFound);
}

TEST_CASE("Paths user directory queries do not create directories")
{
    auto base = Fs::temp_directory() / "pjh_platform_paths_pure";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    REQUIRE(!std::filesystem::exists(base, ec));

    auto home_restore = capture_env("HOME");
    auto config_restore = capture_env("XDG_CONFIG_HOME");
    auto data_restore = capture_env("XDG_DATA_HOME");
    auto cache_restore = capture_env("XDG_CACHE_HOME");
    REQUIRE(Env::set("HOME", path_to_utf8(base / "home")).is_ok());
    REQUIRE(Env::set("XDG_CONFIG_HOME", path_to_utf8(base / "cfg")).is_ok());
    REQUIRE(Env::set("XDG_DATA_HOME", path_to_utf8(base / "data")).is_ok());
    REQUIRE(Env::set("XDG_CACHE_HOME", path_to_utf8(base / "cache")).is_ok());

    (void)Paths::config_dir("app");
    (void)Paths::data_dir("app");
    (void)Paths::cache_dir("app");

    CHECK(!std::filesystem::exists(base, ec));
    CHECK(!std::filesystem::exists(base / "cfg" / "app", ec));
    CHECK(!std::filesystem::exists(base / "data" / "app", ec));
    CHECK(!std::filesystem::exists(base / "cache" / "app", ec));
}

TEST_CASE("Paths user directory accessors omit the app component for an empty app name")
{
    auto base = Fs::temp_directory() / "pjh_platform_paths_empty_app";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    auto restore = capture_env("XDG_DATA_HOME");
    REQUIRE(Env::set("XDG_DATA_HOME", base.string()).is_ok());

    auto r = Paths::data_dir("");
    REQUIRE(r.is_ok());
    CHECK_EQ(r.unwrap(), base);
}

#endif  // PJH_PLATFORM_LINUX

#if PJH_PLATFORM_MACOS

TEST_CASE("Paths macOS directories resolve under Library and include the app name")
{
    auto home = Fs::temp_directory() / "pjh_platform_paths_macos_home";
    std::error_code ec;
    std::filesystem::remove_all(home, ec);
    auto restore = capture_env("HOME");
    REQUIRE(Env::set("HOME", path_to_utf8(home)).is_ok());

    auto config = Paths::config_dir("app");
    auto data = Paths::data_dir("app");
    auto cache = Paths::cache_dir("app");
    REQUIRE(config.is_ok());
    REQUIRE(data.is_ok());
    REQUIRE(cache.is_ok());
    CHECK_EQ(config.unwrap(), home / "Library" / "Application Support" / "app");
    CHECK_EQ(data.unwrap(), home / "Library" / "Application Support" / "app");
    CHECK_EQ(cache.unwrap(), home / "Library" / "Caches" / "app");
}

TEST_CASE("Paths macOS directories return NotFound when HOME is unset")
{
    auto restore = capture_env("HOME");
    REQUIRE(Env::unset("HOME").is_ok());

    auto config = Paths::config_dir("app");
    auto data = Paths::data_dir("app");
    auto cache = Paths::cache_dir("app");
    REQUIRE(config.is_err());
    REQUIRE(data.is_err());
    REQUIRE(cache.is_err());
    CHECK_EQ(config.unwrap_err(), ErrorCode::NotFound);
    CHECK_EQ(data.unwrap_err(), ErrorCode::NotFound);
    CHECK_EQ(cache.unwrap_err(), ErrorCode::NotFound);
}

#endif  // PJH_PLATFORM_MACOS

#if PJH_PLATFORM_WINDOWS

TEST_CASE("Paths Windows config/data use APPDATA and cache uses LOCALAPPDATA")
{
    auto base = Fs::temp_directory() / "pjh_platform_paths_windows";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    auto roaming = base / "Roaming";
    auto local = base / "Local";
    auto appdata_restore = capture_env("APPDATA");
    auto localappdata_restore = capture_env("LOCALAPPDATA");
    REQUIRE(Env::set("APPDATA", path_to_utf8(roaming)).is_ok());
    REQUIRE(Env::set("LOCALAPPDATA", path_to_utf8(local)).is_ok());

    auto config = Paths::config_dir("app");
    auto data = Paths::data_dir("app");
    auto cache = Paths::cache_dir("app");
    REQUIRE(config.is_ok());
    REQUIRE(data.is_ok());
    REQUIRE(cache.is_ok());
    CHECK_EQ(config.unwrap(), roaming / "app");
    CHECK_EQ(data.unwrap(), roaming / "app");
    CHECK_EQ(cache.unwrap(), local / "app" / "Cache");
}

TEST_CASE("Paths Windows cache falls back to APPDATA when LOCALAPPDATA is unset")
{
    auto base = Fs::temp_directory() / "pjh_platform_paths_windows_fallback";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    auto roaming = base / "Roaming";
    auto appdata_restore = capture_env("APPDATA");
    auto localappdata_restore = capture_env("LOCALAPPDATA");
    REQUIRE(Env::set("APPDATA", path_to_utf8(roaming)).is_ok());
    REQUIRE(Env::unset("LOCALAPPDATA").is_ok());

    auto cache = Paths::cache_dir("app");
    REQUIRE(cache.is_ok());
    CHECK_EQ(cache.unwrap(), roaming / "app" / "Cache");
}

TEST_CASE("Paths Windows user directories return NotFound when APPDATA and LOCALAPPDATA are unset")
{
    auto appdata_restore = capture_env("APPDATA");
    auto localappdata_restore = capture_env("LOCALAPPDATA");
    REQUIRE(Env::unset("APPDATA").is_ok());
    REQUIRE(Env::unset("LOCALAPPDATA").is_ok());

    auto config = Paths::config_dir("app");
    auto data = Paths::data_dir("app");
    auto cache = Paths::cache_dir("app");
    REQUIRE(config.is_err());
    REQUIRE(data.is_err());
    REQUIRE(cache.is_err());
    CHECK_EQ(config.unwrap_err(), ErrorCode::NotFound);
    CHECK_EQ(data.unwrap_err(), ErrorCode::NotFound);
    CHECK_EQ(cache.unwrap_err(), ErrorCode::NotFound);
}

#endif  // PJH_PLATFORM_WINDOWS
