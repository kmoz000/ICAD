#include "icad/document/source.hpp"

#include "icad/compiler/compiler.hpp"
#include "icad/document/revision.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <system_error>

namespace icad::document {
namespace {

constexpr std::uintmax_t maximum_source_bytes = 64U * 1024U * 1024U;

struct ResolvedPath {
    SourceStatus status{SourceStatus::path_error};
    std::string message;
    std::filesystem::path root;
    std::filesystem::path relative;
    std::filesystem::path absolute;

    [[nodiscard]] auto ok() const -> bool { return status == SourceStatus::ok; }
};

[[nodiscard]] auto within(const std::filesystem::path& candidate,
                          const std::filesystem::path& root) -> bool {
    std::error_code error;
    const auto relative = std::filesystem::relative(candidate, root, error);
    if (error || relative.empty() || relative.is_absolute()) return false;
    for (const auto& component : relative) {
        if (component == "..") return false;
    }
    return true;
}

[[nodiscard]] auto resolve(const std::filesystem::path& workspace,
                           const std::filesystem::path& relative_path,
                           bool require_existing) -> ResolvedPath {
    if (relative_path.empty() || relative_path.is_absolute() ||
        relative_path.extension() != ".icad") {
        return {SourceStatus::path_error,
                "project path must be a relative .icad file", {}, {}, {}};
    }
    const auto normalized = relative_path.lexically_normal();
    for (const auto& component : normalized) {
        if (component == "..") {
            return {SourceStatus::path_error,
                    "project path cannot traverse outside the workspace", {}, {}, {}};
        }
    }
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(workspace, error);
    if (error) {
        return {SourceStatus::path_error,
                "cannot resolve workspace: " + error.message(), {}, {}, {}};
    }
    const auto candidate = std::filesystem::weakly_canonical(root / normalized, error);
    if (error || !within(candidate, root)) {
        return {SourceStatus::path_error,
                "project path resolves outside the workspace", {}, {}, {}};
    }
    if (require_existing && !std::filesystem::is_regular_file(candidate, error)) {
        return {SourceStatus::not_found, "project source does not exist", {}, {}, {}};
    }
    return {SourceStatus::ok, {}, root, normalized, candidate};
}

[[nodiscard]] auto read_file(const std::filesystem::path& path,
                             std::string& message) -> std::optional<std::string> {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        message = "cannot inspect project source: " + error.message();
        return std::nullopt;
    }
    if (size > maximum_source_bytes) {
        message = "project source exceeds the 64 MiB safety limit";
        return std::nullopt;
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        message = "cannot read project source";
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{input},
                       std::istreambuf_iterator<char>{}};
}

[[nodiscard]] auto history_directory(const ResolvedPath& path) -> std::filesystem::path {
    const auto key = revision_id(path.relative.generic_string()) + "-" +
                     path.relative.filename().string();
    return path.root / ".icad-history" / key;
}

[[nodiscard]] auto valid_revision(std::string_view revision) -> bool {
    if (revision.size() != 16) return false;
    return std::ranges::all_of(revision, [](char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

auto archive(const ResolvedPath& path, std::string_view source,
             std::string_view revision, std::string& message) -> bool {
    std::error_code error;
    const auto directory = history_directory(path);
    std::filesystem::create_directories(directory, error);
    if (error) {
        message = "cannot create revision history: " + error.message();
        return false;
    }
    const auto destination = directory / (std::string{revision} + ".icad");
    if (std::filesystem::exists(destination)) return true;
    std::ofstream output{destination, std::ios::binary};
    if (!output) {
        message = "cannot archive project revision";
        return false;
    }
    output.write(source.data(), static_cast<std::streamsize>(source.size()));
    output.close();
    if (!output) {
        message = "cannot finish project revision archive";
        return false;
    }
    return true;
}

class CommitLock {
  public:
    explicit CommitLock(const std::filesystem::path& target)
        : path_{target.parent_path() / ("." + target.filename().string() + ".icad-lock")} {
        std::error_code error;
        acquired_ = std::filesystem::create_directory(path_, error);
    }
    CommitLock(const CommitLock&) = delete;
    auto operator=(const CommitLock&) -> CommitLock& = delete;
    ~CommitLock() {
        if (acquired_) {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
    }
    [[nodiscard]] auto acquired() const -> bool { return acquired_; }

  private:
    std::filesystem::path path_;
    bool acquired_{false};
};

struct TemporaryFile {
    std::filesystem::path path;
    std::ofstream stream;
};

[[nodiscard]] auto create_temporary(const std::filesystem::path& target,
                                    std::string_view revision)
    -> std::optional<TemporaryFile> {
    static std::atomic_uint64_t sequence{0};
    for (std::size_t attempt = 0; attempt < 100; ++attempt) {
        const auto path = target.parent_path() /
                          ("." + target.filename().string() + ".icad-tmp-" +
                           std::string{revision} + "-" +
                           std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        std::ofstream stream{path, std::ios::binary | std::ios::out | std::ios::noreplace};
        if (stream) return TemporaryFile{path, std::move(stream)};
    }
    return std::nullopt;
}

[[nodiscard]] auto valid_identifier(std::string_view value) -> bool {
    if (value.empty() ||
        !((value.front() >= 'A' && value.front() <= 'Z') ||
          (value.front() >= 'a' && value.front() <= 'z'))) {
        return false;
    }
    return std::ranges::all_of(value.substr(1), [](char character) {
        return (character >= 'A' && character <= 'Z') ||
               (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '_';
    });
}

[[nodiscard]] auto commit(const ResolvedPath& path, std::string source,
                          std::string_view expected_revision) -> SourceChange {
    if (source.size() > maximum_source_bytes) {
        return {SourceStatus::invalid_source,
                "project source exceeds the 64 MiB safety limit", path.relative,
                {}, {}, {}};
    }
    std::error_code error;
    std::filesystem::create_directories(path.absolute.parent_path(), error);
    if (error) {
        return {SourceStatus::io_error,
                "cannot create project directory: " + error.message(), path.relative,
                {}, {}, {}};
    }
    const CommitLock lock{path.absolute};
    if (!lock.acquired()) {
        return {SourceStatus::conflict,
                "project is locked by another commit", path.relative,
                {}, {}, {}};
    }
    std::string old_source;
    std::string old_revision{absent_revision};
    if (std::filesystem::exists(path.absolute, error)) {
        std::string read_error;
        auto existing = read_file(path.absolute, read_error);
        if (!existing) {
            return {SourceStatus::io_error, std::move(read_error), path.relative, {}, {}, {}};
        }
        old_source = std::move(*existing);
        old_revision = revision_id(old_source);
    }
    if (old_revision != expected_revision) {
        return {SourceStatus::conflict,
                "expected revision does not match current source",
                path.relative,
                old_revision,
                old_revision,
                {}};
    }

    const auto compilation = compiler::compile(source);
    if (!compilation.ok()) {
        SourceChange result{SourceStatus::invalid_source,
                            "source failed compiler validation",
                            path.relative,
                            old_revision,
                            old_revision,
                            {}};
        result.diagnostics = compilation.diagnostics;
        return result;
    }
    const auto new_revision = revision_id(source);
    if (new_revision == old_revision) {
        return {SourceStatus::ok, "source is unchanged", path.relative,
                old_revision, new_revision, {}};
    }

    std::string archive_error;
    if (old_revision != absent_revision &&
        !archive(path, old_source, old_revision, archive_error)) {
        return {SourceStatus::io_error, std::move(archive_error), path.relative,
                {}, {}, {}};
    }
    auto temporary = create_temporary(path.absolute, new_revision);
    if (!temporary) {
        return {SourceStatus::io_error, "cannot create atomic project update",
                path.relative, {}, {}, {}};
    }
    temporary->stream.write(source.data(), static_cast<std::streamsize>(source.size()));
    temporary->stream.close();
    if (!temporary->stream) {
        std::filesystem::remove(temporary->path, error);
        return {SourceStatus::io_error, "cannot finish atomic project update",
                path.relative, {}, {}, {}};
    }
    std::filesystem::rename(temporary->path, path.absolute, error);
    if (error) {
        std::filesystem::remove(temporary->path);
        return {SourceStatus::io_error,
                "cannot commit atomic project update: " + error.message(), path.relative,
                {}, {}, {}};
    }
    if (!archive(path, source, new_revision, archive_error)) {
        return {SourceStatus::io_error,
                "source committed but history archive failed: " + archive_error,
                path.relative, old_revision, new_revision, {}};
    }
    return {SourceStatus::ok, "source committed", path.relative,
            old_revision, new_revision, {}};
}

[[nodiscard]] auto format_number(double value) -> std::string {
    char buffer[64]{};
    const auto conversion = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                          std::chars_format::general,
                                          std::numeric_limits<double>::max_digits10);
    return std::string{buffer, conversion.ptr};
}

[[nodiscard]] auto replace_parameter_line(std::string_view source,
                                          std::string_view parameter,
                                          double value,
                                          std::string_view unit)
    -> std::optional<std::string> {
    std::size_t position = 0;
    while (position < source.size()) {
        const auto newline = source.find('\n', position);
        const auto end = newline == std::string_view::npos ? source.size() : newline;
        auto line = source.substr(position, end - position);
        const bool carriage_return = !line.empty() && line.back() == '\r';
        if (carriage_return) line.remove_suffix(1);
        const auto content_end = line.find('#');
        const auto content = line.substr(0, content_end);
        std::size_t cursor = 0;
        const auto skip = [&] {
            while (cursor < content.size() &&
                   (content[cursor] == ' ' || content[cursor] == '\t')) ++cursor;
        };
        const auto token = [&]() -> std::string_view {
            skip();
            const auto start = cursor;
            while (cursor < content.size() && content[cursor] != ' ' &&
                   content[cursor] != '\t') ++cursor;
            return content.substr(start, cursor - start);
        };
        skip();
        const auto indentation = content.substr(0, cursor);
        if (token() == "PARAMETER" && token() == parameter) {
            std::string replacement{indentation};
            replacement += "PARAMETER ";
            replacement += parameter;
            replacement.push_back(' ');
            replacement += format_number(value);
            replacement.push_back(' ');
            replacement += unit;
            if (content_end != std::string_view::npos) {
                replacement.push_back(' ');
                replacement += line.substr(content_end);
            }
            if (carriage_return) replacement.push_back('\r');
            std::string result{source};
            result.replace(position, end - position, replacement);
            return result;
        }
        if (newline == std::string_view::npos) break;
        position = newline + 1;
    }
    return std::nullopt;
}

} // namespace

auto read_source(const std::filesystem::path& workspace,
                 const std::filesystem::path& relative_path) -> SourceSnapshot {
    const auto path = resolve(workspace, relative_path, true);
    if (!path.ok()) return {path.status, path.message, relative_path, {}, {}};
    std::string message;
    auto source = read_file(path.absolute, message);
    if (!source) return {SourceStatus::io_error, std::move(message), path.relative, {}, {}};
    const auto revision = revision_id(*source);
    return {SourceStatus::ok, "source read", path.relative, std::move(*source), revision};
}

auto write_source(const std::filesystem::path& workspace,
                  const std::filesystem::path& relative_path,
                  std::string source,
                  std::string_view expected_revision) -> SourceChange {
    const auto path = resolve(workspace, relative_path, false);
    if (!path.ok()) return {path.status, path.message, relative_path, {}, {}, {}};
    return commit(path, std::move(source), expected_revision);
}

auto set_parameter(const std::filesystem::path& workspace,
                   const std::filesystem::path& relative_path,
                   std::string_view parameter, double value,
                   std::string_view unit,
                   std::string_view expected_revision) -> SourceChange {
    if (!std::isfinite(value) || !valid_identifier(parameter) || !valid_identifier(unit)) {
        return {SourceStatus::invalid_source, "parameter edit arguments are invalid",
                relative_path, {}, {}, {}};
    }
    const auto snapshot = read_source(workspace, relative_path);
    if (!snapshot.ok()) return {snapshot.status, snapshot.message, relative_path, {}, {}, {}};
    if (snapshot.revision != expected_revision) {
        return {SourceStatus::conflict,
                "expected revision does not match current source",
                snapshot.path,
                snapshot.revision,
                snapshot.revision,
                {}};
    }
    auto updated = replace_parameter_line(snapshot.source, parameter, value, unit);
    if (!updated) {
        return {SourceStatus::invalid_source,
                "parameter declaration was not found", snapshot.path,
                snapshot.revision, snapshot.revision, {}};
    }
    const auto path = resolve(workspace, relative_path, true);
    if (!path.ok()) return {path.status, path.message, relative_path, {}, {}, {}};
    return commit(path, std::move(*updated), expected_revision);
}

auto set_parameters(const std::filesystem::path& workspace,
                    const std::filesystem::path& relative_path,
                    const std::vector<ParameterEdit>& edits,
                    std::string_view expected_revision) -> SourceChange {
    if (edits.empty()) {
        return {SourceStatus::invalid_source, "parameter edit list cannot be empty",
                relative_path, {}, {}, {}};
    }
    for (const auto& edit : edits) {
        if (!std::isfinite(edit.value) || !valid_identifier(edit.name) ||
            !valid_identifier(edit.unit)) {
            return {SourceStatus::invalid_source, "parameter edit arguments are invalid",
                    relative_path, {}, {}, {}};
        }
    }
    const auto snapshot = read_source(workspace, relative_path);
    if (!snapshot.ok())
        return {snapshot.status, snapshot.message, relative_path, {}, {}, {}};
    if (snapshot.revision != expected_revision) {
        return {SourceStatus::conflict, "expected revision does not match current source",
                snapshot.path, snapshot.revision, snapshot.revision, {}};
    }
    std::string updated = snapshot.source;
    for (const auto& edit : edits) {
        auto next = replace_parameter_line(updated, edit.name, edit.value, edit.unit);
        if (!next) {
            return {SourceStatus::invalid_source,
                    "parameter declaration was not found: " + edit.name, snapshot.path,
                    snapshot.revision, snapshot.revision, {}};
        }
        updated = std::move(*next);
    }
    const auto path = resolve(workspace, relative_path, true);
    if (!path.ok())
        return {path.status, path.message, relative_path, {}, {}, {}};
    return commit(path, std::move(updated), expected_revision);
}

auto source_history(const std::filesystem::path& workspace,
                    const std::filesystem::path& relative_path) -> SourceHistory {
    const auto path = resolve(workspace, relative_path, true);
    if (!path.ok()) return {path.status, path.message, {}};
    SourceHistory result{SourceStatus::ok, "history read", {}};
    const auto directory = history_directory(path);
    std::error_code error;
    if (!std::filesystem::exists(directory)) return result;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error) return {SourceStatus::io_error, "cannot read revision history", {}};
        if (entry.is_regular_file() && entry.path().extension() == ".icad") {
            const auto revision = entry.path().stem().string();
            if (valid_revision(revision)) result.revisions.push_back(revision);
        }
    }
    std::ranges::sort(result.revisions);
    return result;
}

auto restore_source(const std::filesystem::path& workspace,
                    const std::filesystem::path& relative_path,
                    std::string_view target_revision,
                    std::string_view expected_revision) -> SourceChange {
    if (!valid_revision(target_revision)) {
        return {SourceStatus::not_found, "target revision is invalid", relative_path,
                {}, {}, {}};
    }
    const auto path = resolve(workspace, relative_path, true);
    if (!path.ok()) return {path.status, path.message, relative_path, {}, {}, {}};
    const auto archived = history_directory(path) /
                          (std::string{target_revision} + ".icad");
    if (!std::filesystem::is_regular_file(archived)) {
        return {SourceStatus::not_found, "target revision was not found", path.relative,
                {}, {}, {}};
    }
    std::string message;
    auto source = read_file(archived, message);
    if (!source) return {SourceStatus::io_error, std::move(message), path.relative,
                        {}, {}, {}};
    return commit(path, std::move(*source), expected_revision);
}

} // namespace icad::document
