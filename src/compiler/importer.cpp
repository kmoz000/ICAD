#include "icad/compiler/importer.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <unordered_set>

namespace icad::compiler {
namespace {

struct State {
    ImportResult result;
    std::filesystem::path root;
    std::unordered_set<std::string> active;
    std::size_t bytes{};
};

[[nodiscard]] auto trim(std::string_view value) -> std::string_view {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
        value.remove_suffix(1);
    return value;
}

[[nodiscard]] auto import_argument(std::string_view line) -> std::string {
    auto value = trim(line);
    const auto space = value.find_first_of(" \t");
    if (space == std::string_view::npos)
        return {};
    value = trim(value.substr(space));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        value = value.substr(1, value.size() - 2);
    if (value.empty() || value.find_first_of("\r\n\0") != std::string_view::npos)
        return {};
    return std::string{value};
}

[[nodiscard]] auto directive(std::string_view line) -> bool {
    const auto value = trim(line);
    const auto keyword_end = value.find_first_of(" \t");
    const auto keyword = value.substr(0, keyword_end);
    return keyword == "IMPORT" || keyword == "INJECT";
}

[[nodiscard]] auto has_directive(std::string_view source) -> bool {
    std::size_t cursor = 0;
    while (cursor < source.size()) {
        const auto ending = source.find('\n', cursor);
        const auto length = ending == std::string_view::npos ? source.size() - cursor
                                                             : ending - cursor;
        if (directive(source.substr(cursor, length)))
            return true;
        if (ending == std::string_view::npos)
            return false;
        cursor = ending + 1;
    }
    return false;
}

[[nodiscard]] auto within_root(const std::filesystem::path& path,
                               const std::filesystem::path& root) -> bool {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && relative != std::filesystem::path{"."} &&
           relative.begin() != relative.end() &&
           *relative.begin() != std::filesystem::path{".."};
}

auto error(State& state, std::string code, std::string message, std::size_t line) -> void {
    state.result.diagnostics.push_back(
        {DiagnosticSeverity::error, std::move(code), std::move(message), {line, 1}});
}

auto expand(State& state, std::string_view source, const std::filesystem::path& current_file,
            const ImportOptions& options, std::size_t depth) -> void {
    std::size_t cursor = 0;
    std::size_t line_number = 1;
    while (cursor < source.size()) {
        const auto ending = source.find('\n', cursor);
        const auto length = ending == std::string_view::npos ? source.size() - cursor
                                                             : ending - cursor;
        const auto line = source.substr(cursor, length);
        if (!directive(line)) {
            state.result.source.append(line);
            state.result.source.push_back('\n');
        } else {
            const auto argument = import_argument(line);
            if (argument.empty()) {
                error(state, "ICAD-I0001",
                      "IMPORT/INJECT expects one relative .icad module path", line_number);
            } else if (depth >= options.maximum_depth) {
                error(state, "ICAD-I0002", "maximum ICAD import depth exceeded", line_number);
            } else {
                std::error_code filesystem_error;
                const auto candidate =
                    std::filesystem::weakly_canonical(current_file.parent_path() / argument,
                                                      filesystem_error);
                if (filesystem_error || candidate.extension() != ".icad" ||
                    !within_root(candidate, state.root)) {
                    error(state, "ICAD-I0003",
                          "import must resolve to a .icad file inside the project root: " +
                              argument,
                          line_number);
                } else if (!std::filesystem::is_regular_file(candidate, filesystem_error)) {
                    error(state, "ICAD-I0004", "cannot read imported ICAD module: " + argument,
                          line_number);
                } else if (state.active.contains(candidate.string())) {
                    error(state, "ICAD-I0005", "cyclic ICAD import: " + argument, line_number);
                } else {
                    std::ifstream input{candidate, std::ios::binary};
                    const std::string imported{std::istreambuf_iterator<char>{input},
                                               std::istreambuf_iterator<char>{}};
                    if (!input.good() && !input.eof()) {
                        error(state, "ICAD-I0004",
                              "cannot read imported ICAD module: " + argument, line_number);
                    } else if (imported.size() > options.maximum_bytes -
                                                       std::min(state.bytes, options.maximum_bytes)) {
                        error(state, "ICAD-I0006", "maximum imported source size exceeded",
                              line_number);
                    } else {
                        state.bytes += imported.size();
                        state.active.insert(candidate.string());
                        state.result.dependencies.push_back(candidate);
                        expand(state, imported, candidate, options, depth + 1);
                        state.active.erase(candidate.string());
                    }
                }
            }
        }
        if (ending == std::string_view::npos)
            break;
        cursor = ending + 1;
        ++line_number;
    }
}

} // namespace

auto expand_imports(std::string_view source, const ImportOptions& options) -> ImportResult {
    State state;
    if (source.size() > options.maximum_bytes) {
        error(state, "ICAD-I0006", "maximum imported source size exceeded", 1);
        return state.result;
    }
    if (options.source_path.empty()) {
        state.result.source.assign(source);
        if (has_directive(source)) {
            error(state, "ICAD-I0001", "IMPORT/INJECT requires a source file path", 1);
        }
        return state.result;
    }
    std::error_code filesystem_error;
    const auto source_path =
        std::filesystem::weakly_canonical(options.source_path, filesystem_error);
    if (filesystem_error) {
        error(state, "ICAD-I0001", "cannot resolve the ICAD source path", 1);
        return state.result;
    }
    const auto requested_root =
        options.project_root.empty() ? source_path.parent_path() : options.project_root;
    state.root = std::filesystem::weakly_canonical(requested_root, filesystem_error);
    if (filesystem_error || (!within_root(source_path, state.root) && source_path != state.root)) {
        error(state, "ICAD-I0003", "source file is outside the ICAD project root", 1);
        return state.result;
    }
    state.bytes = source.size();
    state.active.insert(source_path.string());
    expand(state, source, source_path, options, 0);
    std::ranges::sort(state.result.dependencies);
    state.result.dependencies.erase(
        std::unique(state.result.dependencies.begin(), state.result.dependencies.end()),
        state.result.dependencies.end());
    return state.result;
}

} // namespace icad::compiler
