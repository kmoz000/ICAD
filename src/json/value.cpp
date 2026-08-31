#include "icad/json/value.hpp"

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <locale.h>
#include <utility>

namespace icad::json {

Value::Value(std::nullptr_t) : data_{nullptr} {}
Value::Value(bool value) : data_{value} {}
Value::Value(double value) : data_{value} {}
Value::Value(std::string value) : data_{std::move(value)} {}
Value::Value(const char* value) : data_{std::string{value}} {}
Value::Value(Array value) : data_{std::move(value)} {}
Value::Value(Object value) : data_{std::move(value)} {}

auto Value::is_null() const -> bool { return std::holds_alternative<std::nullptr_t>(data_); }
auto Value::boolean() const -> const bool* { return std::get_if<bool>(&data_); }
auto Value::number() const -> const double* { return std::get_if<double>(&data_); }
auto Value::string() const -> const std::string* { return std::get_if<std::string>(&data_); }
auto Value::array() const -> const Array* { return std::get_if<Array>(&data_); }
auto Value::object() const -> const Object* { return std::get_if<Object>(&data_); }
auto Value::array() -> Array* { return std::get_if<Array>(&data_); }
auto Value::object() -> Object* { return std::get_if<Object>(&data_); }

auto Value::find(std::string_view key) const -> const Value* {
    const auto* values = object();
    if (values == nullptr) {
        return nullptr;
    }
    const auto iterator = values->find(key);
    return iterator == values->end() ? nullptr : &iterator->second;
}

namespace {

[[nodiscard]] auto parse_decimal(std::string_view text, double& value) -> bool {
    constexpr std::size_t stack_capacity = 128;
    char stack[stack_capacity]{};
    std::string storage;
    char* buffer = stack;
    if (text.size() >= stack_capacity) {
        storage.assign(text);
        buffer = storage.data();
    } else {
        std::copy(text.begin(), text.end(), stack);
        stack[text.size()] = '\0';
    }
    char* end = nullptr;
    errno = 0;
#if defined(_WIN32)
    static _locale_t c_locale = _create_locale(LC_NUMERIC, "C");
    value = _strtod_l(buffer, &end, c_locale);
#else
    static locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", nullptr);
    value = strtod_l(buffer, &end, c_locale);
#endif
    return errno != ERANGE && end == buffer + text.size() && std::isfinite(value);
}

class Parser {
  public:
    explicit Parser(std::string_view source) : source_{source} {}

    [[nodiscard]] auto run() -> ParseResult {
        skip_space();
        auto value = parse_value();
        if (!value) {
            return {std::nullopt, error_, position_};
        }
        skip_space();
        if (position_ != source_.size()) {
            fail("unexpected characters after JSON value");
            return {std::nullopt, error_, position_};
        }
        return {std::move(value), {}, position_};
    }

  private:
    auto skip_space() -> void {
        while (position_ < source_.size() &&
               (source_[position_] == ' ' || source_[position_] == '\t' ||
                source_[position_] == '\r' || source_[position_] == '\n')) {
            ++position_;
        }
    }

    auto fail(std::string message) -> void {
        if (error_.empty()) {
            error_ = std::move(message);
        }
    }

    [[nodiscard]] auto consume(std::string_view text) -> bool {
        if (source_.substr(position_, text.size()) != text) {
            return false;
        }
        position_ += text.size();
        return true;
    }

    [[nodiscard]] auto parse_value() -> std::optional<Value> {
        skip_space();
        if (position_ >= source_.size()) {
            fail("expected a JSON value");
            return std::nullopt;
        }
        switch (source_[position_]) {
        case 'n':
            if (consume("null")) return Value{nullptr};
            break;
        case 't':
            if (consume("true")) return Value{true};
            break;
        case 'f':
            if (consume("false")) return Value{false};
            break;
        case '"': return parse_string_value();
        case '[': return parse_array();
        case '{': return parse_object();
        default:
            if (source_[position_] == '-' ||
                (source_[position_] >= '0' && source_[position_] <= '9')) {
                return parse_number();
            }
            break;
        }
        fail("invalid JSON value");
        return std::nullopt;
    }

