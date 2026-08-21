#pragma once

#include "icad/compiler/ir/ir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace icad::document {

struct Diff {
    std::ptrdiff_t parameters{};
    std::ptrdiff_t profiles{};
    std::ptrdiff_t bodies{};
    std::ptrdiff_t features{};
    std::ptrdiff_t scenes{};
};

[[nodiscard]] auto fingerprint(const compiler::ir::Project& project) -> std::uint64_t;
[[nodiscard]] auto fingerprint(std::string_view source) -> std::uint64_t;
[[nodiscard]] auto revision_id(std::uint64_t fingerprint_value) -> std::string;
[[nodiscard]] auto revision_id(std::string_view source) -> std::string;
[[nodiscard]] auto diff(const compiler::ir::Project& before,
                        const compiler::ir::Project& after) -> Diff;

class RevisionStore {
  public:
    explicit RevisionStore(compiler::ir::Project initial);

    [[nodiscard]] auto revision() const -> std::uint64_t;
    [[nodiscard]] auto current() const -> const compiler::ir::Project&;
    [[nodiscard]] auto commit(compiler::ir::Project next, std::uint64_t expected_revision)
        -> bool;
    [[nodiscard]] auto undo() -> bool;
    [[nodiscard]] auto redo() -> bool;

  private:
    std::vector<compiler::ir::Project> history_;
    std::size_t cursor_{};
};

} // namespace icad::document
