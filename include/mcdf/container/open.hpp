// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <filesystem>
#include <memory>

#include "mcdf/container/container.hpp"
#include "mcdf/error.hpp"

namespace mcdf {

// Opens a read-only container from a path: a directory yields a
// DirectoryContainer, a regular file yields a TarContainer (.mcdf archive).
Result<std::unique_ptr<Container>> open_container(
    const std::filesystem::path& path);

}  // namespace mcdf
