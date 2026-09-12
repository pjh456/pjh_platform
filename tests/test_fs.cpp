#include <doctest/doctest.h>

#include <fstream>
#include <iostream>
#include <pjh_platform/env.hpp>
#include <pjh_platform/fs.hpp>
#include <pjh_platform/os.hpp>
#include <pjh_platform/platform.hpp>

#if PJH_PLATFORM_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

using pjh::platform::Env;
using pjh::platform::ErrorCode;
using pjh::platform::Fs;

namespace
{
    // Restores one environment variable to its pre-test state (its value if it
    // was set, absence if it was not) on scope exit, so a REQUIRE failure's
    // unwind cannot leak a mutated HOME/USERPROFILE into later cases (task 34;
    // RAII precedent RestorePermissions, test_fs.cpp:561-571).
    struct EnvRestore
    {
        std::string name;
        bool had;
        std::string value;

        ~EnvRestore()
        {
            if (had)
                (void)pjh::platform::Env::set(name, value);
            else
                (void)pjh::platform::Env::unset(name);
        }
    };

    auto capture_env(const char *name) -> EnvRestore
    {
        auto cur = pjh::platform::Env::get(name);
        if (cur.is_ok())
            return EnvRestore{name, true, cur.unwrap()};
        return EnvRestore{name, false, {}};
    }
}  // namespace

TEST_CASE("Fs::current_path returns non-empty path")
{
    auto cwd = Fs::current_path();
    CHECK(!cwd.empty());
}

TEST_CASE("Fs::temp_directory returns non-empty path")
{
    auto tmp = Fs::temp_directory();
    CHECK(!tmp.empty());
}

TEST_CASE("Fs::create_directories and Fs::exists")
{
    auto tmp = Fs::temp_directory() / "pjh_platform_test_dir";
    auto r = Fs::create_directories(tmp);
    CHECK(r.is_ok());
    CHECK(Fs::exists(tmp));
    CHECK(Fs::is_directory(tmp));
    std::filesystem::remove_all(tmp);
}

#if PJH_PLATFORM_UNIX
TEST_CASE("Fs::create_directories returns NotFound when a path component is a file")
{
    // Anchor for the shared errno table: a regular file cannot be a parent
    // directory, so POSIX mkdir reports ENOTDIR; the mapper treats that as an
    // unresolvable path (NotFound, same family as ENOENT/ELOOP and the Windows
    // ERROR_PATH_NOT_FOUND family). Windows has no matching generic errno path
    // here, so the case is POSIX-gated. (task 51.2 F4)
    auto root = Fs::temp_directory() / "pjh_platform_test_create_dirs_file_component";
    std::error_code sec;
    std::filesystem::remove_all(root, sec);  // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(root));
    REQUIRE(Fs::write_file(root / "afile", "not a dir").is_ok());

    auto r = Fs::create_directories(root / "afile" / "child");
    REQUIRE(r.is_err());
    CHECK_EQ(r.unwrap_err(), ErrorCode::NotFound);

    std::filesystem::remove_all(root, sec);
}
#endif

TEST_CASE("Fs::write_file and Fs::read_file round-trip")
{
    auto tmp = Fs::temp_directory() / "pjh_platform_test_file.txt";
    auto r = Fs::write_file(tmp, "hello pjh_platform");
    CHECK(r.is_ok());

    auto content = Fs::read_file(tmp);
    REQUIRE(content.is_ok());
    CHECK_EQ(content.unwrap(), "hello pjh_platform");

    std::filesystem::remove(tmp);
}

TEST_CASE("Fs::read_file returns not_found for non-existent file")
{
    auto content = Fs::read_file("/nonexistent/path/file.txt");
    CHECK(content.is_err());
    CHECK_EQ(content.unwrap_err(), ErrorCode::NotFound);  // task 34 pin: doc fs.hpp:183-185
}

TEST_CASE("Fs::read_file returns InvalidArgument for a directory")
{
    auto dir = Fs::temp_directory() / "pjh_platform_test_read_file_dir";
    std::filesystem::create_directories(dir);

    auto r = Fs::read_file(dir);
    REQUIRE(r.is_err());
    CHECK_EQ(r.unwrap_err(), ErrorCode::InvalidArgument);

    std::filesystem::remove_all(dir);
}

TEST_CASE("Fs::copy_file copies file contents")
{
    auto src = Fs::temp_directory() / "pjh_platform_test_copy_src.txt";
    auto dst = Fs::temp_directory() / "pjh_platform_test_copy_dst.txt";
    std::string_view content = "copy me please";
    REQUIRE(Fs::write_file(src, content).is_ok());

    auto r = Fs::copy_file(src, dst);
    REQUIRE(r.is_ok());

    auto read = Fs::read_file(dst);
    REQUIRE(read.is_ok());
    CHECK_EQ(read.unwrap(), content);

    std::filesystem::remove(src);
    std::filesystem::remove(dst);
}

TEST_CASE("Fs::copy_file fails when destination exists without overwrite")
{
    auto src = Fs::temp_directory() / "pjh_platform_test_copy_src2.txt";
    auto dst = Fs::temp_directory() / "pjh_platform_test_copy_dst2.txt";
    REQUIRE(Fs::write_file(src, "src").is_ok());
    REQUIRE(Fs::write_file(dst, "dst").is_ok());

    auto r = Fs::copy_file(src, dst);
    CHECK(r.is_err());
    CHECK_EQ(r.unwrap_err(), pjh::platform::ErrorCode::AlreadyExists);

    std::filesystem::remove(src);
    std::filesystem::remove(dst);
}

TEST_CASE("Fs::copy_file overwrites existing destination")
{
    auto src = Fs::temp_directory() / "pjh_platform_test_copy_src3.txt";
    auto dst = Fs::temp_directory() / "pjh_platform_test_copy_dst3.txt";
    REQUIRE(Fs::write_file(src, "new content").is_ok());
    REQUIRE(Fs::write_file(dst, "old content").is_ok());

    auto r = Fs::copy_file(src, dst, true);
    REQUIRE(r.is_ok());

    auto read = Fs::read_file(dst);
    REQUIRE(read.is_ok());
    CHECK_EQ(read.unwrap(), "new content");

    std::filesystem::remove(src);
    std::filesystem::remove(dst);
}

TEST_CASE("Fs::copy_file returns NotFound for non-existent source")
{
    auto dst = Fs::temp_directory() / "pjh_platform_test_copy_missing_dst.txt";
    auto r = Fs::copy_file("/nonexistent_path_12345", dst);
    CHECK(r.is_err());
    CHECK_EQ(r.unwrap_err(), pjh::platform::ErrorCode::NotFound);

    std::filesystem::remove(dst);
}

TEST_CASE("Fs::copy_directory copies directory tree recursively")
{
    auto src = Fs::temp_directory() / "pjh_platform_test_copy_dir_src";
    auto dst = Fs::temp_directory() / "pjh_platform_test_copy_dir_dst";
    std::filesystem::create_directories(src / "sub");
    REQUIRE(Fs::write_file(src / "a.txt", "aaa").is_ok());
    REQUIRE(Fs::write_file(src / "sub" / "b.txt", "bbb").is_ok());

    auto r = Fs::copy_directory(src, dst);
    REQUIRE(r.is_ok());

    CHECK(Fs::exists(dst / "a.txt"));
    CHECK(Fs::is_directory(dst / "sub"));
    auto read = Fs::read_file(dst / "sub" / "b.txt");
    REQUIRE(read.is_ok());
    CHECK_EQ(read.unwrap(), "bbb");

    std::filesystem::remove_all(src);
    std::filesystem::remove_all(dst);
}

TEST_CASE("Fs::copy_directory fails when destination file exists without overwrite")
{
    auto src = Fs::temp_directory() / "pjh_platform_test_copy_dir_src2";
    auto dst = Fs::temp_directory() / "pjh_platform_test_copy_dir_dst2";
    std::filesystem::create_directories(src);
    REQUIRE(Fs::write_file(src / "a.txt", "aaa").is_ok());
    std::filesystem::create_directories(dst);
    REQUIRE(Fs::write_file(dst / "a.txt", "existing").is_ok());

    auto r = Fs::copy_directory(src, dst);
    CHECK(r.is_err());
    CHECK_EQ(r.unwrap_err(), pjh::platform::ErrorCode::AlreadyExists);

    std::filesystem::remove_all(src);
    std::filesystem::remove_all(dst);
}

TEST_CASE("Fs::copy_directory overwrites existing files")
{
    auto src = Fs::temp_directory() / "pjh_platform_test_copy_dir_src3";
    auto dst = Fs::temp_directory() / "pjh_platform_test_copy_dir_dst3";
    std::filesystem::create_directories(src);
    REQUIRE(Fs::write_file(src / "a.txt", "new").is_ok());
    std::filesystem::create_directories(dst);
    REQUIRE(Fs::write_file(dst / "a.txt", "old").is_ok());

    auto r = Fs::copy_directory(src, dst, true);
    REQUIRE(r.is_ok());

    auto read = Fs::read_file(dst / "a.txt");
    REQUIRE(read.is_ok());
    CHECK_EQ(read.unwrap(), "new");

    std::filesystem::remove_all(src);
    std::filesystem::remove_all(dst);
}

TEST_CASE("Fs::copy_directory returns NotFound for non-existent source")
{
    auto dst = Fs::temp_directory() / "pjh_platform_test_copy_dir_missing_dst";
    auto r = Fs::copy_directory("/nonexistent_path_12345", dst);
    CHECK(r.is_err());
    CHECK_EQ(r.unwrap_err(), pjh::platform::ErrorCode::NotFound);

    std::filesystem::remove_all(dst);
}

