#ifndef INCLUDE_PJH_PLATFORM_ERROR_HPP
#define INCLUDE_PJH_PLATFORM_ERROR_HPP

#include <pjh_result/result.hpp>

#include <string>
#include <system_error>

namespace pjh::platform {

enum class ErrorCode {
    success = 0,
    not_found,
    permission_denied,
    already_exists,
    invalid_argument,
    not_supported,
    io_error,
    unknown,
};

inline std::error_code make_error_code(ErrorCode e) noexcept {
    static const struct : std::error_category {
        const char* name() const noexcept override { return "pjh_platform"; }
        std::string message(int c) const override {
            switch (static_cast<ErrorCode>(c)) {
                case ErrorCode::success: return "success";
                case ErrorCode::not_found: return "not found";
                case ErrorCode::permission_denied: return "permission denied";
                case ErrorCode::already_exists: return "already exists";
                case ErrorCode::invalid_argument: return "invalid argument";
                case ErrorCode::not_supported: return "not supported";
                case ErrorCode::io_error: return "I/O error";
                case ErrorCode::unknown: return "unknown error";
                default: return "unrecognized error";
            }
        }
    } cat;
    return {static_cast<int>(e), cat};
}

} // namespace pjh::platform

namespace std {
    template<> struct is_error_code_enum<pjh::platform::ErrorCode> : true_type {};
}

#endif // INCLUDE_PJH_PLATFORM_ERROR_HPP
