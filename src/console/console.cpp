#include <pjh_platform/console.hpp>
#include <utility>

#include "console_internal.hpp"

namespace pjh::platform
{
    ConsoleMode::~ConsoleMode() { detail::restore_best_effort(impl_.get()); }

    ConsoleMode::ConsoleMode(ConsoleMode &&other) noexcept = default;

    auto ConsoleMode::operator=(ConsoleMode &&other) noexcept -> ConsoleMode &
    {
        if (this == &other)
            return *this;
        // Release the current guard's terminal state before stealing the other's,
        // so a live raw state is not silently dropped (FileWatcher's move lesson).
        detail::restore_best_effort(impl_.get());
        impl_ = std::move(other.impl_);
        return *this;
    }
}  // namespace pjh::platform
