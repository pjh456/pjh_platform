// example_console.cpp — non-interactive Console capability demo.
#include <iostream>
#include <pjh_platform/console.hpp>
#include <pjh_platform/encoding.hpp>
#include <string>
#include <string_view>

namespace
{

    // Byte-exact UTF-8 samples kept as \x escapes so this file stays ASCII.
    constexpr std::string_view kZhongWen = "\xE4\xB8\xAD\xE6\x96\x87";  // 中文
    constexpr std::string_view kNiHao = "\xE4\xBD\xA0\xE5\xA5\xBD";     // 你好
    constexpr std::size_t kColumnWidth = 8;

    auto pad(std::string_view text) -> std::string
    {
        const std::size_t width = pjh::platform::Encoding::display_width(text);
        if (width >= kColumnWidth)
            return std::string(text);
        return std::string(text) + std::string(kColumnWidth - width, ' ');
    }

}  // namespace

int main(int argc, char **argv)
{
    using pjh::platform::Console;
    using pjh::platform::ErrorCode;

    if (auto r = Console::enable_utf8(); r.is_ok())
        std::cout << "enable_utf8: ok\n";
    else
        std::cout << "enable_utf8: code " << static_cast<int>(r.unwrap_err()) << '\n';

    std::cout << "is_tty(0/1/2): " << Console::is_tty(0) << '/' << Console::is_tty(1) << '/'
              << Console::is_tty(2) << '\n';

    if (auto s = Console::size(); s.is_ok())
        std::cout << "size: " << s.unwrap().columns << " x " << s.unwrap().rows << '\n';
    else if (s.unwrap_err() == ErrorCode::NotATerminal)
        std::cout << "size: NotATerminal (" << static_cast<int>(s.unwrap_err()) << ")\n";
    else
        std::cout << "size: code " << static_cast<int>(s.unwrap_err()) << '\n';

    std::cout << "supports_ansi: " << Console::supports_ansi() << '\n';

    auto args = Console::utf8_arguments(argc, argv);
    std::cout << "arguments: " << args.size();
    for (std::size_t i = 0; i < args.size() && i < 3; ++i)
        std::cout << (i == 0 ? " [" : ", ") << args[i];
    if (!args.empty())
        std::cout << ']';
    std::cout << '\n';

    // Consume Encoding::display_width for CJK-aligned columns.
    std::cout << pad(kZhongWen) << "| aligned\n";
    std::cout << pad(kNiHao) << "| aligned\n";

    std::cout << "example_console: ok\n";
    return 0;
}