TEST_CASE("Fs::copy_directory returns InvalidArgument when source is a file")
{
    auto src = Fs::temp_directory() / "pjh_platform_test_copy_dir_file_src";
    auto dst = Fs::temp_directory() / "pjh_platform_test_copy_dir_file_dst";
    REQUIRE(Fs::write_file(src, "not a dir").is_ok());

    auto r = Fs::copy_directory(src, dst);
    CHECK(r.is_err());
    CHECK_EQ(r.unwrap_err(), pjh::platform::ErrorCode::InvalidArgument);

    std::filesystem::remove(src);
    std::filesystem::remove_all(dst);
}

TEST_CASE("Fs::rename moves a file")
{
    auto src = Fs::temp_directory() / "pjh_platform_test_rename_src.txt";
    auto dst = Fs::temp_directory() / "pjh_platform_test_rename_dst.txt";
    REQUIRE(Fs::write_file(src, "rename me").is_ok());

    auto r = Fs::rename(src, dst);
    REQUIRE(r.is_ok());
    CHECK(!Fs::exists(src));
    CHECK(Fs::is_regular_file(dst));
    auto read = Fs::read_file(dst);
    REQUIRE(read.is_ok());
    CHECK_EQ(read.unwrap(), "rename me");

    std::filesystem::remove(dst);
}

TEST_CASE("Fs::rename moves a directory")
{
    auto src = Fs::temp_directory() / "pjh_platform_test_rename_dir_src";
    auto dst = Fs::temp_directory() / "pjh_platform_test_rename_dir_dst";
    std::filesystem::create_directories(src);
    REQUIRE(Fs::write_file(src / "a.txt", "aaa").is_ok());

    auto r = Fs::rename(src, dst);
    REQUIRE(r.is_ok());
    CHECK(!Fs::exists(src));
    CHECK(Fs::is_directory(dst));
    auto read = Fs::read_file(dst / "a.txt");
    REQUIRE(read.is_ok());
    CHECK_EQ(read.unwrap(), "aaa");

    std::filesystem::remove_all(dst);
}

TEST_CASE("Fs::rename fails when destination exists without overwrite")
{
    auto src = Fs::temp_directory() / "pjh_platform_test_rename_src2.txt";
    auto dst = Fs::temp_directory() / "pjh_platform_test_rename_dst2.txt";
    REQUIRE(Fs::write_file(src, "src").is_ok());
    REQUIRE(Fs::write_file(dst, "dst").is_ok());

    auto r = Fs::rename(src, dst);
    CHECK(r.is_err());
    CHECK_EQ(r.unwrap_err(), pjh::platform::ErrorCode::AlreadyExists);
    CHECK(Fs::exists(src));

    std::filesystem::remove(src);
    std::filesystem::remove(dst);
}

TEST_CASE("Fs::rename overwrites existing destination")
{
    auto src = Fs::temp_directory() / "pjh_platform_test_rename_src3.txt";
    auto dst = Fs::temp_directory() / "pjh_platform_test_rename_dst3.txt";
    REQUIRE(Fs::write_file(src, "new content").is_ok());
    REQUIRE(Fs::write_file(dst, "old content").is_ok());

    auto r = Fs::rename(src, dst, true);
    REQUIRE(r.is_ok());
    CHECK(!Fs::exists(src));

    auto read = Fs::read_file(dst);
    REQUIRE(read.is_ok());
    CHECK_EQ(read.unwrap(), "new content");

    std::filesystem::remove(dst);
}

TEST_CASE("Fs::rename replaces an empty destination directory")
{
    auto src = Fs::temp_directory() / "pjh_platform_test_rename_src4.txt";
    auto dst = Fs::temp_directory() / "pjh_platform_test_rename_dst4_dir";
    REQUIRE(Fs::write_file(src, "over dir").is_ok());
    std::filesystem::create_directories(dst);

    auto r = Fs::rename(src, dst, true);
    REQUIRE(r.is_ok());
    CHECK(Fs::is_regular_file(dst));

    std::filesystem::remove(dst);
}

TEST_CASE("Fs::rename with overwrite succeeds when the destination does not exist")
{
    auto src = Fs::temp_directory() / "pjh_platform_test_rename_ow_src.txt";
    auto dst = Fs::temp_directory() / "pjh_platform_test_rename_ow_dst.txt";
    std::filesystem::remove(src);
    std::filesystem::remove(dst);
    REQUIRE(Fs::write_file(src, "abc").is_ok());
    REQUIRE(!Fs::exists(dst));

    auto r = Fs::rename(src, dst, true);
    REQUIRE(r.is_ok());
    CHECK(!Fs::exists(src));
    CHECK(Fs::is_regular_file(dst));
    auto read = Fs::read_file(dst);
    REQUIRE(read.is_ok());
    CHECK_EQ(read.unwrap(), "abc");

    std::filesystem::remove(dst);
}

TEST_CASE("Fs::rename a directory with overwrite onto a missing destination")
{
    auto src = Fs::temp_directory() / "pjh_platform_test_rename_ow_dir_src";
    auto dst = Fs::temp_directory() / "pjh_platform_test_rename_ow_dir_dst";
    std::error_code sec;
    std::filesystem::remove_all(src, sec);
    std::filesystem::remove_all(dst, sec);
    REQUIRE(std::filesystem::create_directories(src));
    REQUIRE(Fs::write_file(src / "a.txt", "aaa").is_ok());
    REQUIRE(!Fs::exists(dst));

    auto r = Fs::rename(src, dst, true);
    REQUIRE(r.is_ok());
    CHECK(Fs::is_directory(dst));
    CHECK(!Fs::exists(src));

    std::filesystem::remove_all(dst, sec);
}

TEST_CASE("Fs::rename rejects a directory source onto an existing file")
{
    auto dir = Fs::temp_directory() / "pjh_platform_test_rename_dir_over_file";
    auto file = Fs::temp_directory() / "pjh_platform_test_rename_file_target.txt";
    std::filesystem::create_directories(dir);
    REQUIRE(Fs::write_file(file, "target").is_ok());

    auto r = Fs::rename(dir, file, true);
    REQUIRE(r.is_err());
    CHECK_EQ(r.unwrap_err(), ErrorCode::InvalidArgument);
    CHECK(Fs::is_regular_file(file));
    CHECK(Fs::is_directory(dir));

    std::filesystem::remove_all(dir);
    std::filesystem::remove(file);
}

#if PJH_PLATFORM_UNIX
TEST_CASE("Fs::rename renames a directory symlink over an existing file")
{
    // POSIX rename(2) does not follow a trailing symlink on oldpath, so a
    // symlink to a directory is renamed itself and may replace an existing
    // non-directory target. A follow-semantics is_directory(from) check would
    // wrongly reject the pair with InvalidArgument (regression from the
    // dir->file guard); symlink_status keeps that guard non-following.
    auto root = Fs::temp_directory() / "pjh_platform_test_rename_dir_symlink";
    std::error_code sec;
    std::filesystem::remove_all(root, sec);              // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(root));  // S1
    auto target = root / "target_dir";
    REQUIRE(std::filesystem::create_directories(target));  // S2
    auto dest = root / "dest.txt";
    REQUIRE(Fs::write_file(dest, "target").is_ok());  // S3
    auto link = root / "link_to_dir";
    std::filesystem::create_directory_symlink(target, link, sec);  // S4
    if (sec)
    {
        // Environment cannot create directory symlinks (privilege/support):
        // silent skip per repo convention.
        std::filesystem::remove_all(root, sec);
        return;
    }
    REQUIRE(std::filesystem::is_symlink(link));  // S5

    auto r = Fs::rename(link, dest, true);
    REQUIRE(r.is_ok());                        // A1 = THE PIN
    CHECK(std::filesystem::is_symlink(dest));  // A2: the link itself moved
    CHECK(Fs::is_directory(dest));             // A3: follows the moved link
    CHECK(Fs::is_directory(target));           // A4: target survives
    CHECK(!Fs::exists(link));                  // A5: source link gone

    std::filesystem::remove_all(root, sec);
}
#endif

TEST_CASE("Fs::rename returns NotFound for non-existent source")
{
    auto dst = Fs::temp_directory() / "pjh_platform_test_rename_missing_dst.txt";
    auto r = Fs::rename("/nonexistent_path_12345", dst);
    CHECK(r.is_err());
    CHECK_EQ(r.unwrap_err(), pjh::platform::ErrorCode::NotFound);

    std::filesystem::remove(dst);
}

TEST_CASE("Fs::rename to same path is a no-op")
{
    auto file = Fs::temp_directory() / "pjh_platform_test_rename_same.txt";
    REQUIRE(Fs::write_file(file, "content").is_ok());

    auto r = Fs::rename(file, file);
    REQUIRE(r.is_ok());
    CHECK(Fs::exists(file));

    std::filesystem::remove(file);
}

TEST_CASE("Fs::home_directory returns something on typical systems")
{
    auto home = Fs::home_directory();
    CHECK(home.is_ok());
}

TEST_CASE("Fs::remove_all removes directory and returns count")
{
    auto tmp = Fs::temp_directory() / "pjh_platform_test_remove_all";
    std::filesystem::create_directories(tmp);
    auto r = Fs::remove_all(tmp);
    CHECK(r.is_ok());
    CHECK_GE(r.unwrap(), 1);
    CHECK(!Fs::exists(tmp));
}

