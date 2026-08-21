#include "icad/compiler/parser/parser.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace icad::compiler {
namespace {

using Line = std::vector<Token>;
using Lines = std::vector<Line>;

auto add_error(ParseResult& result, std::string code, std::string message, SourceLocation location)
    -> void {
    result.diagnostics.push_back(
        Diagnostic{DiagnosticSeverity::error, std::move(code), std::move(message), location});
}

[[nodiscard]] auto lines_from(const std::vector<Token>& tokens) -> Lines {
    Lines lines;
    Line current;
    for (const auto& token : tokens) {
        if (token.kind == TokenKind::end_of_file) {
            break;
        }
        if (token.kind == TokenKind::newline) {
            if (!current.empty()) {
                lines.push_back(std::move(current));
                current = {};
            }
            continue;
        }
        current.push_back(token);
    }
    if (!current.empty()) {
        lines.push_back(std::move(current));
    }
    return lines;
}

[[nodiscard]] auto keyword(const Line& line) -> std::string_view {
    return line.empty() ? std::string_view{} : std::string_view{line.front().lexeme};
}

[[nodiscard]] auto valid_identifier(const Token& token) -> bool {
    return token.kind == TokenKind::identifier;
}

[[nodiscard]] auto parse_number(const Token& token, double& value) -> bool {
    if (token.kind != TokenKind::number) {
        return false;
    }
    const char* begin = token.lexeme.data();
    const char* end = begin + token.lexeme.size();
    const auto conversion = std::from_chars(begin, end, value);
    return conversion.ec == std::errc{} && conversion.ptr == end;
}

[[nodiscard]] auto parse_quantity(const Line& line, std::size_t value_index, ParseResult& result)
    -> ast::QuantityLiteral {
    ast::QuantityLiteral quantity;
    if (value_index + 1 >= line.size()) {
        add_error(result, "ICAD-P0002", "expected NUMBER UNIT quantity", line.front().location);
        return quantity;
    }
    quantity.location = line[value_index].location;
    if (!parse_number(line[value_index], quantity.value)) {
        add_error(result, "ICAD-P0002", "expected a numeric quantity", line[value_index].location);
    }
    if (!valid_identifier(line[value_index + 1])) {
        add_error(result, "ICAD-P0003", "expected a unit after the quantity",
                  line[value_index + 1].location);
    } else {
        quantity.unit = line[value_index + 1].lexeme;
    }
    return quantity;
}

[[nodiscard]] auto parse_value(const Line& line, std::size_t& index, ParseResult& result)
    -> ast::ValueDecl {
    ast::ValueDecl value;
    if (index >= line.size()) {
        add_error(result, "ICAD-P0020", "expected a quantity or parameter reference",
                  line.front().location);
        return value;
    }
    value.location = line[index].location;
    if (line[index].kind == TokenKind::number) {
        value.literal = parse_quantity(line, index, result);
        index += std::min<std::size_t>(2, line.size() - index);
        return value;
    }
    if (valid_identifier(line[index])) {
        value.parameter_reference = line[index].lexeme;
        ++index;
        return value;
    }
    add_error(result, "ICAD-P0020", "expected a quantity or parameter reference",
              line[index].location);
    ++index;
    return value;
}

[[nodiscard]] auto parse_feature(const Lines& lines, std::size_t& index, ParseResult& result)
    -> ast::FeatureDecl {
    const Line& declaration = lines[index];
    ast::FeatureDecl feature;
    feature.location = declaration.front().location;
    if (declaration.size() != 2 || !valid_identifier(declaration[1])) {
        add_error(result, "ICAD-P0001", "FEATURE expects exactly one identifier",
                  declaration.front().location);
    } else {
        feature.name = declaration[1].lexeme;
    }
    ++index;

    bool closed = false;
    while (index < lines.size()) {
        const Line& line = lines[index];
        if (keyword(line) == "END") {
            if (line.size() != 1) {
                add_error(result, "ICAD-P0001", "END does not accept arguments",
                          line.front().location);
            }
            ++index;
            closed = true;
            break;
        }
        if (keyword(line) == "TYPE") {
            if (line.size() != 2 || !valid_identifier(line[1])) {
                add_error(result, "ICAD-P0001", "TYPE expects exactly one identifier",
                          line.front().location);
            } else if (!feature.type.empty()) {
                add_error(result, "ICAD-P0004", "feature TYPE is declared more than once",
                          line.front().location);
            } else {
                feature.type = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "PROFILE") {
            if (line.size() != 2 || !valid_identifier(line[1]) || !feature.profile.empty()) {
                add_error(result, "ICAD-P0017",
                          "feature PROFILE expects one profile and may appear once",
                          line.front().location);
            } else {
                feature.profile = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "TARGET_PROFILE") {
            if (line.size() != 2 || !valid_identifier(line[1]) ||
                !feature.target_profile.empty()) {
                add_error(result, "ICAD-P0025",
                          "TARGET_PROFILE expects one profile and may appear once",
                          line.front().location);
            } else {
                feature.target_profile = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "PATH") {
            const bool valid = line.size() >= 3 && feature.path_points.empty() &&
                               std::ranges::all_of(line | std::views::drop(1), valid_identifier);
            if (!valid) {
                add_error(result, "ICAD-P0025",
                          "PATH expects at least two POINT3 identifiers and may appear once",
                          line.front().location);
            } else {
                for (std::size_t point = 1; point < line.size(); ++point)
                    feature.path_points.push_back(line[point].lexeme);
            }
            ++index;
            continue;
        }
        if (keyword(line) == "OPERATION") {
            if (line.size() != 2 || !valid_identifier(line[1]) || !feature.operation.empty()) {
                add_error(result, "ICAD-P0023",
                          "feature OPERATION expects NEW, UNION, CUT, or INTERSECT and may appear "
                          "once",
                          line.front().location);
            } else {
                feature.operation = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "SELECT") {
            if (line.size() != 4 || line[1].lexeme != "EDGE" ||
                line[2].lexeme != "NEAREST" || !valid_identifier(line[3]) ||
                !feature.selected_edge_point.empty()) {
                add_error(result, "ICAD-P0024",
                          "SELECT expects EDGE NEAREST POINT and may appear once",
                          line.front().location);
            } else {
                feature.selected_edge_point = line[3].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "DIRECTION") {
            if (line.size() != 2 || !valid_identifier(line[1]) || !feature.direction.empty()) {
                add_error(result, "ICAD-P0024",
                          "DIRECTION expects one VECTOR and may appear once",
                          line.front().location);
            } else {
                feature.direction = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "PLANE") {
            if (line.size() != 4 || !valid_identifier(line[1]) || line[2].lexeme != "NORMAL" ||
                !valid_identifier(line[3]) || !feature.plane_point.empty()) {
                add_error(result, "ICAD-P0024",
                          "PLANE expects POINT NORMAL VECTOR and may appear once",
                          line.front().location);
            } else {
                feature.plane_point = line[1].lexeme;
                feature.plane_normal = line[3].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "COUNT") {
            double count = 0.0;
            if (line.size() != 2 || feature.has_count || !parse_number(line[1], count) ||
                count < 0.0 || count > 1'000'000.0 || std::floor(count) != count) {
                add_error(result, "ICAD-P0024",
                          "COUNT expects one non-negative integer and may appear once",
                          line.front().location);
            } else {
                feature.count = static_cast<std::size_t>(count);
                feature.has_count = true;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "FEATURE" || keyword(line) == "BODY") {
            add_error(result, "ICAD-P0005", "feature block is missing END", feature.location);
            break;
        }
        if (!valid_identifier(line[0]) || (line.size() != 2 && line.size() != 3)) {
            add_error(result, "ICAD-P0001",
                      "feature property expects NAME NUMBER UNIT or NAME PARAMETER",
                      line.front().location);
            ++index;
            continue;
        }
        if (line.size() == 2 && valid_identifier(line[1])) {
            feature.properties.push_back(
                ast::PropertyDecl{line[0].lexeme, {}, line[1].lexeme, line.front().location});
        } else {
            feature.properties.push_back(ast::PropertyDecl{
                line[0].lexeme, parse_quantity(line, 1, result), {}, line.front().location});
        }
        ++index;
    }

    if (!closed && index >= lines.size()) {
        add_error(result, "ICAD-P0005", "feature block is missing END", feature.location);
    }
    return feature;
}

[[nodiscard]] auto parse_profile(const Lines& lines, std::size_t& index, ParseResult& result)
    -> ast::ProfileDecl {
    const Line& declaration = lines[index];
    ast::ProfileDecl profile;
    profile.location = declaration.front().location;
    if (declaration.size() != 2 || !valid_identifier(declaration[1])) {
        add_error(result, "ICAD-P0018", "PROFILE expects exactly one identifier",
                  declaration.front().location);
    } else {
        profile.name = declaration[1].lexeme;
    }
    ++index;

    bool closed = false;
    const auto select_mode = [&](ast::ProfileMode mode, const Line& line) {
        if (profile.mode == ast::ProfileMode::unset) {
            profile.mode = mode;
            return true;
        }
        if (profile.mode != mode) {
            add_error(result, "ICAD-P0018", "PROFILE cannot mix POINT, path, and CIRCLE syntax",
                      line.front().location);
            return false;
        }
        return true;
    };
    while (index < lines.size()) {
        const Line& line = lines[index];
        if (keyword(line) == "END") {
            if (line.size() != 1) {
                add_error(result, "ICAD-P0018", "END does not accept profile arguments",
                          line.front().location);
            }
            ++index;
            closed = true;
            break;
        }
        if (keyword(line) == "POINT") {
            if (line.size() != 5 || !select_mode(ast::ProfileMode::points, line)) {
                add_error(result, "ICAD-P0018", "POINT expects X UNIT Y UNIT",
                          line.front().location);
                ++index;
                continue;
            }
            profile.points.push_back(ast::Point2Decl{parse_quantity(line, 1, result),
                                                     parse_quantity(line, 3, result),
                                                     line.front().location});
            ++index;
            continue;
        }
        if (keyword(line) == "START") {
            if (line.size() != 5 || profile.mode != ast::ProfileMode::unset) {
                add_error(result, "ICAD-P0018",
                          "START expects X UNIT Y UNIT and must begin a path profile",
                          line.front().location);
            } else {
                profile.mode = ast::ProfileMode::path;
                profile.path_start =
                    ast::Point2Decl{parse_quantity(line, 1, result),
                                    parse_quantity(line, 3, result), line.front().location};
            }
            ++index;
            continue;
        }
        if (keyword(line) == "LINE") {
            if (line.size() != 5 || profile.mode != ast::ProfileMode::path || profile.path_closed) {
                add_error(result, "ICAD-P0018",
                          "LINE expects X UNIT Y UNIT inside an open path profile",
                          line.front().location);
            } else {
                profile.path_segments.push_back(
                    ast::PathSegmentDecl{ast::PathSegmentKind::line,
                                         {parse_quantity(line, 1, result),
                                          parse_quantity(line, 3, result), line.front().location},
                                         {},
                                         true,
                                         line.front().location});
            }
            ++index;
            continue;
        }
        if (keyword(line) == "ARC") {
            const bool layout = line.size() == 11 && line[5].lexeme == "CENTER" &&
                                (line[10].lexeme == "CW" || line[10].lexeme == "CCW");
            if (!layout || profile.mode != ast::ProfileMode::path || profile.path_closed) {
                add_error(
                    result, "ICAD-P0018",
                    "ARC expects X UNIT Y UNIT CENTER X UNIT Y UNIT CW|CCW inside an open path",
                    line.front().location);
            } else {
                profile.path_segments.push_back(
                    ast::PathSegmentDecl{ast::PathSegmentKind::circular_arc,
                                         {parse_quantity(line, 1, result),
                                          parse_quantity(line, 3, result), line.front().location},
                                         {parse_quantity(line, 6, result),
                                          parse_quantity(line, 8, result), line.front().location},
                                         line[10].lexeme == "CCW",
                                         line.front().location});
            }
            ++index;
            continue;
        }
        if (keyword(line) == "CLOSE") {
            if (line.size() != 1 || profile.mode != ast::ProfileMode::path || profile.path_closed) {
                add_error(result, "ICAD-P0018", "CLOSE must end one open path profile",
                          line.front().location);
            } else {
                profile.path_closed = true;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "CIRCLE") {
            if (line.size() != 7 || !select_mode(ast::ProfileMode::circle, line) ||
                profile.circle_radius.unit.size() != 0) {
                add_error(result, "ICAD-P0018",
                          "CIRCLE expects CENTER_X UNIT CENTER_Y UNIT RADIUS UNIT and must be the "
                          "only profile statement",
                          line.front().location);
            } else {
                profile.circle_center =
                    ast::Point2Decl{parse_quantity(line, 1, result),
                                    parse_quantity(line, 3, result), line.front().location};
                profile.circle_radius = parse_quantity(line, 5, result);
            }
            ++index;
            continue;
        }
        add_error(result, "ICAD-P0018",
                  "PROFILE accepts POINT, START, LINE, ARC, CLOSE, or CIRCLE statements",
                  line.front().location);
        ++index;
    }
    if (!closed) {
        add_error(result, "ICAD-P0018", "profile block is missing END", profile.location);
    }
    if (profile.mode == ast::ProfileMode::path && !profile.path_closed) {
        add_error(result, "ICAD-P0018", "path profile requires CLOSE before END", profile.location);
    }
    return profile;
}

[[nodiscard]] auto parse_sketch(const Lines& lines, std::size_t& index, ParseResult& result)
    -> ast::SketchDecl {
    const Line& declaration = lines[index];
    ast::SketchDecl sketch;
    sketch.location = declaration.front().location;
    if (declaration.size() != 2 || !valid_identifier(declaration[1])) {
        add_error(result, "ICAD-P0026", "SKETCH expects exactly one identifier",
                  declaration.front().location);
    } else {
        sketch.name = declaration[1].lexeme;
    }
    ++index;

    bool closed = false;
    while (index < lines.size()) {
        const Line& line = lines[index];
        if (keyword(line) == "END") {
            if (line.size() != 1) {
                add_error(result, "ICAD-P0026", "END does not accept sketch arguments",
                          line.front().location);
            }
            ++index;
            closed = true;
            break;
        }
        if (keyword(line) == "POINT") {
            if (line.size() < 4 || !valid_identifier(line[1])) {
                add_error(result, "ICAD-P0026", "sketch POINT expects NAME X Y [FIXED]",
                          line.front().location);
                ++index;
                continue;
            }
            std::size_t cursor = 2;
            ast::SketchPointDecl point_decl;
            point_decl.name = line[1].lexeme;
            point_decl.x = parse_value(line, cursor, result);
            point_decl.y = parse_value(line, cursor, result);
            point_decl.location = line.front().location;
            if (cursor < line.size() && line[cursor].lexeme == "FIXED") {
                point_decl.fixed = true;
                ++cursor;
            }
            if (cursor != line.size()) {
                add_error(result, "ICAD-P0026", "sketch POINT has unexpected trailing values",
                          line[cursor].location);
            }
            sketch.points.push_back(std::move(point_decl));
            ++index;
            continue;
        }
        if (keyword(line) == "CONSTRAINT") {
            const bool prefix = line.size() >= 5 && valid_identifier(line[1]) &&
                                valid_identifier(line[2]) && valid_identifier(line[3]) &&
                                valid_identifier(line[4]);
            if (!prefix) {
                add_error(result, "ICAD-P0026",
                          "sketch CONSTRAINT expects NAME KIND and point references",
                          line.front().location);
                ++index;
                continue;
            }
            ast::SketchConstraintDecl constraint;
            constraint.name = line[1].lexeme;
            constraint.kind = line[2].lexeme;
            constraint.location = line.front().location;
            std::size_t cursor = 3;
            const std::size_t reference_count = constraint.kind == "ANGLE" ? 3 : 2;
            for (std::size_t reference = 0;
                 reference < reference_count && cursor < line.size(); ++reference, ++cursor) {
                if (!valid_identifier(line[cursor])) {
                    add_error(result, "ICAD-P0026", "sketch constraint expects point identifiers",
                              line[cursor].location);
                } else {
                    constraint.references.push_back(line[cursor].lexeme);
                }
            }
            if (constraint.references.size() != reference_count) {
                add_error(result, "ICAD-P0026", "sketch constraint has too few point references",
                          line.front().location);
            }
            if (constraint.kind == "DISTANCE" || constraint.kind == "ANGLE")
                constraint.target = parse_value(line, cursor, result);
            if (cursor != line.size()) {
                add_error(result, "ICAD-P0026", "sketch constraint has unexpected trailing values",
                          line[cursor].location);
            }
            sketch.constraints.push_back(std::move(constraint));
            ++index;
            continue;
        }
        add_error(result, "ICAD-P0026", "SKETCH accepts POINT or CONSTRAINT statements",
                  line.front().location);
        ++index;
    }
    if (!closed)
        add_error(result, "ICAD-P0026", "sketch block is missing END", sketch.location);
    return sketch;
}

[[nodiscard]] auto parse_body(const Lines& lines, std::size_t& index, ParseResult& result)
    -> ast::BodyDecl {
    const Line& declaration = lines[index];
    ast::BodyDecl body;
    body.location = declaration.front().location;
    if (declaration.size() != 2 || !valid_identifier(declaration[1])) {
        add_error(result, "ICAD-P0001", "BODY expects exactly one identifier",
                  declaration.front().location);
    } else {
        body.name = declaration[1].lexeme;
    }
    ++index;

    bool closed = false;
    while (index < lines.size()) {
        const Line& line = lines[index];
        if (keyword(line) == "END") {
            if (line.size() != 1) {
                add_error(result, "ICAD-P0001", "END does not accept arguments",
                          line.front().location);
            }
            ++index;
            closed = true;
            break;
        }
        if (keyword(line) == "MATERIAL") {
            if (line.size() != 2 || !valid_identifier(line[1]) || !body.material.empty()) {
                add_error(result, "ICAD-P0013",
                          "BODY MATERIAL expects one material and may appear once",
                          line.front().location);
            } else {
                body.material = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "FEATURE") {
            body.features.push_back(parse_feature(lines, index, result));
            continue;
        }
        add_error(result, "ICAD-P0006", "BODY accepts MATERIAL and FEATURE statements only",
                  line.front().location);
        ++index;
    }

    if (!closed) {
        add_error(result, "ICAD-P0007", "body block is missing END", body.location);
    }
    return body;
}

[[nodiscard]] auto parse_keyframe(const Line& line, ParseResult& result) -> ast::KeyframeDecl {
    ast::KeyframeDecl frame;
    frame.location = line.front().location;
    const bool visibility_layout = line.size() == 5 && keyword(line) == "KEYFRAME" &&
                                   line[3].lexeme == "VISIBLE";
    if (visibility_layout) {
        frame.time = parse_quantity(line, 1, result);
        if (line[4].lexeme != "ON" && line[4].lexeme != "OFF") {
            add_error(result, "ICAD-P0016", "VISIBLE expects ON or OFF", line[4].location);
        }
        frame.visible = line[4].lexeme == "ON";
        frame.visibility_only = true;
        return frame;
    }
    const bool value_layout = line.size() >= 4 && keyword(line) == "KEYFRAME" &&
                              line[3].lexeme == "VALUE";
    if (value_layout) {
        frame.time = parse_quantity(line, 1, result);
        std::size_t cursor = 4;
        frame.joint_value = parse_value(line, cursor, result);
        frame.value_only = true;
        if (cursor != line.size()) {
            add_error(result, "ICAD-P0016", "joint KEYFRAME has unexpected trailing values",
                      line[cursor].location);
        }
        return frame;
    }
    const bool layout = line.size() == 17 && keyword(line) == "KEYFRAME" &&
                        line[3].lexeme == "POSITION" && line[10].lexeme == "ROTATION";
    if (!layout) {
        add_error(result, "ICAD-P0016",
                  "KEYFRAME expects TIME POSITION X Y Z ROTATION X Y Z, TIME VALUE scalar, "
                  "or TIME VISIBLE ON|OFF",
                  line.front().location);
        return frame;
    }
    frame.time = parse_quantity(line, 1, result);
    frame.position_x = parse_quantity(line, 4, result);
    frame.position_y = parse_quantity(line, 6, result);
    frame.position_z = parse_quantity(line, 8, result);
    frame.rotation_x = parse_quantity(line, 11, result);
    frame.rotation_y = parse_quantity(line, 13, result);
    frame.rotation_z = parse_quantity(line, 15, result);
    return frame;
}

[[nodiscard]] auto parse_track(const Lines& lines, std::size_t& index, ParseResult& result)
    -> ast::TrackDecl {
    const Line& declaration = lines[index];
    ast::TrackDecl track;
    track.location = declaration.front().location;
    if (declaration.size() != 4 || !valid_identifier(declaration[1]) ||
        !valid_identifier(declaration[2]) || !valid_identifier(declaration[3])) {
        add_error(result, "ICAD-P0015", "TRACK expects NAME BODY|CAMERA|JOINT TARGET",
                  declaration.front().location);
    } else {
        track.name = declaration[1].lexeme;
        track.target_kind = declaration[2].lexeme;
        track.target = declaration[3].lexeme;
    }
    ++index;

    bool closed = false;
    while (index < lines.size()) {
        const Line& line = lines[index];
        if (keyword(line) == "END") {
            ++index;
            closed = true;
            break;
        }
        if (keyword(line) == "KEYFRAME") {
            track.keyframes.push_back(parse_keyframe(line, result));
        } else if (keyword(line) == "EASING" && line.size() == 2 && valid_identifier(line[1])) {
            track.easing = line[1].lexeme;
        } else {
            add_error(result, "ICAD-P0015", "TRACK accepts EASING and KEYFRAME statements",
                      line.front().location);
        }
        ++index;
    }
    if (!closed) {
        add_error(result, "ICAD-P0015", "track block is missing END", track.location);
    }
    return track;
}

[[nodiscard]] auto parse_scene(const Lines& lines, std::size_t& index, ParseResult& result)
    -> ast::SceneDecl {
    const Line& declaration = lines[index];
    ast::SceneDecl scene;
    scene.location = declaration.front().location;
    if (declaration.size() != 2 || !valid_identifier(declaration[1])) {
        add_error(result, "ICAD-P0014", "SCENE expects exactly one identifier",
                  declaration.front().location);
    } else {
        scene.name = declaration[1].lexeme;
    }
    ++index;

    bool closed = false;
    while (index < lines.size()) {
        const Line& line = lines[index];
        if (keyword(line) == "END") {
            ++index;
            closed = true;
            break;
        }
        if (keyword(line) == "DURATION") {
            if (line.size() != 3) {
                add_error(result, "ICAD-P0014", "DURATION expects NUMBER UNIT",
                          line.front().location);
            } else {
                scene.duration = parse_quantity(line, 1, result);
            }
            ++index;
            continue;
        }
        if (keyword(line) == "FPS") {
            if (line.size() != 2 || !parse_number(line[1], scene.frames_per_second)) {
                add_error(result, "ICAD-P0014", "FPS expects one number", line.front().location);
            }
            ++index;
            continue;
        }
        if (keyword(line) == "BACKGROUND") {
            if (line.size() != 2 || !valid_identifier(line[1])) {
                add_error(result, "ICAD-P0014", "BACKGROUND expects one preset",
                          line.front().location);
            } else {
                scene.background = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (keyword(line) == "LOOP") {
            double count{};
            if (line.size() != 2 || !parse_number(line[1], count) || count < 1.0 ||
                count != static_cast<double>(static_cast<std::size_t>(count))) {
                add_error(result, "ICAD-P0014", "LOOP expects a positive integer",
                          line.front().location);
            } else {
                scene.loop_count = static_cast<std::size_t>(count);
            }
            ++index;
            continue;
        }
        if (keyword(line) == "LIGHT") {
            const bool layout = (line.size() == 9 || line.size() == 11) &&
                                valid_identifier(line[1]) && valid_identifier(line[2]) &&
                                line[3].lexeme == "COLOR" && line[7].lexeme == "INTENSITY" &&
                                (line.size() == 9 || line[9].lexeme == "AT");
            ast::LightDecl light;
            light.location = line.front().location;
            if (!layout) {
                add_error(result, "ICAD-P0014",
                          "LIGHT expects NAME DIRECTIONAL|POINT COLOR R G B INTENSITY N [AT POINT]",
                          line.front().location);
            } else {
                light.name = line[1].lexeme;
                light.kind = line[2].lexeme;
                bool valid = parse_number(line[4], light.color[0]);
                valid = parse_number(line[5], light.color[1]) && valid;
                valid = parse_number(line[6], light.color[2]) && valid;
                valid = parse_number(line[8], light.intensity) && valid;
                if (!valid) {
                    add_error(result, "ICAD-P0014", "LIGHT color and intensity must be numbers",
                              line.front().location);
                }
                if (line.size() == 11) {
                    light.point = line[10].lexeme;
                }
                scene.lights.push_back(std::move(light));
            }
            ++index;
            continue;
        }
        if (keyword(line) == "EVENT") {
            if (line.size() != 4 || !valid_identifier(line[3])) {
                add_error(result, "ICAD-P0014", "EVENT expects TIME NAME", line.front().location);
            } else {
                ast::SceneEventDecl event;
                event.time = parse_quantity(line, 1, result);
                event.name = line[3].lexeme;
                event.location = line.front().location;
                scene.events.push_back(std::move(event));
            }
            ++index;
            continue;
        }
        if (keyword(line) == "TRACK") {
            scene.tracks.push_back(parse_track(lines, index, result));
            continue;
        }
        add_error(result, "ICAD-P0014",
                  "SCENE accepts DURATION, FPS, BACKGROUND, LOOP, LIGHT, EVENT, and TRACK statements",
                  line.front().location);
        ++index;
    }
    if (!closed) {
        add_error(result, "ICAD-P0014", "scene block is missing END", scene.location);
    }
    return scene;
}

[[nodiscard]] auto parse_material(const Lines& lines, std::size_t& index, ParseResult& result)
    -> ast::MaterialDecl {
    const Line& declaration = lines[index];
    ast::MaterialDecl material;
    material.location = declaration.front().location;
    if (declaration.size() != 2 || !valid_identifier(declaration[1])) {
        add_error(result, "ICAD-P0013", "MATERIAL block expects one name",
                  declaration.front().location);
    } else {
        material.name = declaration[1].lexeme;
    }
    ++index;
    bool closed = false;
    while (index < lines.size()) {
        const Line& line = lines[index];
        const auto entry = keyword(line);
        if (entry == "END") {
            ++index;
            closed = true;
            break;
        }
        if (entry == "PRESET" && line.size() == 2 && valid_identifier(line[1])) {
            material.preset = line[1].lexeme;
        } else if (entry == "BASE_COLOR" && line.size() == 5) {
            bool valid = true;
            for (std::size_t component = 0; component < 4; ++component) {
                valid = parse_number(line[component + 1], material.base_color[component]) && valid;
            }
            if (!valid) {
                add_error(result, "ICAD-P0013", "BASE_COLOR expects four numbers",
                          line.front().location);
            } else {
                material.has_base_color = true;
            }
        } else if ((entry == "METALLIC" || entry == "ROUGHNESS") && line.size() == 2) {
            double value{};
            if (!parse_number(line[1], value)) {
                add_error(result, "ICAD-P0013", std::string{entry} + " expects one number",
                          line.front().location);
            } else if (entry == "METALLIC") {
                material.metallic = value;
                material.has_metallic = true;
            } else {
                material.roughness = value;
                material.has_roughness = true;
            }
        } else if (entry == "TEXTURE_SCALE" && line.size() == 3) {
            material.texture_scale.literal = parse_quantity(line, 1, result);
            material.texture_scale.location = line.front().location;
            material.has_texture_scale = true;
        } else if (entry == "UV_MODE" && line.size() == 2 && valid_identifier(line[1])) {
            material.uv_mode = line[1].lexeme;
        } else {
            add_error(result, "ICAD-P0013",
                      "MATERIAL accepts PRESET, BASE_COLOR, METALLIC, ROUGHNESS, "
                      "TEXTURE_SCALE, and UV_MODE",
                      line.front().location);
        }
        ++index;
    }
    if (!closed) {
        add_error(result, "ICAD-P0013", "material block is missing END", material.location);
    }
    return material;
}

} // namespace

auto parse(const std::vector<Token>& tokens) -> ParseResult {
    ParseResult result;
    const Lines lines = lines_from(tokens);
    std::size_t index = 0;

    while (index < lines.size()) {
        const Line& line = lines[index];
        const std::string_view first = keyword(line);
        if (first == "PROJECT") {
            if (line.size() != 2 || !valid_identifier(line[1])) {
                add_error(result, "ICAD-P0001", "PROJECT expects exactly one identifier",
                          line.front().location);
            } else if (!result.program.project_name.empty()) {
                add_error(result, "ICAD-P0008", "PROJECT is declared more than once",
                          line.front().location);
            } else {
                result.program.project_name = line[1].lexeme;
                result.program.location = line.front().location;
            }
            ++index;
            continue;
        }
        if (first == "UNITS") {
            if (line.size() != 2 || !valid_identifier(line[1])) {
                add_error(result, "ICAD-P0001", "UNITS expects exactly one identifier",
                          line.front().location);
            } else if (!result.program.default_length_unit.empty()) {
                add_error(result, "ICAD-P0009", "UNITS is declared more than once",
                          line.front().location);
            } else {
                result.program.default_length_unit = line[1].lexeme;
            }
            ++index;
            continue;
        }
        if (first == "PARAMETER") {
            if (line.size() != 4 || !valid_identifier(line[1])) {
                add_error(result, "ICAD-P0001", "PARAMETER expects NAME NUMBER UNIT",
                          line.front().location);
            } else {
                result.program.parameters.push_back(ast::ParameterDecl{
                    line[1].lexeme, parse_quantity(line, 2, result), line.front().location});
            }
            ++index;
            continue;
        }
        if (first == "ANGLE") {
            std::size_t cursor = 2;
            if (line.size() < 3 || !valid_identifier(line[1])) {
                add_error(result, "ICAD-P0020", "ANGLE expects NAME QUANTITY|PARAMETER",
                          line.front().location);
            } else {
                auto value = parse_value(line, cursor, result);
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0020", "ANGLE has unexpected trailing values",
                              line[cursor].location);
                }
                result.program.angles.push_back(
                    ast::AngleDecl{line[1].lexeme, std::move(value), line.front().location});
            }
            ++index;
            continue;
        }
        if (first == "POINT3") {
            const bool derived = line.size() >= 8 && line[2].lexeme == "FROM" &&
                                 valid_identifier(line[3]) && line[4].lexeme == "ALONG" &&
                                 valid_identifier(line[5]) && line[6].lexeme == "DISTANCE";
            std::size_t cursor = 2;
            if (!valid_identifier(line[1])) {
                add_error(result, "ICAD-P0020",
                          "POINT3 expects a name and an absolute or derived point definition",
                          line.front().location);
            } else if (derived) {
                ast::Point3Decl point;
                point.name = line[1].lexeme;
                point.base_point = line[3].lexeme;
                point.direction = line[5].lexeme;
                point.derived = true;
                point.location = line.front().location;
                cursor = 7;
                point.distance = parse_value(line, cursor, result);
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0020", "POINT3 has unexpected trailing values",
                              line[cursor].location);
                }
                result.program.points.push_back(std::move(point));
            } else if (line.size() < 5) {
                add_error(result, "ICAD-P0020",
                          "POINT3 expects NAME and three length quantities or parameters",
                          line.front().location);
            } else {
                std::array<ast::ValueDecl, 3> coordinates;
                for (auto& coordinate : coordinates) {
                    coordinate = parse_value(line, cursor, result);
                }
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0020", "POINT3 has unexpected trailing values",
                              line[cursor].location);
                }
                ast::Point3Decl point;
                point.name = line[1].lexeme;
                point.coordinates = std::move(coordinates);
                point.location = line.front().location;
                result.program.points.push_back(std::move(point));
            }
            ++index;
            continue;
        }
        if (first == "VECTOR") {
            const bool between_points = line.size() == 6 && line[2].lexeme == "FROM" &&
                                 valid_identifier(line[3]) && line[4].lexeme == "TO" &&
                                 valid_identifier(line[5]);
            const bool rotated = line.size() >= 8 && line[2].lexeme == "ROTATE" &&
                                 valid_identifier(line[3]) && line[4].lexeme == "AROUND" &&
                                 valid_identifier(line[5]) && line[6].lexeme == "BY";
            std::array<double, 3> components{};
            if (between_points && valid_identifier(line[1])) {
                ast::VectorDecl vector;
                vector.name = line[1].lexeme;
                vector.from_point = line[3].lexeme;
                vector.to_point = line[5].lexeme;
                vector.derived = true;
                vector.location = line.front().location;
                result.program.vectors.push_back(std::move(vector));
            } else if (rotated && valid_identifier(line[1])) {
                ast::VectorDecl vector;
                vector.name = line[1].lexeme;
                vector.source_vector = line[3].lexeme;
                vector.around_axis = line[5].lexeme;
                vector.rotated = true;
                vector.location = line.front().location;
                std::size_t cursor = 7;
                vector.angle = parse_value(line, cursor, result);
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0020", "VECTOR has unexpected trailing values",
                              line[cursor].location);
                }
                result.program.vectors.push_back(std::move(vector));
            } else if (line.size() != 5 || !valid_identifier(line[1]) ||
                       !parse_number(line[2], components[0]) ||
                       !parse_number(line[3], components[1]) ||
                       !parse_number(line[4], components[2])) {
                add_error(result, "ICAD-P0020",
                          "VECTOR expects NAME X Y Z, NAME FROM POINT TO POINT, or NAME ROTATE "
                          "VECTOR AROUND VECTOR BY ANGLE",
                          line.front().location);
            } else {
                ast::VectorDecl vector;
                vector.name = line[1].lexeme;
                vector.components = components;
                vector.location = line.front().location;
                result.program.vectors.push_back(std::move(vector));
            }
            ++index;
            continue;
        }
        if (first == "POSE") {
            const bool prefix = line.size() >= 8 && valid_identifier(line[1]) &&
                                line[2].lexeme == "AT" && valid_identifier(line[3]) &&
                                line[4].lexeme == "ROTATION";
            if (!prefix) {
                add_error(result, "ICAD-P0021", "POSE expects BODY AT POINT ROTATION X Y Z angles",
                          line.front().location);
            } else {
                std::size_t cursor = 5;
                std::array<ast::ValueDecl, 3> rotation;
                for (auto& angle : rotation) {
                    angle = parse_value(line, cursor, result);
                }
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0021", "POSE has unexpected trailing values",
                              line[cursor].location);
                }
                result.program.poses.push_back(ast::PoseDecl{
                    line[1].lexeme, line[3].lexeme, std::move(rotation), line.front().location});
            }
            ++index;
            continue;
        }
        if (first == "TOLERANCE") {
            const bool prefix = line.size() >= 5 && line[1].lexeme == "LINEAR";
            if (!prefix || result.program.tolerance.declared) {
                add_error(result, "ICAD-P0027",
                          "TOLERANCE expects LINEAR length ANGULAR angle and may appear once",
                          line.front().location);
            } else {
                std::size_t cursor = 2;
                ast::ToleranceDecl tolerance;
                tolerance.linear = parse_value(line, cursor, result);
                if (cursor >= line.size() || line[cursor].lexeme != "ANGULAR") {
                    add_error(result, "ICAD-P0027", "TOLERANCE requires ANGULAR after LINEAR",
                              line.front().location);
                } else {
                    ++cursor;
                    tolerance.angular = parse_value(line, cursor, result);
                }
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0027", "TOLERANCE has unexpected trailing values",
                              line[cursor].location);
                }
                tolerance.declared = true;
                tolerance.location = line.front().location;
                result.program.tolerance = std::move(tolerance);
            }
            ++index;
            continue;
        }
        if (first == "INSTANCE") {
            const bool prefix = line.size() >= 9 && valid_identifier(line[1]) &&
                                line[2].lexeme == "OF" && valid_identifier(line[3]) &&
                                line[4].lexeme == "AT" && valid_identifier(line[5]) &&
                                line[6].lexeme == "ROTATION";
            if (!prefix) {
                add_error(result, "ICAD-P0028",
                          "INSTANCE expects NAME OF BODY AT POINT ROTATION X Y Z",
                          line.front().location);
            } else {
                std::size_t cursor = 7;
                std::array<ast::ValueDecl, 3> rotation;
                for (auto& angle : rotation)
                    angle = parse_value(line, cursor, result);
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0028", "INSTANCE has unexpected trailing values",
                              line[cursor].location);
                }
                result.program.instances.push_back(ast::InstanceDecl{
                    line[1].lexeme, line[3].lexeme, line[5].lexeme, std::move(rotation),
                    line.front().location});
            }
            ++index;
            continue;
        }
        if (first == "JOINT") {
            const bool prefix =
                line.size() >= 9 && valid_identifier(line[1]) && valid_identifier(line[2]) &&
                valid_identifier(line[3]) && valid_identifier(line[4]) && line[5].lexeme == "AT" &&
                valid_identifier(line[6]) && line[7].lexeme == "AXIS" && valid_identifier(line[8]);
            if (!prefix) {
                add_error(result, "ICAD-P0022",
                          "JOINT expects NAME TYPE PARENT CHILD AT POINT AXIS VECTOR",
                          line.front().location);
            } else {
                ast::JointDecl joint;
                joint.name = line[1].lexeme;
                joint.kind = line[2].lexeme;
                joint.parent_body = line[3].lexeme;
                joint.child_body = line[4].lexeme;
                joint.point = line[6].lexeme;
                joint.axis = line[8].lexeme;
                joint.location = line.front().location;
                std::size_t cursor = 9;
                if (joint.kind != "FIXED") {
                    if (cursor >= line.size() || line[cursor].lexeme != "VALUE") {
                        add_error(result, "ICAD-P0022",
                                  "moving JOINT requires VALUE and LIMIT declarations",
                                  line.front().location);
                    } else {
                        ++cursor;
                        joint.value = parse_value(line, cursor, result);
                        if (cursor >= line.size() || line[cursor].lexeme != "LIMIT") {
                            add_error(result, "ICAD-P0022",
                                      "moving JOINT requires LIMIT LOWER UPPER", joint.location);
                        } else {
                            ++cursor;
                            joint.lower_limit = parse_value(line, cursor, result);
                            joint.upper_limit = parse_value(line, cursor, result);
                            joint.driven = true;
                        }
                    }
                }
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0022", "JOINT has unexpected trailing values",
                              line[cursor].location);
                }
                result.program.joints.push_back(std::move(joint));
            }
            ++index;
            continue;
        }
        if (first == "MATERIAL") {
            if (line.size() == 2) {
                result.program.materials.push_back(parse_material(lines, index, result));
                continue;
            }
            if (line.size() != 3 || !valid_identifier(line[1]) || !valid_identifier(line[2])) {
                add_error(result, "ICAD-P0013", "MATERIAL expects NAME PRESET",
                          line.front().location);
            } else {
                ast::MaterialDecl material;
                material.name = line[1].lexeme;
                material.preset = line[2].lexeme;
                material.location = line.front().location;
                result.program.materials.push_back(std::move(material));
            }
            ++index;
            continue;
        }
        if (first == "PROFILE") {
            result.program.profiles.push_back(parse_profile(lines, index, result));
            continue;
        }
        if (first == "SKETCH") {
            result.program.sketches.push_back(parse_sketch(lines, index, result));
            continue;
        }
        if (first == "BODY") {
            result.program.bodies.push_back(parse_body(lines, index, result));
            continue;
        }
        if (first == "CONSTRAINT") {
            const bool references = line.size() >= 5 && valid_identifier(line[1]) &&
                                    valid_identifier(line[2]) && valid_identifier(line[3]) &&
                                    valid_identifier(line[4]);
            const bool no_value = line.size() == 5 && (line[2].lexeme == "PARALLEL" ||
                                                       line[2].lexeme == "PERPENDICULAR");
            const bool value_kind = line[2].lexeme == "MIN_DISTANCE" ||
                                    line[2].lexeme == "COINCIDENT" ||
                                    line[2].lexeme == "ANGLE_BETWEEN";
            const bool with_value = line.size() >= 6 && value_kind;
            if (!references || (!no_value && !with_value)) {
                add_error(result, "ICAD-P0019",
                          "CONSTRAINT expects NAME KIND REFERENCE REFERENCE and an optional "
                          "tolerance or target quantity",
                          line.front().location);
            } else {
                ast::ConstraintDecl constraint;
                constraint.name = line[1].lexeme;
                constraint.kind = line[2].lexeme;
                constraint.first_body = line[3].lexeme;
                constraint.second_body = line[4].lexeme;
                constraint.location = line.front().location;
                if (with_value) {
                    std::size_t cursor = 5;
                    constraint.target = parse_value(line, cursor, result);
                    if (cursor != line.size()) {
                        add_error(result, "ICAD-P0019",
                                  "CONSTRAINT has unexpected trailing values",
                                  line[cursor].location);
                    }
                }
                result.program.constraints.push_back(std::move(constraint));
            }
            ++index;
            continue;
        }
        if (first == "MATE") {
            const bool header = line.size() >= 9 && valid_identifier(line[1]) &&
                                valid_identifier(line[3]) && valid_identifier(line[4]) &&
                                valid_identifier(line[5]) && valid_identifier(line[6]);
            const bool face = header && line[2].lexeme == "FACE" &&
                              line[7].lexeme == "OFFSET";
            const bool edge = header && line[2].lexeme == "EDGE" &&
                              line[7].lexeme == "TOLERANCE";
            if (!face && !edge) {
                add_error(result, "ICAD-P0027",
                          "MATE expects NAME FACE OCCURRENCE SELECTOR OCCURRENCE SELECTOR OFFSET VALUE or NAME EDGE ... TOLERANCE VALUE",
                          line.front().location);
            } else {
                ast::MateDecl mate;
                mate.name = line[1].lexeme;
                mate.kind = line[2].lexeme;
                mate.first_occurrence = line[3].lexeme;
                mate.first_selector = line[4].lexeme;
                mate.second_occurrence = line[5].lexeme;
                mate.second_selector = line[6].lexeme;
                mate.location = line.front().location;
                std::size_t cursor = 8;
                mate.target = parse_value(line, cursor, result);
                if (cursor != line.size()) {
                    add_error(result, "ICAD-P0027", "MATE has unexpected trailing values",
                              line[cursor].location);
                }
                result.program.mates.push_back(std::move(mate));
            }
            ++index;
            continue;
        }
        if (first == "SCENE") {
            result.program.scenes.push_back(parse_scene(lines, index, result));
            continue;
        }

        add_error(result, "ICAD-P0010",
                  "unexpected top-level statement '" + std::string{first} + "'",
                  line.front().location);
        ++index;
    }

    if (result.program.project_name.empty()) {
        add_error(result, "ICAD-P0011", "source must declare exactly one PROJECT",
                  SourceLocation{1, 1});
    }
    if (result.program.default_length_unit.empty()) {
        add_error(result, "ICAD-P0012", "source must declare default UNITS",
                  result.program.location);
    }
    return result;
}

} // namespace icad::compiler
