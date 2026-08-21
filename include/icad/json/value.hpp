#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace icad::json {

class Value {
  public:
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value, std::less<>>;

    Value() = default;
    Value(std::nullptr_t);
    Value(bool value);
    Value(double value);
    Value(std::string value);
    Value(const char* value);
    Value(Array value);
    Value(Object value);

    [[nodiscard]] auto is_null() const -> bool;
    [[nodiscard]] auto boolean() const -> const bool*;
    [[nodiscard]] auto number() const -> const double*;
    [[nodiscard]] auto string() const -> const std::string*;
    [[nodiscard]] auto array() const -> const Array*;
    [[nodiscard]] auto object() const -> const Object*;
    [[nodiscard]] auto array() -> Array*;
    [[nodiscard]] auto object() -> Object*;
    [[nodiscard]] auto find(std::string_view key) const -> const Value*;

  private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data_{nullptr};
};

struct ParseResult {
    std::optional<Value> value;
    std::string error;
    std::size_t offset{};

    [[nodiscard]] auto ok() const -> bool { return value.has_value(); }
};

[[nodiscard]] auto parse(std::string_view source) -> ParseResult;
[[nodiscard]] auto serialize(const Value& value) -> std::string;
[[nodiscard]] auto quote(std::string_view value) -> std::string;

} // namespace icad::json