TEST_CASE("Fs::remove_all handles read-only files")
{
    // This test is especially important on Windows where
    // std::filesystem::remove_all cannot delete read-only files
    // (e.g., inside .git directories).
    auto tmp = Fs::temp_directory() / "pjh_platform_test_remove_readonly";
    std::filesystem::create_directories(tmp);

    auto f1 = tmp / "readonly.txt";
    auto w1 = Fs::write_file(f1, "read only content");
    REQUIRE(w1.is_ok());
    std::filesystem::permissions(
        f1, std::filesystem::perms::owner_read, std::filesystem::perm_options::replace);

    auto sub = tmp / "subdir";
    std::filesystem::create_directories(sub);
    auto f2 = sub / "nested_readonly.txt";
    auto w2 = Fs::write_file(f2, "nested content");
    REQUIRE(w2.is_ok());
    std::filesystem::permissions(
        f2, std::filesystem::perms::owner_read, std::filesystem::perm_options::replace);

    auto r = Fs::remove_all(tmp);
    CHECK(r.is_ok());
    CHECK(!Fs::exists(tmp));
}

TEST_CASE("Fs::remove_all on non-existent path returns 0")
{
    auto r = Fs::remove_all("/nonexistent_path_12345");
    CHECK(r.is_ok());
    CHECK_EQ(r.unwrap(), 0);
}

TEST_CASE("Fs::is_regular_file distinguishes files from directories")
{
    auto tmp = Fs::temp_directory() / "pjh_platform_test_is_regular";
    std::filesystem::create_directories(tmp);
    CHECK(!Fs::is_regular_file(tmp));

    auto file = tmp / "test.txt";
    auto wr = Fs::write_file(file, "content");
    REQUIRE(wr.is_ok());
    CHECK(Fs::is_regular_file(file));

    std::filesystem::remove_all(tmp);
}

TEST_CASE("Fs::file_size returns correct size")
{
    auto file = Fs::temp_directory() / "pjh_platform_test_file_size.txt";
    std::string_view content = "hello pjh_platform";
    auto wr = Fs::write_file(file, content);
    REQUIRE(wr.is_ok());

    auto sz = Fs::file_size(file);
    REQUIRE(sz.is_ok());
    CHECK_EQ(sz.unwrap(), content.size());

    std::filesystem::remove(file);
}

TEST_CASE("Fs::file_size returns NotFound for non-existent file")
{
    auto sz = Fs::file_size("/nonexistent_path_12345");
    CHECK(sz.is_err());
    CHECK_EQ(sz.unwrap_err(), pjh::platform::ErrorCode::NotFound);
}

TEST_CASE("Fs::list_directory lists entries")
{
    auto tmp = Fs::temp_directory() / "pjh_platform_test_list_dir";
    std::filesystem::create_directories(tmp);
    auto wa = Fs::write_file(tmp / "a.txt", "aaa");
    auto wb = Fs::write_file(tmp / "b.txt", "bbb");
    REQUIRE(wa.is_ok());
    REQUIRE(wb.is_ok());
    std::filesystem::create_directory(tmp / "subdir");

    auto entries = Fs::list_directory(tmp);
    REQUIRE(entries.is_ok());
    CHECK_EQ(entries.unwrap().size(), 3);

    std::filesystem::remove_all(tmp);
}

TEST_CASE("Fs::list_directory returns NotFound for non-existent path")
{
    auto entries = Fs::list_directory("/nonexistent_path_12345");
    CHECK(entries.is_err());
    CHECK_EQ(entries.unwrap_err(), pjh::platform::ErrorCode::NotFound);
}

TEST_CASE("Fs::normalize collapses dot and dot-dot elements")
{
    CHECK_EQ(
        Fs::normalize(std::filesystem::path("a/./b/../c")).generic_string(),
        std::filesystem::path("a/c").generic_string());
    CHECK_EQ(
        Fs::normalize(std::filesystem::path("a//b///c")).generic_string(),
        std::filesystem::path("a/b/c").generic_string());
    CHECK_EQ(
        Fs::normalize(std::filesystem::path("../../a/./b")).generic_string(),
        std::filesystem::path("../../a/b").generic_string());
    CHECK_EQ(
        Fs::normalize(std::filesystem::path("./")).generic_string(),
        std::filesystem::path(".").generic_string());
    CHECK_EQ(
        Fs::normalize(std::filesystem::path("")).generic_string(),
        std::filesystem::path("").generic_string());
    if (pjh::platform::Os::is_windows)
        CHECK_EQ(
            Fs::normalize(std::filesystem::path("C:\\a\\..")).generic_string(),
            std::filesystem::path("C:\\").generic_string());
    else
        CHECK_EQ(
            Fs::normalize(std::filesystem::path("/a/..")).generic_string(),
            std::filesystem::path("/").generic_string());
}

TEST_CASE("Fs::join concatenates parts with platform separator")
{
    auto p = Fs::join(std::filesystem::path("a"), "b", "c.txt");
    CHECK_EQ(
        p.string(),
        std::filesystem::path("a") / std::filesystem::path("b") / std::filesystem::path("c.txt"));

    auto empty = Fs::join(std::filesystem::path("a"));
    CHECK_EQ(empty.string(), std::filesystem::path("a").string());
}

TEST_CASE("Fs::join with an absolute part replaces base")
{
    auto p = Fs::join(std::filesystem::path("a/b"), "/x", "y");
    CHECK_EQ(p.string(), std::filesystem::path("/x") / std::filesystem::path("y"));
}

TEST_CASE("Fs::extension returns extension with dot")
{
    CHECK_EQ(Fs::extension(std::filesystem::path("a/b/c.txt")), ".txt");
    CHECK_EQ(Fs::extension(std::filesystem::path("archive.tar.gz")), ".gz");
    CHECK_EQ(Fs::extension(std::filesystem::path("noext")), "");
    CHECK_EQ(Fs::extension(std::filesystem::path("dir/")), "");
    CHECK_EQ(Fs::extension(std::filesystem::path(".hidden")), "");
    CHECK_EQ(Fs::extension(std::filesystem::path("file.")), ".");  // task 34 pin: doc fs.hpp:443
    CHECK_EQ(
        Fs::extension(std::filesystem::path(".bashrc")), "");  // task 34 pin: doc fs.hpp:443-444
}

TEST_CASE("Fs::stem returns name without extension")
{
    CHECK_EQ(Fs::stem(std::filesystem::path("a/b/c.txt")), "c");
    CHECK_EQ(Fs::stem(std::filesystem::path("archive.tar.gz")), "archive.tar");
    CHECK_EQ(Fs::stem(std::filesystem::path("noext")), "noext");
    CHECK_EQ(Fs::stem(std::filesystem::path("dir")), "dir");
    CHECK_EQ(Fs::stem(std::filesystem::path(".hidden")), ".hidden");
}

TEST_CASE("Fs::relative computes relative path lexically")
{
    auto r = Fs::relative(std::filesystem::path("a/b"), std::filesystem::path("a/b/c/d"));
    REQUIRE(r.is_ok());
    CHECK_EQ(r.unwrap().generic_string(), std::filesystem::path("c/d").generic_string());

    auto up = Fs::relative(std::filesystem::path("a/b/c"), std::filesystem::path("a/b"));
    REQUIRE(up.is_ok());
    CHECK_EQ(up.unwrap().generic_string(), std::filesystem::path("..").generic_string());

    auto same = Fs::relative(std::filesystem::path("a/b"), std::filesystem::path("a/b"));
    REQUIRE(same.is_ok());
    CHECK_EQ(same.unwrap().generic_string(), std::filesystem::path(".").generic_string());
}

TEST_CASE("Fs::relative handles non-existent and unnormalized paths")
{
    auto r = Fs::relative(std::filesystem::path("a/./b/../b"), std::filesystem::path("a/b/c"));
    REQUIRE(r.is_ok());
    CHECK_EQ(r.unwrap().generic_string(), std::filesystem::path("c").generic_string());
}

TEST_CASE("Fs::relative fails when paths share no common root")
{
    if (pjh::platform::Os::is_windows)
    {
        auto r =
            Fs::relative(std::filesystem::path("C:\\dir"), std::filesystem::path("D:\\dir\\file"));
        CHECK(r.is_err());
        CHECK_EQ(r.unwrap_err(), pjh::platform::ErrorCode::InvalidArgument);
    }
    else
    {
        auto r =
            Fs::relative(std::filesystem::path("relative"), std::filesystem::path("/absolute"));
        CHECK(r.is_err());
        CHECK_EQ(r.unwrap_err(), pjh::platform::ErrorCode::InvalidArgument);
    }
}

TEST_CASE("Fs::write_file returns NotFound when the parent directory is missing")
{
    auto parent = Fs::temp_directory() / "pjh_platform_test_write_missing_parent";
    std::error_code rec;
    std::filesystem::remove_all(parent, rec);  // defensive: stale scratch
    auto r = Fs::write_file(parent / "f.txt", "x");
    CHECK(r.is_err());                              // A1
    CHECK_EQ(r.unwrap_err(), ErrorCode::NotFound);  // A2 (pre-fix red: IoError)
    // Positive control: with the parent present, the write succeeds.
    REQUIRE(std::filesystem::create_directories(parent));  // A3
    CHECK(Fs::write_file(parent / "f.txt", "x").is_ok());  // A4
    std::filesystem::remove_all(parent, rec);
}

