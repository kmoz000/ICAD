#include "icad/compiler/lexer/lexer.hpp"
#include "icad/compiler/parser/parser.hpp"

#include <iostream>

auto main() -> int {
    const auto lexed =
        icad::compiler::lex("# retained lexer comment\n"
                            "PROJECT parser_test // parser ignores retained comments\n"
                            "UNITS mm\n"
                            "PARAMETER span 2 m\n"
                            "PARAMETER inset span / 4 + 5 mm\n"
                            "PROFILE rounded\nSTART 0 mm 0 mm\nLINE 10 mm 0 mm\n"
                            "ARC 10 mm 10 mm CENTER 5 mm 5 mm CCW\nLINE 0 mm 10 mm\nCLOSE\nEND\n"
                            "BODY bridge\n"
                            "FEATURE deck\n"
                            "TYPE BOX\n"
                            "WIDTH 2000 mm\nDEPTH 500 mm\nHEIGHT 50 mm\n"
                            "END\nEND\n"
                            "MATERIAL concrete CONCRETE\n"
                            "SCENE reveal\nDURATION 2 s\nFPS 30\nBACKGROUND STUDIO\n"
                            "TRACK orbit CAMERA main\n"
                            "KEYFRAME 0 s POSITION 0 mm 0 mm 0 mm ROTATION 0 deg 0 deg 0 deg\n"
                            "KEYFRAME 2 s POSITION 0 mm 0 mm 0 mm ROTATION 0 deg 0 deg 90 deg\n"
                            "END\nEND\n");
    const auto parsed = icad::compiler::parse(lexed.tokens);
    if (!lexed.ok() || !parsed.ok()) {
        std::cerr << "parser rejected valid nested source\n";
        return 1;
    }
    if (parsed.program.project_name != "parser_test" || parsed.program.parameters.size() != 2 ||
        parsed.program.parameters[1].expression.references.size() != 1 ||
        parsed.program.parameters[1].expression.references.front() != "span" ||
        parsed.program.parameters[1].expression.source != "span / 4 + 5 mm" ||
        parsed.program.profiles.size() != 1 ||
        parsed.program.profiles.front().mode != icad::compiler::ast::ProfileMode::path ||
        parsed.program.profiles.front().path_segments.size() != 3 ||
        parsed.program.bodies.size() != 1 || parsed.program.bodies.front().features.size() != 1 ||
        parsed.program.bodies.front().features.front().properties.size() != 3 ||
        parsed.program.materials.size() != 1 || parsed.program.scenes.size() != 1 ||
        parsed.program.scenes.front().tracks.front().keyframes.size() != 2) {
        std::cerr << "parser produced an unexpected AST shape\n";
        return 1;
    }

    const auto incomplete_tokens =
        icad::compiler::lex("PROJECT broken\nUNITS mm\nBODY bridge\nFEATURE deck\nTYPE BOX\n");
    const auto incomplete = icad::compiler::parse(incomplete_tokens.tokens);
    if (incomplete.ok()) {
        std::cerr << "parser accepted unclosed blocks\n";
        return 1;
    }
    const auto open_profile = icad::compiler::parse(
        icad::compiler::lex("PROJECT open\nUNITS mm\nPROFILE p\nSTART 0 mm 0 mm\n"
                            "LINE 1 mm 0 mm\nEND\n")
            .tokens);
    if (open_profile.ok()) {
        std::cerr << "parser accepted a path profile without CLOSE\n";
        return 1;
    }
    const auto proposed_part_tokens =
        icad::compiler::lex("PROJECT future\nUNITS mm\nPART bracket\nEND\n");
    const auto proposed_part = icad::compiler::parse(proposed_part_tokens.tokens);
    if (!proposed_part_tokens.ok() || proposed_part.ok()) {
        std::cerr << "lexer/parser boundary incorrectly enabled proposed PART syntax\n";
        return 1;
    }
    const auto malformed_expression = icad::compiler::parse(
        icad::compiler::lex("PROJECT bad_expression\nUNITS mm\nPARAMETER width (2 mm\n")
            .tokens);
    if (malformed_expression.ok() || malformed_expression.diagnostics.front().code !=
                                               "ICAD-E0001") {
        std::cerr << "parser accepted a malformed scalar expression\n";
        return 1;
    }
    return 0;
}
