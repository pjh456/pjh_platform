#include <doctest/doctest.h>

#include <fstream>
#include <iostream>
#include <pjh_platform/fs.hpp>
#include <pjh_platform/os.hpp>

using pjh::platform::Fs;

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
        p.string(), std::filesystem::path("a") / std::filesystem::path("b") /
                        std::filesystem::path("c.txt"));

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
    auto r =
        Fs::relative(std::filesystem::path("a/./b/../b"), std::filesystem::path("a/b/c"));
    REQUIRE(r.is_ok());
    CHECK_EQ(r.unwrap().generic_string(), std::filesystem::path("c").generic_string());
}

TEST_CASE("Fs::relative fails when paths share no common root")
{
    if (pjh::platform::Os::is_windows)
    {
        auto r = Fs::relative(
            std::filesystem::path("C:\\dir"), std::filesystem::path("D:\\dir\\file"));
        CHECK(r.is_err());
        CHECK_EQ(r.unwrap_err(), pjh::platform::ErrorCode::InvalidArgument);
    }
    else
    {
        auto r = Fs::relative(
            std::filesystem::path("relative"), std::filesystem::path("/absolute"));
        CHECK(r.is_err());
        CHECK_EQ(r.unwrap_err(), pjh::platform::ErrorCode::InvalidArgument);
    }
}