#if PJH_PLATFORM_UNIX
TEST_CASE("Fs::write_file returns PermissionDenied when the parent directory is unwritable")
{
    auto p = Fs::temp_directory() / "pjh_platform_test_write_perm_dir";
    std::error_code sec;
    std::filesystem::remove_all(p, sec);  // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(p));
    auto original = std::filesystem::status(p, sec).permissions();
    REQUIRE_FALSE(sec);
    std::filesystem::permissions(p, std::filesystem::perms::none, sec);
    REQUIRE_FALSE(sec);
    // Self-skip probe: if a file can still be created inside the 000
    // directory, EACCES cannot be manufactured here (privileged process,
    // e.g. root on CI): restore and skip (task 16 precedent, documented
    // silent skip).
    {
        int probe = ::open(((p / "probe").string()).c_str(), O_WRONLY | O_CREAT, 0644);
        if (probe != -1)
        {
            ::close(probe);
            std::filesystem::remove(p / "probe", sec);
            std::filesystem::permissions(p, original, sec);
            std::filesystem::remove_all(p, sec);
            return;
        }
    }
    {
        // Restores the mode on every exit path (including a REQUIRE
        // failure's unwind); a leaked 000 directory would break the next
        // case's remove_all.
        struct RestorePermissions
        {
            std::filesystem::path dir;
            std::filesystem::perms perms;

            ~RestorePermissions()
            {
                std::error_code ec;
                std::filesystem::permissions(dir, perms, ec);
            }
        } guard{p, original};
        // Positive control in a writable sibling (inside p is impossible
        // while p is 000).
        auto sib = p.parent_path() / "pjh_platform_test_write_perm_sib";
        std::filesystem::create_directories(sib);
        CHECK(Fs::write_file(sib / "s.txt", "x").is_ok());  // B1
        std::filesystem::remove_all(sib, sec);
        auto r = Fs::write_file(p / "f.txt", "x");
        CHECK(r.is_err());                                      // B2
        CHECK_EQ(r.unwrap_err(), ErrorCode::PermissionDenied);  // B3 (pre-fix red: IoError)
    }
    std::filesystem::remove_all(p, sec);
}
#endif

#if PJH_PLATFORM_WINDOWS
TEST_CASE("Fs::write_file returns PermissionDenied when the file is read-only")
{
    auto f = Fs::temp_directory() / "pjh_platform_test_write_readonly.txt";
    std::error_code sec;
    std::filesystem::remove(f, sec);           // defensive: stale scratch
    CHECK(Fs::write_file(f, "seed").is_ok());  // C1 (positive control)
    auto original = std::filesystem::status(f, sec).permissions();
    REQUIRE_FALSE(sec);
    std::filesystem::permissions(
        f, std::filesystem::perms::owner_read, std::filesystem::perm_options::replace, sec);
    REQUIRE_FALSE(sec);
    {
        struct RestorePermissions
        {
            std::filesystem::path file;
            std::filesystem::perms perms;

            ~RestorePermissions()
            {
                std::error_code ec;
                std::filesystem::permissions(file, perms, ec);
            }
        } guard{f, original};
        auto r = Fs::write_file(f, "x");
        CHECK(r.is_err());                                      // C2
        CHECK_EQ(r.unwrap_err(), ErrorCode::PermissionDenied);  // C3 (pre-fix red: IoError)
    }
    std::filesystem::remove(f, sec);
}
#endif

#if PJH_PLATFORM_UNIX
TEST_CASE("Fs::is_regular_file returns true for a symlink to a regular file")
{
    // Reality anchor (task 32; pin discipline per task 27/28): the header
    // documents follow semantics -- a symlink to a regular file reports
    // true, a broken symlink reports false without throwing. If a future
    // change switches to non-follow (symlink_status) semantics, the A3 pin
    // flips red; that alternative is rejected on record (ROADMAP 32).
    auto p = Fs::temp_directory() / "pjh_platform_test_symlink_follow";
    std::error_code sec;
    std::filesystem::remove_all(p, sec);              // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(p));  // S1
    auto file = p / "target.txt";
    REQUIRE(Fs::write_file(file, "x").is_ok());  // S2
    auto link = p / "link_to_file";
    std::filesystem::create_symlink(file, link, sec);
    REQUIRE_FALSE(sec);  // S3
    auto broken = p / "broken_link";
    std::filesystem::create_symlink(p / "missing.txt", broken, sec);
    REQUIRE_FALSE(sec);  // S4

    CHECK(Fs::is_regular_file(file));     // A1 positive control
    CHECK(!Fs::is_regular_file(p));       // A2 negative control
    CHECK(Fs::is_regular_file(link));     // A3 = THE PIN (follow)
    CHECK(!Fs::is_regular_file(broken));  // A4 broken link: false, no throw

    std::filesystem::remove_all(p, sec);
}
#endif

// ── Task 34 pins ─────────────────────────────────────────────────────────
// Pin the documented Fs long-tail behaviors named by ROADMAP 34 (empty
// read, copy_directory symlink resolution, remove_all file/link, extension
// edge names, HOME-unset). Clauses cited per case; symlinks/permission are
// UNIX-gated (create_symlink / chmod-000 / no-HOME-fallback are POSIX
// capabilities), each with a task-30-style self-skip or RAII guard.

TEST_CASE("Fs::read_file returns empty string for a zero-byte file")
{
    // Contract pin (task 34): fs.hpp:176 -- "Empty files yield Ok("")".
    // Lane-invariant: POSIX st_size==0 (fs.cpp:210-214) and Windows
    // fileSize.QuadPart==0 (fs.cpp:163-167) both yield an empty string.
    auto f = Fs::temp_directory() / "pjh_platform_test_read_empty.txt";
    std::error_code sec;
    std::filesystem::remove(f, sec);         // defensive: stale scratch
    REQUIRE(Fs::write_file(f, "").is_ok());  // S1: zero-byte file
    auto r = Fs::read_file(f);
    REQUIRE(r.is_ok());        // A1
    CHECK_EQ(r.unwrap(), "");  // A2 = THE PIN (empty)
    std::filesystem::remove(f, sec);
}

#if PJH_PLATFORM_UNIX
TEST_CASE("Fs::read_file returns PermissionDenied when the file is unreadable")
{
    // Contract pin (task 34): fs.hpp:183-185 -- "Failure(PermissionDenied) on
    // access errors". POSIX: open(O_RDONLY) on a 000 file => EACCES
    // (fs.cpp:198-199). Self-skip + RAII guard per task 30 B.
    auto f = Fs::temp_directory() / "pjh_platform_test_read_perm.txt";
    std::error_code sec;
    std::filesystem::remove(f, sec);               // defensive: stale scratch
    REQUIRE(Fs::write_file(f, "secret").is_ok());  // S1
    auto pre = Fs::read_file(f);
    REQUIRE(pre.is_ok());              // P1 positive control
    CHECK_EQ(pre.unwrap(), "secret");  // P2
    auto original = std::filesystem::status(f, sec).permissions();
    REQUIRE_FALSE(sec);  // S2
    std::filesystem::permissions(f, std::filesystem::perms::none, sec);
    REQUIRE_FALSE(sec);  // S3
    // Self-skip probe: if the 000 file is still readable, EACCES cannot be
    // manufactured here (privileged process, e.g. root on CI): restore + skip
    // (task 16/30 precedent, documented silent skip).
    {
        int probe = ::open(f.string().c_str(), O_RDONLY);
        if (probe != -1)
        {
            ::close(probe);
            std::filesystem::permissions(f, original, sec);
            std::filesystem::remove(f, sec);
            return;
        }
    }
    {
        // Restores the mode on every exit path (including a REQUIRE
        // failure's unwind); a leaked 000 file would break the cleanup.
        struct RestorePermissions
        {
            std::filesystem::path file;
            std::filesystem::perms perms;

            ~RestorePermissions()
            {
                std::error_code ec;
                std::filesystem::permissions(file, perms, ec);
            }
        } guard{f, original};
        auto r = Fs::read_file(f);
        CHECK(r.is_err());                                      // A1
        CHECK_EQ(r.unwrap_err(), ErrorCode::PermissionDenied);  // A2 = THE PIN
    }
    std::filesystem::remove(f, sec);
}
#endif

#if PJH_PLATFORM_UNIX
TEST_CASE("Fs::copy_directory copies symlinks by dereferencing files and stubbing dirs")
{
    // Contract pin (task 34): fs.hpp:259-271 (the three-type clause written
    // by this task's single include/ hunk). A link to a regular file is
    // dereferenced (dest is a regular file holding the target's bytes); a
    // link to a directory is not followed (dest is an empty stub, the target
    // contents are NOT copied through the link); a broken link fails the copy
    // with the mapped error of resolving the link. Probe-confirmed locally
    // (task 34 probe, all four symlink cases; plan 4-R2 for the broken code);
    // the second UNIX lane is verified via CI.
    auto root = Fs::temp_directory() / "pjh_platform_test_copydir_sym";
    std::error_code sec;
    std::filesystem::remove_all(root, sec);              // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(root));  // S0
    // Scenario A: a file symlink and a directory symlink (no broken link).
    auto srcA = root / "srcA";
    REQUIRE(std::filesystem::create_directories(srcA / "realdir"));                    // S1
    REQUIRE(Fs::write_file(srcA / "realdir" / "inner.txt", "inner").is_ok());          // S2
    REQUIRE(Fs::write_file(srcA / "targetfile.txt", "filebytes").is_ok());             // S3
    std::filesystem::create_symlink(srcA / "targetfile.txt", srcA / "linkfile", sec);  // S4
    REQUIRE_FALSE(sec);
    std::filesystem::create_symlink(srcA / "realdir", srcA / "linkdir", sec);  // S5
    REQUIRE_FALSE(sec);
    auto dstA = root / "dstA";
    auto rA = Fs::copy_directory(srcA, dstA);
    REQUIRE(rA.is_ok());                            // A1 (copy succeeds)
    CHECK(Fs::is_regular_file(dstA / "linkfile"));  // A2 (file link deref'd)
    auto rf = Fs::read_file(dstA / "linkfile");
    REQUIRE(rf.is_ok());                                 // A3
    CHECK_EQ(rf.unwrap(), "filebytes");                  // A4 = PIN (file link)
    CHECK(Fs::is_directory(dstA / "linkdir"));           // A5 (stub is a dir)
    CHECK(!Fs::exists(dstA / "linkdir" / "inner.txt"));  // A6 = PIN (not followed)
    auto lst = Fs::list_directory(dstA / "linkdir");
    REQUIRE(lst.is_ok());               // A7a (empty stub)
    CHECK_EQ(lst.unwrap().size(), 0u);  // A7b
    // Scenario B: a broken link makes the copy fail.
    auto srcB = root / "srcB";
    REQUIRE(std::filesystem::create_directories(srcB));                           // S6
    std::filesystem::create_symlink(srcB / "missing.txt", srcB / "broken", sec);  // S7
    REQUIRE_FALSE(sec);
    auto dstB = root / "dstB";
    auto rB = Fs::copy_directory(srcB, dstB);
    CHECK(rB.is_err());                              // B1
    CHECK_EQ(rB.unwrap_err(), ErrorCode::NotFound);  // B2 = PIN (broken link)
    std::filesystem::remove_all(root, sec);
}
#endif