    [[nodiscard]] auto parse_hex4(std::uint32_t& value) -> bool {
        if (position_ + 4 > source_.size()) {
            return false;
        }
        value = 0;
        for (std::size_t index = 0; index < 4; ++index) {
            const char digit = source_[position_++];
            value <<= 4U;
            if (digit >= '0' && digit <= '9') {
                value += static_cast<std::uint32_t>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
                value += static_cast<std::uint32_t>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
                value += static_cast<std::uint32_t>(digit - 'A' + 10);
            } else {
                return false;
            }
        }
        return true;
    }

    static auto append_utf8(std::string& output, std::uint32_t codepoint) -> void {
        if (codepoint <= 0x7fU) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ffU) {
            output.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        } else if (codepoint <= 0xffffU) {
            output.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        } else {
            output.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        }
    }

    [[nodiscard]] auto parse_unicode(std::string& output) -> bool {
        std::uint32_t codepoint = 0;
        if (!parse_hex4(codepoint)) {
            return false;
        }
        if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
            if (!consume("\\u")) {
                return false;
            }
            std::uint32_t low = 0;
            if (!parse_hex4(low) || low < 0xdc00U || low > 0xdfffU) {
                return false;
            }
            codepoint = 0x10000U + ((codepoint - 0xd800U) << 10U) + (low - 0xdc00U);
        } else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
            return false;
        }
        append_utf8(output, codepoint);
        return true;
    }

    [[nodiscard]] auto parse_string() -> std::optional<std::string> {
        if (source_[position_] != '"') {
            return std::nullopt;
        }
        ++position_;
        std::string result;
        while (position_ < source_.size()) {
            const char character = source_[position_++];
            if (character == '"') {
                return result;
            }
            if (static_cast<unsigned char>(character) < 0x20U) {
                fail("control character in JSON string");
                return std::nullopt;
            }
            if (character != '\\') {
                result.push_back(character);
                continue;
            }
            if (position_ >= source_.size()) {
                break;
            }
            const char escape = source_[position_++];
            switch (escape) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u':
                if (!parse_unicode(result)) {
                    fail("invalid Unicode escape in JSON string");
                    return std::nullopt;
                }
                break;
            default:
                fail("invalid escape in JSON string");
                return std::nullopt;
            }
        }
        fail("unterminated JSON string");
        return std::nullopt;
    }

    [[nodiscard]] auto parse_string_value() -> std::optional<Value> {
        auto string = parse_string();
        return string ? std::optional<Value>{Value{std::move(*string)}} : std::nullopt;
    }

    [[nodiscard]] auto parse_number() -> std::optional<Value> {
        const std::size_t start = position_;
        if (source_[position_] == '-') ++position_;
        if (position_ >= source_.size()) {
            fail("invalid JSON number");
            return std::nullopt;
        }
        if (source_[position_] == '0') {
            ++position_;
        } else if (source_[position_] >= '1' && source_[position_] <= '9') {
            while (position_ < source_.size() && source_[position_] >= '0' &&
                   source_[position_] <= '9') ++position_;
        } else {
            fail("invalid JSON number");
            return std::nullopt;
        }
        if (position_ < source_.size() && source_[position_] == '.') {
            ++position_;
            const auto fraction = position_;
            while (position_ < source_.size() && source_[position_] >= '0' &&
                   source_[position_] <= '9') ++position_;
            if (fraction == position_) {
                fail("invalid JSON number fraction");
                return std::nullopt;
            }
        }
        if (position_ < source_.size() &&
            (source_[position_] == 'e' || source_[position_] == 'E')) {
            ++position_;
            if (position_ < source_.size() &&
                (source_[position_] == '+' || source_[position_] == '-')) ++position_;
            const auto exponent = position_;
            while (position_ < source_.size() && source_[position_] >= '0' &&
                   source_[position_] <= '9') ++position_;
            if (exponent == position_) {
                fail("invalid JSON number exponent");
                return std::nullopt;
            }
        }
        double value = 0.0;
        if (!parse_decimal(source_.substr(start, position_ - start), value)) {
            fail("JSON number is out of range");
            return std::nullopt;
        }
        return Value{value};
    }

    [[nodiscard]] auto parse_array() -> std::optional<Value> {
        ++position_;
        Value::Array values;
        skip_space();
        if (position_ < source_.size() && source_[position_] == ']') {
            ++position_;
            return Value{std::move(values)};
        }
        while (true) {
            auto value = parse_value();
            if (!value) return std::nullopt;
            values.push_back(std::move(*value));
            skip_space();
            if (position_ >= source_.size()) break;
            if (source_[position_] == ']') {
                ++position_;
                return Value{std::move(values)};
            }
            if (source_[position_] != ',') break;
            ++position_;
        }
        fail("expected ',' or ']' in JSON array");
        return std::nullopt;
    }

    [[nodiscard]] auto parse_object() -> std::optional<Value> {
        ++position_;
        Value::Object values;
        skip_space();
        if (position_ < source_.size() && source_[position_] == '}') {
            ++position_;
            return Value{std::move(values)};
        }
        while (true) {
            skip_space();
            if (position_ >= source_.size() || source_[position_] != '"') break;
            auto key = parse_string();
            if (!key) return std::nullopt;
            skip_space();
            if (position_ >= source_.size() || source_[position_] != ':') {
                fail("expected ':' in JSON object");
                return std::nullopt;
            }
            ++position_;
            auto value = parse_value();
            if (!value) return std::nullopt;
            if (!values.emplace(std::move(*key), std::move(*value)).second) {
                fail("duplicate key in JSON object");
                return std::nullopt;
            }
            skip_space();
            if (position_ >= source_.size()) break;
            if (source_[position_] == '}') {
                ++position_;
                return Value{std::move(values)};
            }
            if (source_[position_] != ',') break;
            ++position_;
        }
        fail("expected object member or '}' in JSON object");
        return std::nullopt;
    }

    std::string_view source_;
    std::size_t position_{};
    std::string error_;
};

