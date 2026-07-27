#pragma once

#include <system_error>
#include <string>

namespace pjh::platform {

enum class errc {
    success = 0,
    not_found,
    permission_denied,
    already_exists,
    invalid_argument,
    not_supported,
    io_error,
    unknown,
};

inline std::error_code make_error_code(errc e) noexcept {
    static const struct : std::error_category {
        const char* name() const noexcept override { return "pjh_platform"; }
        std::string message(int c) const override {
            switch (static_cast<errc>(c)) {
                case errc::success: return "success";
                case errc::not_found: return "not found";
                case errc::permission_denied: return "permission denied";
                case errc::already_exists: return "already exists";
                case errc::invalid_argument: return "invalid argument";
                case errc::not_supported: return "not supported";
                case errc::io_error: return "I/O error";
                case errc::unknown: return "unknown error";
                default: return "unrecognized error";
            }
        }
    } cat;
    return {static_cast<int>(e), cat};
}

} // namespace pjh::platform

namespace std {
    template<> struct is_error_code_enum<pjh::platform::errc> : true_type {};
}