#if PJH_PLATFORM_UNIX
TEST_CASE("Fs::home_directory returns NotFound when HOME is unset (POSIX)")
{
    // Contract pin (task 34): fs.hpp:372-373 -- "Failure(NotFound) when
    // neither variable is set". On POSIX there is no USERPROFILE fallback,
    // so unsetting HOME alone exercises the NotFound path (fs.cpp:451).
    auto guard = capture_env("HOME");                    // restore on exit
    REQUIRE(pjh::platform::Env::unset("HOME").is_ok());  // S1
    auto r = Fs::home_directory();
    CHECK(r.is_err());                              // A1
    CHECK_EQ(r.unwrap_err(), ErrorCode::NotFound);  // A2 = THE PIN
}
#endif

#if PJH_PLATFORM_WINDOWS
TEST_CASE("Fs::home_directory returns NotFound when neither HOME nor USERPROFILE is set (Windows)")
{
    // Contract pin (task 34): fs.hpp:372-373 / 379-380 -- "HOME then
    // USERPROFILE on Windows"; with both unset the fallback chain is
    // exhausted => Failure(NotFound) (fs.cpp:451). Symmetric pair with the
    // POSIX arm (task 30/33 gating precedent).
    auto gh = capture_env("HOME");  // restore on exit
    auto gu = capture_env("USERPROFILE");
    REQUIRE(pjh::platform::Env::unset("HOME").is_ok());         // S1
    REQUIRE(pjh::platform::Env::unset("USERPROFILE").is_ok());  // S2
    auto r = Fs::home_directory();
    CHECK(r.is_err());                              // A1
    CHECK_EQ(r.unwrap_err(), ErrorCode::NotFound);  // A2 = THE PIN
}
#endif

TEST_CASE("Fs::remove_all removes a single file and reports count 1")
{
    // Contract pin (task 34): fs.hpp:68 -- "Recursively removes the file or
    // directory at @p p" (file half, already documented). Lane-invariant:
    // std::filesystem::remove_all of a single file removes it and returns 1.
    auto f = Fs::temp_directory() / "pjh_platform_test_remove_file.txt";
    std::error_code sec;
    std::filesystem::remove(f, sec);          // defensive: stale scratch
    REQUIRE(Fs::write_file(f, "x").is_ok());  // S1
    auto r = Fs::remove_all(f);
    CHECK(r.is_ok());          // A1
    CHECK_EQ(r.unwrap(), 1u);  // A2 = PIN (count = 1 file)
    CHECK(!Fs::exists(f));     // A3 (gone)
}

#if PJH_PLATFORM_UNIX
TEST_CASE("Fs::remove_all on a symlink removes the link, not the target")
{
    // Reality anchor (task 34; pin discipline per task 27): the header is
    // silent on symlink targets (fs.hpp:68 covers only "file or directory").
    // The standard says remove_all does not follow symlinks (the symlink is
    // removed, not its target), and the local probe confirms it for both
    // link->file (count 1, target survives) and link->dir (count 1, target
    // survives). Per plan 4-R1 only the lane-invariant invariants are pinned
    // (is_ok + link gone); the target's fate is reported by the probe, not
    // pinned, since it is verifiable on the other lanes only via CI.
    auto root = Fs::temp_directory() / "pjh_platform_test_remove_sym";
    std::error_code sec;
    std::filesystem::remove_all(root, sec);              // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(root));  // S0
    // (link -> file): remove the link, target survives, count 1.
    auto target = root / "target.txt";
    REQUIRE(Fs::write_file(target, "keep").is_ok());  // S1
    auto lf = root / "link_to_file";
    std::filesystem::create_symlink(target, lf, sec);  // S2
    REQUIRE_FALSE(sec);
    auto r1 = Fs::remove_all(lf);
    CHECK(r1.is_ok());          // A1
    CHECK_EQ(r1.unwrap(), 1u);  // A2 = PIN (one link)
    CHECK(!Fs::exists(lf));     // A3 link gone
    CHECK(Fs::exists(target));  // A4 = PIN (target survives)
    // (link -> dir): removes the link (target fate lane-qualified, reported).
    auto tdir = root / "tdir";
    REQUIRE(std::filesystem::create_directories(tdir));    // S3
    REQUIRE(Fs::write_file(tdir / "x.txt", "y").is_ok());  // S4
    auto ld = root / "link_to_dir";
    std::filesystem::create_symlink(tdir, ld, sec);  // S5
    REQUIRE_FALSE(sec);
    auto r2 = Fs::remove_all(ld);
    CHECK(r2.is_ok());       // A5 (invariant)
    CHECK(!Fs::exists(ld));  // A6 (invariant: link gone)
    std::filesystem::remove_all(root, sec);
}
#endif

// ── Task 58 pins ─────────────────────────────────────────────────────────
// Pin the Fs::append contract (fs.hpp `append` Doxygen): create-if-missing,
// never truncate, parent-missing NotFound, raw-byte passthrough (no CRLF
// translation / encoding validation / BOM), empty-content semantics, and
// looped native append. T1-T9 are lane-invariant; T10 (POSIX 000 dir) and
// T11 (Windows read-only target) are platform-gated in-place with a
// self-skip probe / RAII restore, mirroring the write_file permission cases
// above. Partial-write branches are not manufactured here: on POSIX the loop
// is the same shape as the already-hardened write_file loop, and on Windows
// the 1 GiB chunk cap mirrors read_file.

TEST_CASE("Fs::append creates the file when it does not exist")
{
    // Contract pin (task 58): "Creates @p p if it does not exist".
    auto f = Fs::temp_directory() / "pjh_platform_test_append_create.txt";
    std::error_code sec;
    std::filesystem::remove(f, sec);            // defensive: stale scratch
    REQUIRE(Fs::append(f, "created").is_ok());  // A1
    CHECK(Fs::exists(f));                       // A2
    CHECK(Fs::is_regular_file(f));              // A3
    auto r = Fs::read_file(f);
    REQUIRE(r.is_ok());               // A4
    CHECK_EQ(r.unwrap(), "created");  // A5
    std::filesystem::remove(f, sec);
}

TEST_CASE("Fs::append preserves existing content and appends at the end")
{
    // Contract pin (task 58): "An existing file is never truncated" / "No
    // existing bytes are lost".
    auto f = Fs::temp_directory() / "pjh_platform_test_append_preserve.txt";
    std::error_code sec;
    std::filesystem::remove(f, sec);            // defensive: stale scratch
    REQUIRE(Fs::write_file(f, "abc").is_ok());  // S1 seed
    CHECK(Fs::append(f, "def").is_ok());        // A1
    auto r = Fs::read_file(f);
    REQUIRE(r.is_ok());              // A2
    CHECK_EQ(r.unwrap(), "abcdef");  // A3 = THE PIN (no truncation)
    std::filesystem::remove(f, sec);
}

TEST_CASE("Fs::append returns NotFound when the parent directory is missing")
{
    // Contract pin (task 58): "Failure(NotFound) if the parent directory of
    // @p p does not exist" (same as write_file). POSIX open(O_CREAT) => ENOENT
    // and Windows OPEN_ALWAYS => ERROR_PATH_NOT_FOUND both map to NotFound.
    auto parent = Fs::temp_directory() / "pjh_platform_test_append_missing_parent";
    std::error_code rec;
    std::filesystem::remove_all(parent, rec);  // defensive: stale scratch
    auto r = Fs::append(parent / "f.txt", "x");
    CHECK(r.is_err());                              // A1
    CHECK_EQ(r.unwrap_err(), ErrorCode::NotFound);  // A2 = THE PIN
    // Positive control: with the parent present, the append succeeds.
    REQUIRE(std::filesystem::create_directories(parent));  // A3
    CHECK(Fs::append(parent / "f.txt", "x").is_ok());      // A4
    std::filesystem::remove_all(parent, rec);
}

