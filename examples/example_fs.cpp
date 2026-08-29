// example_fs.cpp — Fs create/write/read/copy/list/remove round-trip in a temp sandbox.
#include <chrono>
#include <filesystem>
#include <iostream>
#include <pjh_platform/fs.hpp>
#include <string>
#include <string_view>

namespace
{

    auto fail(std::string_view op, int code) -> int
    {
        std::cerr << "example_fs: " << op << " failed (code " << code << ")\n";
        return 1;
    }

    struct cleanup
    {
        std::filesystem::path root;
        ~cleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }
    };

}  // namespace

int main()
{
    using pjh::platform::Fs;

    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path root = Fs::temp_directory() / ("pjh_example_fs_" + suffix);
    cleanup c{root};

    if (auto r = Fs::create_directories(root / "a" / "b"); r.is_err())
        return fail("create_directories", static_cast<int>(r.unwrap_err()));

    const std::string content = "hello pjh_platform\n";
    if (auto r = Fs::write_file(root / "a" / "hello.txt", content); r.is_err())
        return fail("write_file", static_cast<int>(r.unwrap_err()));

    auto read = Fs::read_file(root / "a" / "hello.txt");
    if (read.is_err() || read.unwrap() != content)
        return fail("read_file", static_cast<int>(read.unwrap_err()));
    std::cout << "read_file round-trip: " << read.unwrap() << std::flush;

    if (auto r = Fs::copy_file(root / "a" / "hello.txt", root / "a" / "copy.txt"); r.is_err())
        return fail("copy_file", static_cast<int>(r.unwrap_err()));

    if (auto r = Fs::copy_directory(root / "a", root / "b"); r.is_err())
        return fail("copy_directory", static_cast<int>(r.unwrap_err()));

    auto entries = Fs::list_directory(root / "b");
    if (entries.is_err())
        return fail("list_directory", static_cast<int>(entries.unwrap_err()));
    std::cout << "list_directory entries in b: " << entries.unwrap().size() << '\n';
    bool saw_hello = false;
    bool saw_copy = false;
    for (const auto &p : entries.unwrap())
    {
        saw_hello = saw_hello || p.filename() == "hello.txt";
        saw_copy = saw_copy || p.filename() == "copy.txt";
    }
    if (!saw_hello || !saw_copy)
    {
        std::cerr << "example_fs: copied files missing from listing of b\n";
        return 1;
    }

    auto removed = Fs::remove_all(root);
    if (removed.is_err())
        return fail("remove_all", static_cast<int>(removed.unwrap_err()));
    std::cout << "remove_all removed: " << removed.unwrap() << '\n';
    std::cout << "example_fs: ok\n";
    return 0;
}
