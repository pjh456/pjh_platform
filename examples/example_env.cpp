// example_env.cpp — Env get/set/unset round-trip plus snapshot and list.
#include <iostream>
#include <pjh_platform/env.hpp>
#include <string_view>

namespace
{

    constexpr std::string_view kVarName = "__PJH_EXAMPLE_VAR__";
    constexpr std::string_view kVarValue = "hello_pjh_platform";

    auto fail(std::string_view op, int code) -> int
    {
        std::cerr << "example_env: " << op << " failed (code " << code << ")\n";
        return 1;
    }

}  // namespace

int main()
{
    using pjh::platform::Env;

    if (auto r = Env::set(kVarName, kVarValue); r.is_err())
        return fail("set", static_cast<int>(r.unwrap_err()));

    auto value = Env::get(kVarName);
    if (value.is_err() || value.unwrap() != kVarValue)
        return fail("get", static_cast<int>(value.unwrap_err()));
    std::cout << "get " << kVarName << " = " << value.unwrap() << '\n';

    std::cout << "snapshot contains variable: "
              << (Env::snapshot().count(std::string(kVarName)) != 0) << '\n';
    std::cout << "list size: " << Env::list().size() << '\n';

    if (auto r = Env::unset(kVarName); r.is_err())
        return fail("unset", static_cast<int>(r.unwrap_err()));

    auto missing = Env::get(kVarName);
    std::cout << "get after unset: "
              << (missing.is_err() ? "not found as expected" : "still present") << '\n';
    return missing.is_err() ? 0 : 1;
}