TEST_CASE("Fs::append preserves CRLF bytes without translation")
{
    // Contract pin (task 58): "no newline (LF/CRLF) translation". The bytes on
    // disk must equal the input exactly on every platform (native append is
    // not text mode).
    auto f = Fs::temp_directory() / "pjh_platform_test_append_crlf.txt";
    std::error_code sec;
    std::filesystem::remove(f, sec);  // defensive: stale scratch
    const std::string crlf = "a\r\nb\r\n";
    REQUIRE(Fs::append(f, crlf).is_ok());  // A1
    auto r = Fs::read_file(f);
    REQUIRE(r.is_ok());               // A2
    CHECK_EQ(r.unwrap(), crlf);       // A3 = THE PIN (CRLF literal)
    CHECK_EQ(r.unwrap().size(), 6u);  // A4 (both CR bytes survived)
    // Positive control: LF-only content is likewise byte-exact.
    auto g = Fs::temp_directory() / "pjh_platform_test_append_lf.txt";
    std::filesystem::remove(g, sec);
    const std::string lf = "a\nb\n";
    REQUIRE(Fs::append(g, lf).is_ok());  // B1
    auto r2 = Fs::read_file(g);
    REQUIRE(r2.is_ok());        // B2
    CHECK_EQ(r2.unwrap(), lf);  // B3 (LF-only unchanged)
    std::filesystem::remove(f, sec);
    std::filesystem::remove(g, sec);
}

TEST_CASE("Fs::append round-trips CJK bytes")
{
    // Contract pin (task 58): UTF-8 content passes through unvalidated and
    // unmodified. Bytes are written as explicit \x escapes so the case does
    // not depend on the host compiler's source charset (test_encoding.cpp
    // precedent); the literal decodes to U+4E2D U+6587 U+8FFD U+52A0.
    auto f = Fs::temp_directory() / "pjh_platform_test_append_cjk.txt";
    std::error_code sec;
    std::filesystem::remove(f, sec);  // defensive: stale scratch
    const std::string cjk = "\xE4\xB8\xAD\xE6\x96\x87\xE8\xBF\xBD\xE5\x8A\xA0";
    REQUIRE(Fs::append(f, cjk).is_ok());  // A1
    auto r = Fs::read_file(f);
    REQUIRE(r.is_ok());                // A2
    CHECK_EQ(r.unwrap(), cjk);         // A3 = THE PIN (byte-exact CJK)
    CHECK_EQ(r.unwrap().size(), 12u);  // A4 (4 code points, 12 bytes)
    std::filesystem::remove(f, sec);
}

TEST_CASE("Fs::append with empty content creates an empty file")
{
    // Contract pin (task 58): "An empty @p content still creates @p p when it
    // is absent (the file is opened/created)".
    auto f = Fs::temp_directory() / "pjh_platform_test_append_empty_create.txt";
    std::error_code sec;
    std::filesystem::remove(f, sec);     // defensive: stale scratch
    REQUIRE(Fs::append(f, "").is_ok());  // A1
    CHECK(Fs::exists(f));                // A2
    CHECK(Fs::is_regular_file(f));       // A3
    auto sz = Fs::file_size(f);
    REQUIRE(sz.is_ok());        // A4
    CHECK_EQ(sz.unwrap(), 0u);  // A5 = THE PIN (zero bytes)
    std::filesystem::remove(f, sec);
}

TEST_CASE("Fs::append with empty content leaves existing content unchanged")
{
    // Contract pin (task 58): "and appends zero bytes when it is present".
    auto f = Fs::temp_directory() / "pjh_platform_test_append_empty_noop.txt";
    std::error_code sec;
    std::filesystem::remove(f, sec);             // defensive: stale scratch
    REQUIRE(Fs::write_file(f, "keep").is_ok());  // S1 seed
    CHECK(Fs::append(f, "").is_ok());            // A1
    auto r = Fs::read_file(f);
    REQUIRE(r.is_ok());            // A2
    CHECK_EQ(r.unwrap(), "keep");  // A3 = THE PIN (unchanged)
    std::filesystem::remove(f, sec);
}

TEST_CASE("Fs::append accumulates repeated appends in order")
{
    // Contract pin (task 58): each call appends at the current end, so the
    // concatenation is exactly the call order.
    auto f = Fs::temp_directory() / "pjh_platform_test_append_repeated.txt";
    std::error_code sec;
    std::filesystem::remove(f, sec);      // defensive: stale scratch
    REQUIRE(Fs::append(f, "1").is_ok());  // S1
    REQUIRE(Fs::append(f, "2").is_ok());  // S2
    REQUIRE(Fs::append(f, "3").is_ok());  // S3
    auto r = Fs::read_file(f);
    REQUIRE(r.is_ok());           // A1
    CHECK_EQ(r.unwrap(), "123");  // A2 = THE PIN (order preserved)
    std::filesystem::remove(f, sec);
}

TEST_CASE("Fs::append round-trips embedded NUL bytes")
{
    // Contract pin (task 58): content is length-delimited raw bytes, not a
    // C string; an embedded NUL is written and read back verbatim.
    auto f = Fs::temp_directory() / "pjh_platform_test_append_nul.bin";
    std::error_code sec;
    std::filesystem::remove(f, sec);             // defensive: stale scratch
    const std::string_view with_nul("a\0b", 3);  // 3 bytes: 'a', NUL, 'b'
    REQUIRE(Fs::append(f, with_nul).is_ok());    // A1
    auto r = Fs::read_file(f);
    REQUIRE(r.is_ok());  // A2
    const std::string expected("a\0b", 3);
    CHECK_EQ(r.unwrap().size(), 3u);  // A3
    CHECK_EQ(r.unwrap(), expected);   // A4 = THE PIN (NUL kept)
    std::filesystem::remove(f, sec);
}

#if PJH_PLATFORM_UNIX
TEST_CASE("Fs::append returns PermissionDenied when the parent directory is unwritable")
{
    // Contract pin (task 58): "Failure(PermissionDenied) on access errors".
    // POSIX: open(O_WRONLY|O_CREAT|O_APPEND) inside a 000 directory => EACCES.
    // Self-skip + RAII restore per the write_file permission case above.
    auto p = Fs::temp_directory() / "pjh_platform_test_append_perm_dir";
    std::error_code sec;
    std::filesystem::remove_all(p, sec);  // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(p));
    auto original = std::filesystem::status(p, sec).permissions();
    REQUIRE_FALSE(sec);
    std::filesystem::permissions(p, std::filesystem::perms::none, sec);
    REQUIRE_FALSE(sec);
    // Self-skip probe: if a file can still be created inside the 000
    // directory, EACCES cannot be manufactured here (privileged process, e.g.
    // root on CI): restore and skip (task 16/30 precedent, documented silent
    // skip).
    {
        int probe = ::open(((p / "probe").string()).c_str(), O_WRONLY | O_CREAT, 0644);
        if (probe != -1)
        {
            ::close(probe);
            std::filesystem::remove(p / "probe", sec);
            std::filesystem::permissions(p, original, sec);
            std::filesystem::remove_all(p, sec);
            return;
        }
    }
    {
        // Restores the mode on every exit path (including a REQUIRE failure's
        // unwind); a leaked 000 directory would break the next case's cleanup.
        struct RestorePermissions
        {
            std::filesystem::path dir;
            std::filesystem::perms perms;

            ~RestorePermissions()
            {
                std::error_code ec;
                std::filesystem::permissions(dir, perms, ec);
            }
        } guard{p, original};
        // Positive control in a writable sibling (inside p is impossible while
        // p is 000).
        auto sib = p.parent_path() / "pjh_platform_test_append_perm_sib";
        std::filesystem::create_directories(sib);
        CHECK(Fs::append(sib / "s.txt", "x").is_ok());  // B1
        std::filesystem::remove_all(sib, sec);
        auto r = Fs::append(p / "f.txt", "x");
        CHECK(r.is_err());                                      // B2
        CHECK_EQ(r.unwrap_err(), ErrorCode::PermissionDenied);  // B3 = THE PIN
    }
    std::filesystem::remove_all(p, sec);
}
#endif

#if PJH_PLATFORM_WINDOWS
TEST_CASE("Fs::append returns PermissionDenied when the file is read-only")
{
    // Contract pin (task 58): "Failure(PermissionDenied) on access errors".
    // Windows: CreateFileW(FILE_APPEND_DATA) on a FILE_ATTRIBUTE_READONLY file
    // => ERROR_ACCESS_DENIED, mapped by detail::map_windows_error.
    auto f = Fs::temp_directory() / "pjh_platform_test_append_readonly.txt";
    std::error_code sec;
    std::filesystem::remove(f, sec);           // defensive: stale scratch
    CHECK(Fs::write_file(f, "seed").is_ok());  // C1 (positive control)
    auto original = std::filesystem::status(f, sec).permissions();
    REQUIRE_FALSE(sec);
    std::filesystem::permissions(
        f, std::filesystem::perms::owner_read, std::filesystem::perm_options::replace, sec);
    REQUIRE_FALSE(sec);
    {
        struct RestorePermissions
        {
            std::filesystem::path file;
            std::filesystem::perms perms;

            ~RestorePermissions()
            {
                std::error_code ec;
                std::filesystem::permissions(file, perms, ec);
            }
        } guard{f, original};
        auto r = Fs::append(f, "x");
        CHECK(r.is_err());                                      // C2
        CHECK_EQ(r.unwrap_err(), ErrorCode::PermissionDenied);  // C3 = THE PIN
    }
    std::filesystem::remove(f, sec);
}
#endif

// ── Task 59 pins: Fs::write_file_atomic ──────────────────────────────────
// Pin the Fs::write_file_atomic contract (fs.hpp Doxygen): same-directory
// unique temp plus exclusive create, atomic replace, failure cleanup that
// never masks the first error, target-is-directory InvalidArgument, parent
// missing NotFound, byte-exact round-trips, and permission replacement.
// T1-T4 and T6-T10 are lane-invariant; T5/T11 (POSIX) and T12 (Windows) are
// platform-gated in-place with a self-skip probe / RAII restore, mirroring
// the permission cases above. Concurrency uniqueness is covered
// deterministically by T10 (pid + atomic counter + residual assertions); a
// real cross-process race is not manufactured (the library owns no threads
// and would need a Threads::Threads link). Every case uses a per-case unique
// directory under temp, cleared before and after.

