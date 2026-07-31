#ifndef INCLUDE_PJH_PLATFORM_HPP
#define INCLUDE_PJH_PLATFORM_HPP

/// @file pjh_platform.hpp
/// @brief Umbrella header exposing the complete pjh_platform API.
/// @details Includes the platform detection macros and the `Os`, `Env`, `Fs`,
///          `FileWatcher`, `DirectorySnapshot`, and `Encoding` modules.
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
