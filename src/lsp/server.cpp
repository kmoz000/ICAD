#include "icad/lsp/server.hpp"

#include "icad/compiler/compiler.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace icad::lsp {
namespace {

[[nodiscard]] auto escaped(std::string_view value) -> std::string {
    std::string result;
    for (const char character : value) {
        switch (character) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result.push_back(character); break;
        }
    }
    return result;
}

[[nodiscard]] auto string_field(std::string_view json, std::string_view name) -> std::string {
    const std::string key = "\"" + std::string{name} + "\"";
    auto position = json.find(key);
    if (position == std::string_view::npos) {
        return {};
    }
    position = json.find(':', position + key.size());
    if (position == std::string_view::npos) {
        return {};
    }
    position = json.find('"', position + 1);
    if (position == std::string_view::npos) {
        return {};
    }
    std::string value;
    bool slash = false;
    for (++position; position < json.size(); ++position) {
        const char character = json[position];
        if (slash) {
            switch (character) {
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            default: value.push_back(character); break;
            }
            slash = false;
        } else if (character == '\\') {
            slash = true;
        } else if (character == '"') {
            break;
        } else {
            value.push_back(character);
        }
    }
    return value;
}

[[nodiscard]] auto id_field(std::string_view json) -> std::string {
    auto position = json.find("\"id\"");
    if (position == std::string_view::npos) {
        return {};
    }
    position = json.find(':', position + 4);
    if (position == std::string_view::npos) {
        return {};
    }
    ++position;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) {
        ++position;
    }
    if (position >= json.size()) {
        return {};
    }
    if (json[position] == '"') {
        const auto end = json.find('"', position + 1);
        return end == std::string_view::npos ? std::string{} : std::string{json.substr(position, end - position + 1)};
    }
    auto end = position;
    while (end < json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) ||
                                 json[end] == '-')) {
        ++end;
    }
    return std::string{json.substr(position, end - position)};
}

[[nodiscard]] auto number_field(std::string_view json, std::string_view name,
                                std::size_t start = 0) -> std::size_t {
    const std::string key = "\"" + std::string{name} + "\"";
    auto position = json.find(key, start);
    if (position == std::string_view::npos)
        return 0;
    position = json.find(':', position + key.size());
    if (position == std::string_view::npos)
        return 0;
    ++position;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position])))
        ++position;
    std::size_t value{};
    while (position < json.size() && std::isdigit(static_cast<unsigned char>(json[position]))) {
        value = value * 10 + static_cast<std::size_t>(json[position] - '0');
        ++position;
    }
    return value;
}

[[nodiscard]] auto word_at(std::string_view source, std::size_t target_line,
                           std::size_t target_column) -> std::string {
    std::size_t line{};
    std::size_t start{};
    while (line < target_line && start < source.size()) {
        const auto newline = source.find('\n', start);
        if (newline == std::string_view::npos)
            return {};
        start = newline + 1;
        ++line;
    }
    const auto end = source.find('\n', start);
    const auto line_end = end == std::string_view::npos ? source.size() : end;
    std::size_t position = std::min(start + target_column, line_end);
    while (position > start && (std::isalnum(static_cast<unsigned char>(source[position - 1])) ||
                                source[position - 1] == '_'))
        --position;
    auto word_end = std::min(start + target_column, line_end);
    while (word_end < line_end &&
           (std::isalnum(static_cast<unsigned char>(source[word_end])) || source[word_end] == '_'))
        ++word_end;
    return std::string{source.substr(position, word_end - position)};
}

struct DefinitionLocation {
    std::size_t line{};
    std::size_t column{};
    bool found{};
};

[[nodiscard]] auto definition_of(std::string_view source, std::string_view symbol)
    -> DefinitionLocation {
    std::istringstream input{std::string{source}};
    std::string line;
    std::size_t line_number{};
    while (std::getline(input, line)) {
        std::istringstream words{line};
        std::string keyword_value;
        std::string declared;
        words >> keyword_value >> declared;
        constexpr std::string_view declarations[]{"PARAMETER", "ANGLE", "POINT3", "VECTOR",
                                                   "MATERIAL", "PROFILE", "SKETCH", "BODY",
                                                   "INSTANCE", "JOINT", "CONSTRAINT", "MATE",
                                                   "SCENE", "FEATURE", "TRACK"};
        if (declared == symbol &&
            std::ranges::find(declarations, keyword_value) != std::end(declarations)) {
            return {line_number, line.find(declared), true};
        }
        ++line_number;
    }
    return {};
}

[[nodiscard]] auto formatted(std::string_view source) -> std::string {
    std::istringstream input{std::string{source}};
    std::string result;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
            line.pop_back();
        result += line;
        result.push_back('\n');
    }
    return result;
}

[[nodiscard]] auto completion_items() -> std::string {
    constexpr std::string_view items[]{
        "PROJECT", "UNITS", "TOLERANCE", "PARAMETER", "ANGLE", "POINT3", "VECTOR",
        "POSE", "INSTANCE", "JOINT", "MATERIAL", "PRESET", "BASE_COLOR", "METALLIC",
        "ROUGHNESS", "TEXTURE_SCALE", "UV_MODE", "PROFILE", "SKETCH", "BODY", "FEATURE",
        "TYPE", "OPERATION", "CONSTRAINT", "MATE", "SCENE", "DURATION", "FPS",
        "BACKGROUND", "LOOP", "LIGHT", "EVENT", "TRACK", "EASING", "KEYFRAME", "END",
        "BOX", "CYLINDER", "CONE", "SPHERE", "EXTRUDE", "REVOLVE", "SWEEP", "LOFT",
        "FREEFORM", "UNION", "CUT", "INTERSECT", "STRUCTURAL_STEEL", "ALUMINUM",
        "CONCRETE", "ASPHALT", "GLASS", "WOOD"};
    std::string result{"["};
    for (std::size_t index = 0; index < std::size(items); ++index) {
        if (index != 0)
            result.push_back(',');
        result += "{\"label\":\"" + std::string{items[index]} +
                  "\",\"kind\":14,\"detail\":\"ICAD language\"}";
    }
    result.push_back(']');
    return result;
}