TEST_CASE("Fs::write_file_atomic creates the file when it does not exist")
{
    // Contract pin (task 59): "Atomically replaces @p p with @p content".
    auto root = Fs::temp_directory() / "pjh_platform_test_atomic_create";
    std::error_code sec;
    std::filesystem::remove_all(root, sec);              // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(root));  // S1
    auto target = root / "save.json";

    REQUIRE(Fs::write_file_atomic(target, "created").is_ok());  // A1
    CHECK(Fs::exists(target));                                  // A2
    CHECK(Fs::is_regular_file(target));                         // A3
    auto r = Fs::read_file(target);
    REQUIRE(r.is_ok());               // A4
    CHECK_EQ(r.unwrap(), "created");  // A5 = THE PIN
    auto lst = Fs::list_directory(root);
    REQUIRE(lst.is_ok());               // A6
    CHECK_EQ(lst.unwrap().size(), 1u);  // A7 = no temp residue

    std::filesystem::remove_all(root, sec);
}

TEST_CASE("Fs::write_file_atomic overwrites an existing file and leaves no temp residue")
{
    // Contract pin (task 59): "never a half-written file"; a successful call
    // leaves exactly the target and no temporary sibling.
    auto root = Fs::temp_directory() / "pjh_platform_test_atomic_overwrite";
    std::error_code sec;
    std::filesystem::remove_all(root, sec);              // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(root));  // S1
    auto target = root / "doc.txt";
    REQUIRE(Fs::write_file(target, "old").is_ok());  // S2 seed

    REQUIRE(Fs::write_file_atomic(target, "new").is_ok());  // A1
    auto r = Fs::read_file(target);
    REQUIRE(r.is_ok());           // A2
    CHECK_EQ(r.unwrap(), "new");  // A3 = THE PIN (replaced, not appended)
    auto lst = Fs::list_directory(root);
    REQUIRE(lst.is_ok());               // A4
    CHECK_EQ(lst.unwrap().size(), 1u);  // A5 = no temp residue
    for (const auto &entry : lst.unwrap())
        CHECK_EQ(entry.filename().string().find(".tmp"), std::string::npos);  // A6

    std::filesystem::remove_all(root, sec);
}

TEST_CASE("Fs::write_file_atomic returns NotFound when the parent directory is missing")
{
    // Contract pin (task 59): "Failure(NotFound) if the parent directory of
    // @p p does not exist". The exclusive create is the first filesystem touch,
    // so POSIX ENOENT / Windows ERROR_PATH_NOT_FOUND map to NotFound.
    auto parent = Fs::temp_directory() / "pjh_platform_test_atomic_missing_parent";
    std::error_code rec;
    std::filesystem::remove_all(parent, rec);  // defensive: stale scratch

    auto r = Fs::write_file_atomic(parent / "f.txt", "x");
    CHECK(r.is_err());                              // A1
    CHECK_EQ(r.unwrap_err(), ErrorCode::NotFound);  // A2 = THE PIN
    // Positive control: with the parent present, the atomic write succeeds.
    REQUIRE(std::filesystem::create_directories(parent));         // A3
    CHECK(Fs::write_file_atomic(parent / "f.txt", "x").is_ok());  // A4

    std::filesystem::remove_all(parent, rec);
}

TEST_CASE("Fs::write_file_atomic fails with InvalidArgument when the target is a directory")
{
    // Contract pin (task 59): "Failure(InvalidArgument) if @p p is an existing
    // directory" -- rejected before any temporary file is created, and the
    // directory survives.
    auto root = Fs::temp_directory() / "pjh_platform_test_atomic_dir_target";
    std::error_code sec;
    std::filesystem::remove_all(root, sec);              // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(root));  // S1
    auto dir = root / "adir";
    REQUIRE(std::filesystem::create_directories(dir));  // S2

    auto r = Fs::write_file_atomic(dir, "x");
    CHECK(r.is_err());                                     // A1
    CHECK_EQ(r.unwrap_err(), ErrorCode::InvalidArgument);  // A2 = THE PIN
    CHECK(Fs::is_directory(dir));                          // A3 (still a dir)
    auto lst = Fs::list_directory(root);
    REQUIRE(lst.is_ok());               // A4
    CHECK_EQ(lst.unwrap().size(), 1u);  // A5 = no temp created

    std::filesystem::remove_all(root, sec);
}

#if PJH_PLATFORM_UNIX
TEST_CASE(
    "Fs::write_file_atomic keeps the original content and no residue when temp creation fails")
{
    // Contract pin (task 59): "an existing @p p is left untouched" and the
    // first error (PermissionDenied) is not masked. POSIX: creating the temp
    // inside a read-only (0555) directory => EACCES. Self-skip + RAII restore
    // per the write_file/append permission cases above.
    auto dir = Fs::temp_directory() / "pjh_platform_test_atomic_perm_dir";
    std::error_code sec;
    std::filesystem::remove_all(dir, sec);  // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(dir));
    auto target = dir / "keep.txt";
    REQUIRE(Fs::write_file(target, "original").is_ok());
    auto original = std::filesystem::status(dir, sec).permissions();
    REQUIRE_FALSE(sec);
    std::filesystem::permissions(
        dir,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec |
            std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
            std::filesystem::perms::others_read | std::filesystem::perms::others_exec,
        sec);
    REQUIRE_FALSE(sec);
    // Self-skip probe: if a file can still be created inside the read-only
    // directory, EACCES cannot be manufactured here (privileged process, e.g.
    // root on CI): restore and skip (task 16/30 precedent).
    {
        int probe = ::open(((dir / "probe").string()).c_str(), O_WRONLY | O_CREAT, 0644);
        if (probe != -1)
        {
            ::close(probe);
            std::filesystem::remove(dir / "probe", sec);
            std::filesystem::permissions(dir, original, sec);
            std::filesystem::remove_all(dir, sec);
            return;
        }
    }
    {
        // Restores the mode on every exit path (including a REQUIRE failure's
        // unwind); a leaked 0555 directory would break the next case's cleanup.
        struct RestorePermissions
        {
            std::filesystem::path path;
            std::filesystem::perms perms;

            ~RestorePermissions()
            {
                std::error_code ec;
                std::filesystem::permissions(path, perms, ec);
            }
        } guard{dir, original};

        auto r = Fs::write_file_atomic(target, "new");
        CHECK(r.is_err());                                      // A1
        CHECK_EQ(r.unwrap_err(), ErrorCode::PermissionDenied);  // A2 = THE PIN
        auto rd = Fs::read_file(target);
        REQUIRE(rd.is_ok());                // A3
        CHECK_EQ(rd.unwrap(), "original");  // A4 = original preserved
        auto lst = Fs::list_directory(dir);
        REQUIRE(lst.is_ok());               // A5
        CHECK_EQ(lst.unwrap().size(), 1u);  // A6 = only the target, no temp
    }
    std::filesystem::remove_all(dir, sec);
}
#endif

TEST_CASE("Fs::write_file_atomic with empty content creates an empty file")
{
    // Contract pin (task 59): an empty @p content is a valid write and yields a
    // zero-byte regular file (byte-level API, no text heuristic).
    auto root = Fs::temp_directory() / "pjh_platform_test_atomic_empty_create";
    std::error_code sec;
    std::filesystem::remove_all(root, sec);              // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(root));  // S1
    auto target = root / "empty.txt";

    REQUIRE(Fs::write_file_atomic(target, "").is_ok());  // A1
    CHECK(Fs::exists(target));                           // A2
    CHECK(Fs::is_regular_file(target));                  // A3
    auto sz = Fs::file_size(target);
    REQUIRE(sz.is_ok());        // A4
    CHECK_EQ(sz.unwrap(), 0u);  // A5 = THE PIN (zero bytes)

    std::filesystem::remove_all(root, sec);
}

TEST_CASE("Fs::write_file_atomic with empty content replaces an existing file")
{
    // Contract pin (task 59): replace semantics apply to an empty payload too
    // (the existing content is discarded, not appended to).
    auto root = Fs::temp_directory() / "pjh_platform_test_atomic_empty_replace";
    std::error_code sec;
    std::filesystem::remove_all(root, sec);              // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(root));  // S1
    auto target = root / "shrunk.txt";
    REQUIRE(Fs::write_file(target, "keep").is_ok());  // S2 seed

    REQUIRE(Fs::write_file_atomic(target, "").is_ok());  // A1
    auto r = Fs::read_file(target);
    REQUIRE(r.is_ok());        // A2
    CHECK_EQ(r.unwrap(), "");  // A3 = THE PIN (emptied, not kept)

    std::filesystem::remove_all(root, sec);
}

TEST_CASE("Fs::write_file_atomic round-trips CJK bytes")
{
    // Contract pin (task 59): UTF-8 content passes through unvalidated and
    // unmodified. Bytes are written as explicit \x escapes so the case does
    // not depend on the host compiler's source charset; the literal decodes to
    // U+4E2D U+6587 U+539F U+5B50 U+5199 (15 bytes).
    auto root = Fs::temp_directory() / "pjh_platform_test_atomic_cjk";
    std::error_code sec;
    std::filesystem::remove_all(root, sec);              // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(root));  // S1
    auto target = root / "cjk.txt";
    const std::string cjk = "\xE4\xB8\xAD\xE6\x96\x87\xE5\x8E\x9F\xE5\xAD\x90\xE5\x86\x99";

    REQUIRE(Fs::write_file_atomic(target, cjk).is_ok());  // A1
    auto r = Fs::read_file(target);
    REQUIRE(r.is_ok());                // A2
    CHECK_EQ(r.unwrap(), cjk);         // A3 = THE PIN (byte-exact CJK)
    CHECK_EQ(r.unwrap().size(), 15u);  // A4 (5 code points, 15 bytes)

    std::filesystem::remove_all(root, sec);
}

