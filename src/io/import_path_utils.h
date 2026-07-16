#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace mulan::io::detail {

inline std::optional<std::filesystem::path> resolveContainedImportPath(const std::filesystem::path& baseDirectory,
                                                                       const std::filesystem::path& reference) {
    if (reference.empty() || reference.has_root_name() || reference.has_root_directory() || reference.is_absolute())
        return std::nullopt;

    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(baseDirectory, ec);
    if (ec)
        return std::nullopt;
    const auto candidate = std::filesystem::weakly_canonical(root / reference, ec);
    if (ec)
        return std::nullopt;

    const auto relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute())
        return std::nullopt;
    for (const auto& component : relative) {
        if (component == "..")
            return std::nullopt;
    }
    return candidate;
}

inline bool importFileWithinLimit(const std::filesystem::path& path, uint64_t maxBytes) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec)
        return true;  // The importer will report a missing file with its normal diagnostic.
    if (!std::filesystem::is_regular_file(path, ec) || ec)
        return false;
    const auto size = std::filesystem::file_size(path, ec);
    return !ec && size <= maxBytes;
}

}  // namespace mulan::io::detail