auto send(std::ostream& output, std::string_view message) -> void {
    output << "Content-Length: " << message.size() << "\r\n\r\n" << message;
    output.flush();
}

[[nodiscard]] auto read_message(std::istream& input, std::string& message) -> bool {
    std::string line;
    std::size_t length = 0;
    bool saw_header = false;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }
        saw_header = true;
        constexpr std::string_view prefix = "Content-Length:";
        if (line.starts_with(prefix)) {
            length = static_cast<std::size_t>(std::strtoull(line.c_str() + prefix.size(), nullptr, 10));
        }
    }
    if (!saw_header || length == 0) {
        return false;
    }
    message.assign(length, '\0');
    input.read(message.data(), static_cast<std::streamsize>(length));
    return input.gcount() == static_cast<std::streamsize>(length);
}

auto publish_diagnostics(std::ostream& output, std::string_view uri,
                         std::string_view source) -> void {
    const auto compilation = compiler::compile(source);
    std::string message = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"";
    message += escaped(uri);
    message += "\",\"diagnostics\":[";
    for (std::size_t index = 0; index < compilation.diagnostics.size(); ++index) {
        const auto& diagnostic = compilation.diagnostics[index];
        if (index != 0) {
            message.push_back(',');
        }
        const auto line = diagnostic.location.line > 0 ? diagnostic.location.line - 1 : 0;
        const auto column = diagnostic.location.column > 0 ? diagnostic.location.column - 1 : 0;
        message += "{\"range\":{\"start\":{\"line\":" + std::to_string(line) +
                   ",\"character\":" + std::to_string(column) +
                   "},\"end\":{\"line\":" + std::to_string(line) +
                   ",\"character\":" + std::to_string(column + 1) +
                   "}},\"severity\":1,\"code\":\"" + escaped(diagnostic.code) +
                   "\",\"source\":\"icad\",\"message\":\"" +
                   escaped(diagnostic.message) + "\"}";
    }
    message += "]}}";
    send(output, message);
}

} // namespace

auto run(std::istream& input, std::ostream& output) -> int {
    std::string message;
    bool shutdown = false;
    std::unordered_map<std::string, std::string> documents;
    while (read_message(input, message)) {
        const auto method = string_field(message, "method");
        const auto id = id_field(message);
        if (method == "initialize") {
            send(output, "{\"jsonrpc\":\"2.0\",\"id\":" + id +
                             ",\"result\":{\"capabilities\":{\"textDocumentSync\":1,"
                             "\"completionProvider\":{\"triggerCharacters\":[\" \"]},"
                             "\"definitionProvider\":true,\"documentFormattingProvider\":true}}}");
        } else if (method == "textDocument/didOpen" || method == "textDocument/didChange") {
            const auto uri = string_field(message, "uri");
            const auto source = string_field(message, "text");
            documents[uri] = source;
            publish_diagnostics(output, uri, source);
        } else if (method == "textDocument/didClose") {
            const auto uri = string_field(message, "uri");
            documents.erase(uri);
            send(output, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
                         "\"params\":{\"uri\":\"" + escaped(uri) + "\",\"diagnostics\":[]}}");
        } else if (method == "textDocument/completion") {
            send(output, "{\"jsonrpc\":\"2.0\",\"id\":" + id +
                         ",\"result\":" + completion_items() + "}");
        } else if (method == "textDocument/definition") {
            const auto uri = string_field(message, "uri");
            const auto document = documents.find(uri);
            const auto position = message.find("\"position\"");
            const auto line = number_field(message, "line", position);
            const auto character = number_field(message, "character", position);
            DefinitionLocation definition;
            if (document != documents.end())
                definition = definition_of(document->second,
                                           word_at(document->second, line, character));
            if (!definition.found) {
                send(output, "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":null}");
            } else {
                send(output, "{\"jsonrpc\":\"2.0\",\"id\":" + id +
                             ",\"result\":{\"uri\":\"" + escaped(uri) +
                             "\",\"range\":{\"start\":{\"line\":" +
                             std::to_string(definition.line) + ",\"character\":" +
                             std::to_string(definition.column) + "},\"end\":{\"line\":" +
                             std::to_string(definition.line) + ",\"character\":" +
                             std::to_string(definition.column + 1) + "}}}}");
            }
        } else if (method == "textDocument/formatting") {
            const auto uri = string_field(message, "uri");
            const auto document = documents.find(uri);
            if (document == documents.end()) {
                send(output, "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":[]}");
            } else {
                const auto replacement = formatted(document->second);
                send(output, "{\"jsonrpc\":\"2.0\",\"id\":" + id +
                             ",\"result\":[{\"range\":{\"start\":{\"line\":0,\"character\":0},"
                             "\"end\":{\"line\":1000000,\"character\":0}},\"newText\":\"" +
                             escaped(replacement) + "\"}]}");
            }
        } else if (method == "shutdown") {
            shutdown = true;
            send(output, "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":null}");
        } else if (method == "exit") {
            return shutdown ? 0 : 1;
        } else if (!id.empty()) {
            send(output, "{\"jsonrpc\":\"2.0\",\"id\":" + id +
                             ",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}");
        }
    }
    return shutdown ? 0 : 1;
}

} // namespace icad::lsp
