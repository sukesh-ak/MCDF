// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "mcdf/error.hpp"

namespace mcdf {

class Container;

// Packs every member of a container into a deterministic TAR (.mcdf) archive.
Result<std::string> pack_container(const Container& container);

// Unpacks a TAR archive into a destination directory, guarding path escapes.
Result<void> unpack_archive(std::string_view archive,
                            const std::filesystem::path& dest);

}  // namespace mcdf
