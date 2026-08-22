#include "icad/compiler/language.hpp"
#include "icad/compiler/lexer/lexer.hpp"
#include "icad/compiler/parser/parser.hpp"

#include <iostream>
#include <string_view>

namespace {

auto require(bool condition, std::string_view message) -> bool {
    if (!condition) {
        std::cerr << "language test failure: " << message << '\n';
    }
    return condition;
}

[[nodiscard]] auto parse_source(std::string_view source) -> icad::compiler::ParseResult {
    const auto lexed = icad::compiler::lex(source);
    if (!lexed.ok()) {
        return {};
    }
    return icad::compiler::parse(lexed.tokens);
}

} // namespace

auto main() -> int {
    using icad::compiler::ast::RequirementKind;

    bool passed = true;
    passed &= require(icad::compiler::language::version == "1.0",
                      "production language version is explicit");
    passed &= require(
        icad::compiler::language::supports_capability("CAPABILITY_NEGOTIATION"),
        "capability negotiation advertises itself");
    passed &= require(!icad::compiler::language::supports_capability("MULTI_SHAPE_SKETCH"),
                      "proposed multi-shape syntax is not advertised");

    const auto supported = parse_source(
        "# requirements are a source header\n"
        "REQUIRES ICAD 1.0\n"
        "REQUIRES CAPABILITY BODY_HISTORY\n"
        "PROJECT requirements\n"
        "UNITS mm\n");
    passed &= require(supported.ok(), "supported requirements compile through the parser");
    passed &= require(supported.program.requirements.size() == 2,
                      "supported requirements are retained in the AST");
    if (supported.program.requirements.size() == 2) {
        passed &= require(supported.program.requirements[0].kind ==
                                  RequirementKind::language_version &&
                              supported.program.requirements[0].version_major == 1 &&
                              supported.program.requirements[0].version_minor == 0,
                          "language requirement retains its parsed version");
        passed &= require(supported.program.requirements[1].kind ==
                                  RequirementKind::capability &&
                              supported.program.requirements[1].capability == "BODY_HISTORY",
                          "capability requirement retains its stable name");
    }

    const auto compatible_older =
        parse_source("REQUIRES ICAD 0.9\nPROJECT older\nUNITS mm\n");
    passed &= require(compatible_older.ok(), "an older language contract remains compatible");

    const auto future = parse_source(
        "REQUIRES ICAD 2.0\n"
        "PART future_syntax\n"
        "END\n");
    passed &= require(!future.ok() && future.diagnostics.size() == 1 &&
                          future.diagnostics.front().code == "ICAD-C0002",
                      "future version fails before proposed syntax is parsed");

    const auto unsupported = parse_source(
        "REQUIRES CAPABILITY MULTI_SHAPE_SKETCH\n"
        "PROJECT unsupported\n"
        "UNITS mm\n");
    passed &= require(!unsupported.ok() && unsupported.diagnostics.size() == 1 &&
                          unsupported.diagnostics.front().code == "ICAD-C0003",
                      "unimplemented capability has one preflight diagnostic");

    const auto duplicate = parse_source(
        "REQUIRES CAPABILITY BODY_HISTORY\n"
        "REQUIRES CAPABILITY BODY_HISTORY\n"
        "PROJECT duplicate\n"
        "UNITS mm\n");
    passed &= require(!duplicate.ok() && duplicate.diagnostics.size() == 1 &&
                          duplicate.diagnostics.front().code == "ICAD-C0004",
                      "duplicate capability requirement is rejected");

    const auto late = parse_source(
        "PROJECT late\n"
        "REQUIRES ICAD 1.0\n"
        "UNITS mm\n");
    passed &= require(!late.ok() && late.diagnostics.size() == 1 &&
                          late.diagnostics.front().code == "ICAD-C0005",
                      "requirements after declarations are rejected");

    const auto malformed = parse_source(
        "REQUIRES ICAD version_one\n"
        "PROJECT malformed\n"
        "UNITS mm\n");
    passed &= require(!malformed.ok() && malformed.diagnostics.size() == 1 &&
                          malformed.diagnostics.front().code == "ICAD-C0001",
                      "malformed requirement has a stable diagnostic");

    return passed ? 0 : 1;
}
