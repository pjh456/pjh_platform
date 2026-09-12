// example_utf8_entry.cpp — copy-pasteable Windows-safe UTF-8 entry point.
//
// On Windows the C runtime decodes the wide command line into the narrow
// argv with the process ANSI code page, so non-ASCII arguments are already
// lossy by the time main() runs. Console::utf8_arguments() recovers them from
// the wide command line; on POSIX it copies argv verbatim. pjh_platform does
// not replace main(), so this file is the portable entry pattern to copy.
//
// Try it on Windows with a Chinese argument:
//   example_utf8_entry.exe zhongwen canshu
#include <iostream>
#include <pjh_platform/console.hpp>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    // Windows: set the console code page to UTF-8 when stdout is a console
    // (a successful no-op for redirected output and on POSIX).
    if (auto r = pjh::platform::Console::enable_utf8(); r.is_ok())
        std::cout << "enable_utf8: ok\n";
    else
        std::cout << "enable_utf8: code " << static_cast<int>(r.unwrap_err()) << '\n';

    // UTF-8 arguments (Windows: from the wide command line; POSIX: argv copy).
    const std::vector<std::string> args = pjh::platform::Console::utf8_arguments(argc, argv);
    std::cout << "argc: " << args.size() << '\n';
    for (std::size_t i = 0; i < args.size(); ++i) std::cout << '[' << i << "] " << args[i] << '\n';

    // Byte-exact UTF-8 for a Chinese output line, kept as \x escapes so the
    // source file stays ASCII (no MSVC /utf-8 requirement).
    std::cout << "\xE4\xB8\xAD\xE6\x96\x87\xE8\xBE\x93\xE5\x87\xBA OK\n";
    std::cout << "example_utf8_entry: ok\n";
    return 0;
}
