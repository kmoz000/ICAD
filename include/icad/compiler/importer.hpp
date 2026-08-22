#pragma once

#include "icad/compiler/diagnostics/diagnostic.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace icad::compiler {

struct ImportOptions {
    std::filesystem::path source_path;
    std::filesystem::path project_root;
    std::size_t maximum_depth{32};
    std::size_t maximum_bytes{256U * 1024U * 1024U};
};

struct ImportResult {
    std::string source;
    std::vector<std::filesystem::path> dependencies;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] auto ok() const noexcept -> bool { return diagnostics.empty(); }
};

// Expands `IMPORT "relative/file.icad"` and its explicit alias `INJECT`.
// Imports are text-only ICAD fragments and are confined to project_root.
[[nodiscard]] auto expand_imports(std::string_view source, const ImportOptions& options)
    -> ImportResult;

} // namespace icad::compiler