TEST_CASE("Fs::write_file_atomic round-trips embedded NUL bytes")
{
    // Contract pin (task 59): content is length-delimited raw bytes, not a C
    // string; an embedded NUL is written and read back verbatim.
    auto root = Fs::temp_directory() / "pjh_platform_test_atomic_nul";
    std::error_code sec;
    std::filesystem::remove_all(root, sec);              // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(root));  // S1
    auto target = root / "nul.bin";
    const std::string_view with_nul("a\0b", 3);  // 3 bytes: 'a', NUL, 'b'

    REQUIRE(Fs::write_file_atomic(target, with_nul).is_ok());  // A1
    auto r = Fs::read_file(target);
    REQUIRE(r.is_ok());  // A2
    const std::string expected("a\0b", 3);
    CHECK_EQ(r.unwrap().size(), 3u);  // A3
    CHECK_EQ(r.unwrap(), expected);   // A4 = THE PIN (NUL kept)

    std::filesystem::remove_all(root, sec);
}

TEST_CASE("Fs::write_file_atomic accumulates rapid calls without colliding or leaving residue")
{
    // Contract pin (task 59): the pid + process-wide atomic counter makes every
    // temporary name unique, so back-to-back calls on one target neither
    // collide nor leave a temporary sibling behind. A true cross-process race
    // is intentionally not manufactured (see the section comment).
    auto root = Fs::temp_directory() / "pjh_platform_test_atomic_rapid";
    std::error_code sec;
    std::filesystem::remove_all(root, sec);              // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(root));  // S1
    auto target = root / "rapid.txt";
    constexpr int kCalls = 32;
    for (int i = 0; i < kCalls; ++i)
    {
        const std::string content = "value-" + std::to_string(i);
        REQUIRE(Fs::write_file_atomic(target, content).is_ok());  // A1
        auto lst = Fs::list_directory(root);
        REQUIRE(lst.is_ok());               // A2
        CHECK_EQ(lst.unwrap().size(), 1u);  // A3 = no residue on any iteration
    }
    auto r = Fs::read_file(target);
    REQUIRE(r.is_ok());                                           // A4
    CHECK_EQ(r.unwrap(), "value-" + std::to_string(kCalls - 1));  // A5 = last wins

    std::filesystem::remove_all(root, sec);
}

#if PJH_PLATFORM_UNIX
TEST_CASE("Fs::write_file_atomic replaces the target permissions with the temp file's")
{
    // Contract pin (task 59): "The new @p p inherits the temporary file's
    // permissions". The control file is created by Fs::write_file with the same
    // umask, so its mode equals the temp's; the pre-existing 0400 target mode
    // must NOT survive the replace.
    auto root = Fs::temp_directory() / "pjh_platform_test_atomic_perms";
    std::error_code sec;
    std::filesystem::remove_all(root, sec);              // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(root));  // S1
    auto target = root / "perm.txt";
    auto control = root / "control.txt";
    REQUIRE(Fs::write_file(target, "old").is_ok());  // S2
    REQUIRE(Fs::write_file(control, "x").is_ok());   // S3 (same umask as the temp)
    std::filesystem::permissions(
        target, std::filesystem::perms::owner_read, std::filesystem::perm_options::replace, sec);
    REQUIRE_FALSE(sec);  // S4

    REQUIRE(Fs::write_file_atomic(target, "new").is_ok());  // A1
    auto st_target = std::filesystem::status(target, sec);
    REQUIRE_FALSE(sec);  // A2
    auto st_control = std::filesystem::status(control, sec);
    REQUIRE_FALSE(sec);                                           // A3
    CHECK_EQ(st_target.permissions(), st_control.permissions());  // A4 = THE PIN
    CHECK(
        (st_target.permissions() & std::filesystem::perms::owner_write) !=
        std::filesystem::perms::none);  // A5 (0400 not preserved)
    auto r = Fs::read_file(target);
    REQUIRE(r.is_ok());           // A6
    CHECK_EQ(r.unwrap(), "new");  // A7

    std::filesystem::remove_all(root, sec);
}
#endif

#if PJH_PLATFORM_WINDOWS
TEST_CASE(
    "Fs::write_file_atomic fails and preserves the read-only target when rename cannot replace it")
{
    // Contract pin (task 59): on Windows MoveFileExW(MOVEFILE_REPLACE_EXISTING)
    // refuses a FILE_ATTRIBUTE_READONLY target with ERROR_ACCESS_DENIED, so the
    // atomic replace fails, the temp is cleaned up, and the original content is
    // preserved (the rename-failure cleanup arm).
    auto root = Fs::temp_directory() / "pjh_platform_test_atomic_readonly";
    std::error_code sec;
    std::filesystem::remove_all(root, sec);              // defensive: stale scratch
    REQUIRE(std::filesystem::create_directories(root));  // S1
    auto target = root / "ro.txt";
    REQUIRE(Fs::write_file(target, "keep").is_ok());  // S2
    auto original = std::filesystem::status(target, sec).permissions();
    REQUIRE_FALSE(sec);
    std::filesystem::permissions(
        target, std::filesystem::perms::owner_read, std::filesystem::perm_options::replace, sec);
    REQUIRE_FALSE(sec);  // S3
    {
        struct RestorePermissions
        {
            std::filesystem::path file;
            std::filesystem::perms perms;

            ~RestorePermissions()
            {
                std::error_code ec;
                std::filesystem::permissions(file, perms, ec);
            }
        } guard{target, original};

        auto r = Fs::write_file_atomic(target, "new");
        CHECK(r.is_err());                                      // A1
        CHECK_EQ(r.unwrap_err(), ErrorCode::PermissionDenied);  // A2 = THE PIN
        auto rd = Fs::read_file(target);
        REQUIRE(rd.is_ok());            // A3
        CHECK_EQ(rd.unwrap(), "keep");  // A4 = original preserved
        auto lst = Fs::list_directory(root);
        REQUIRE(lst.is_ok());               // A5
        CHECK_EQ(lst.unwrap().size(), 1u);  // A6 = no temp residue
    }
    std::filesystem::remove_all(root, sec);
}
#endif

// ── Task 34.1 pins ───────────────────────────────────────────────────────
// Fs::home_directory treats a set-but-empty HOME/USERPROFILE as unset
// (detail/61 §4.5, detail/49 §2.7) and builds the path from the UTF-8 env
// value without the Windows active-code-page narrow constructor.

#if PJH_PLATFORM_UNIX
TEST_CASE("Fs::home_directory returns NotFound when HOME is empty (POSIX)")
{
    // Task 34.1 pin: empty HOME == unset; POSIX has no USERPROFILE fallback.
    auto guard = capture_env("HOME");
    REQUIRE(Env::set("HOME", "").is_ok());
    auto r = Fs::home_directory();
    CHECK(r.is_err());
    CHECK_EQ(r.unwrap_err(), ErrorCode::NotFound);
}
#endif

#if PJH_PLATFORM_WINDOWS
TEST_CASE("Fs::home_directory falls back to USERPROFILE when HOME is empty (Windows)")
{
    // Task 34.1 pin: an empty HOME must not short-circuit the USERPROFILE
    // fallback (Env::get returns Ok("") for a set-but-empty variable).
    auto gh = capture_env("HOME");
    auto gu = capture_env("USERPROFILE");
    auto expected = Fs::temp_directory() / "pjh_home_empty_home";
    auto u8 = expected.u8string();
    REQUIRE(Env::set("USERPROFILE", std::string(u8.begin(), u8.end())).is_ok());
    REQUIRE(Env::set("HOME", "").is_ok());
    auto r = Fs::home_directory();
    REQUIRE(r.is_ok());
    CHECK_EQ(r.unwrap(), expected);
}

TEST_CASE("Fs::home_directory returns NotFound when USERPROFILE is empty (Windows)")
{
    // Task 34.1 pin: an empty USERPROFILE is unset, so the chain is exhausted.
    auto gh = capture_env("HOME");
    auto gu = capture_env("USERPROFILE");
    REQUIRE(Env::set("HOME", "").is_ok());
    REQUIRE(Env::set("USERPROFILE", "").is_ok());
    auto r = Fs::home_directory();
    CHECK(r.is_err());
    CHECK_EQ(r.unwrap_err(), ErrorCode::NotFound);
}

TEST_CASE("Fs::home_directory preserves a non-ASCII UTF-8 HOME (Windows)")
{
    // Task 34.1 pin: the UTF-8 env value is decoded through the wide path
    // constructor, not the active-code-page narrow one; `.u8string()` produces
    // the UTF-8 bytes and native path equality avoids a second ACP skew.
    auto guard = capture_env("HOME");
    // \u escapes keep the source ASCII; .u8string() yields the UTF-8 bytes.
    auto expected = Fs::temp_directory() /
                    std::filesystem::path(std::u8string(u8"pjh_home_\u4e2d\u6587_\u00e9"));
    auto u8 = expected.u8string();
    REQUIRE(Env::set("HOME", std::string(u8.begin(), u8.end())).is_ok());
    auto r = Fs::home_directory();
    REQUIRE(r.is_ok());
    CHECK_EQ(r.unwrap(), expected);
}
#endif