auto append_serialized(const Value& value, std::string& output) -> void {
    if (value.is_null()) {
        output += "null";
    } else if (const auto* boolean = value.boolean()) {
        output += *boolean ? "true" : "false";
    } else if (const auto* number = value.number()) {
        char buffer[64]{};
        const auto conversion = std::to_chars(buffer, buffer + sizeof(buffer), *number,
                                              std::chars_format::general,
                                              std::numeric_limits<double>::max_digits10);
        output.append(buffer, conversion.ptr);
    } else if (const auto* string = value.string()) {
        output += quote(*string);
    } else if (const auto* array = value.array()) {
        output.push_back('[');
        for (std::size_t index = 0; index < array->size(); ++index) {
            if (index != 0) output.push_back(',');
            append_serialized((*array)[index], output);
        }
        output.push_back(']');
    } else if (const auto* object = value.object()) {
        output.push_back('{');
        std::size_t index = 0;
        for (const auto& [key, child] : *object) {
            if (index++ != 0) output.push_back(',');
            output += quote(key);
            output.push_back(':');
            append_serialized(child, output);
        }
        output.push_back('}');
    }
}

} // namespace

auto parse(std::string_view source) -> ParseResult { return Parser{source}.run(); }

auto serialize(const Value& value) -> std::string {
    std::string result;
    append_serialized(value, result);
    return result;
}

auto quote(std::string_view value) -> std::string {
    constexpr char hex[] = "0123456789abcdef";
    std::string result{"\""};
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (byte < 0x20U) {
                result += "\\u00";
                result.push_back(hex[(byte >> 4U) & 0x0fU]);
                result.push_back(hex[byte & 0x0fU]);
            } else {
                result.push_back(character);
            }
            break;
        }
    }
    result.push_back('"');
    return result;
}

} // namespace icad::json
