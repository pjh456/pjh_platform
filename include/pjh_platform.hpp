#ifndef INCLUDE_PJH_PLATFORM_HPP
#define INCLUDE_PJH_PLATFORM_HPP

/// @file pjh_platform.hpp
/// @brief Umbrella header exposing the complete pjh_platform API.
/// @details Includes the `Os`, `Encoding`, `Env`, `Fs`, `FileWatcher`,
///          `DirectorySnapshot`, `DirectoryDiff`, `DirectoryStatus`, and
///          `ErrorCode` module headers. The platform detection macros
///          (`pjh_platform/platform.hpp`) are deliberately not included here;
///          include that header directly when you need the `PJH_PLATFORM_*`
///          macros.
/// @platform Windows, Linux, macOS.

#include "pjh_platform/directory_diff.hpp"
#include "pjh_platform/directory_snapshot.hpp"
#include "pjh_platform/directory_status.hpp"
#include "pjh_platform/encoding.hpp"
#include "pjh_platform/env.hpp"
#include "pjh_platform/error.hpp"
#include "pjh_platform/file_watcher.hpp"
#include "pjh_platform/fs.hpp"
#include "pjh_platform/os.hpp"

#endif  // INCLUDE_PJH_PLATFORM_HPP
