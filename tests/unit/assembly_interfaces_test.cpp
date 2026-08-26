#include "icad/ai/inspector.hpp"
#include "icad/cad/intersection.hpp"
#include "icad/compiler/compiler.hpp"
#include "icad/manufacturing/validator.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view source = R"ICAD(
REQUIRES ICAD 1.0
REQUIRES CAPABILITY MANUFACTURING_CONNECTIONS_V1
REQUIRES CAPABILITY MAGNETIC_INTERFACE_SNAP_V1
PROJECT interface_fixture
UNITS mm
TOLERANCE LINEAR 0.01 mm ANGULAR 0.1 deg
POINT3 seat_a 20 mm 10 mm 5 mm
POINT3 seat_b 20 mm 10 mm 5 mm
POINT3 bracket_origin 10 mm 5 mm 5 mm
VECTOR axis_a 0 0 1
VECTOR axis_b 0 0 -1
MATERIAL alloy ALUMINUM
BODY base
MATERIAL alloy
FEATURE stock
TYPE BOX
WIDTH 40 mm
DEPTH 20 mm
HEIGHT 5 mm
END
END
BODY bracket
MATERIAL alloy
FEATURE stock
TYPE BOX
WIDTH 20 mm
DEPTH 10 mm
HEIGHT 5 mm
END
END
POSE bracket AT bracket_origin ROTATION 0 deg 0 deg 0 deg
INTERFACE base_flange BODY base AT seat_a AXIS axis_a TYPE FLANGE SIZE 20 mm
INTERFACE bracket_flange BODY bracket AT seat_b AXIS axis_b TYPE FLANGE SIZE 20 mm
CONNECT bracket_mount base_flange bracket_flange METHOD BOLTED STANDARD ISO_4762 FASTENER M8 CLEARANCE 0.02 mm AUTO
)ICAD";

[[nodiscard]] auto contains(const std::string& text, std::string_view value) -> bool {
    return text.find(value) != std::string::npos;
}

} // namespace

auto main() -> int {
    const auto compilation = icad::compiler::compile(source);
    if (!compilation.ok()) {
        std::cerr << "manufacturing interface fixture did not compile\n";
        return 1;
    }
    const auto& project = *compilation.ir_project;
    if (project.interfaces.size() != 2 || project.connections.size() != 1 ||
        !project.connections.front().aligned || !project.connections.front().automatic ||
        project.connections.front().method != "BOLTED" ||
        project.connections.front().standard != "ISO_4762" ||
        project.connections.front().fastener != "M8") {
        std::cerr << "interface or connection metadata did not lower to IR\n";
        return 1;
    }
    const std::string visual = icad::ai::visual_snapshot_json(project);
    if (!contains(visual, "\"connections\":[{") ||
        !contains(visual, "\"snapState\":\"SEATED\"") ||
        !contains(visual, "\"standard\":\"ISO_4762\"") ||
        !contains(visual, "\"axisAlignment\":1")) {
        std::cerr << "visual JSON omitted agent-readable assembly evidence\n";
        return 1;
    }
    if (!icad::manufacturing::validate(project).passed) {
        std::cerr << "seated connection failed manufacturing validation\n";
        return 1;
    }
    const auto intersections = icad::cad::analyze_intersections(project);
    if (intersections.body_contacts.size() != 1 ||
        !intersections.body_contacts.front().declared_connection ||
        intersections.body_contacts.front().connection_name != "bracket_mount" ||
        intersections.unintended_penetrating_part_pairs != 0) {
        std::cerr << "intersection evidence did not identify the declared connection\n";
        return 1;
    }

    const auto stale_datums = icad::compiler::compile(R"ICAD(
PROJECT stale_connection
UNITS mm
TOLERANCE LINEAR 0.01 mm ANGULAR 0.1 deg
POINT3 cover_origin 0 mm 0 mm 10 mm
POINT3 stale_seat 5 mm 5 mm 10 mm
VECTOR up 0 0 1
VECTOR down 0 0 -1
MATERIAL alloy ALUMINUM
BODY base
MATERIAL alloy
FEATURE stock
TYPE BOX
WIDTH 100 mm
DEPTH 200 mm
HEIGHT 100 mm
END
END
INSTANCE cover OF base AT cover_origin ROTATION 0 deg 0 deg 0 deg
MATE seated FACE base Z_MAX cover Z_MIN OFFSET 0 mm
INTERFACE base_flange BODY base AT stale_seat AXIS up TYPE FLANGE SIZE 10 mm
INTERFACE cover_flange BODY cover AT stale_seat AXIS down TYPE FLANGE SIZE 10 mm
CONNECT mount base_flange cover_flange METHOD BOLTED STANDARD ISO_4762 FASTENER M6 CLEARANCE 0.01 mm AUTO
)ICAD");
    if (!stale_datums.ok()) {
        std::cerr << "stale-datum regression fixture did not compile\n";
        return 1;
    }
    const auto stale_report = icad::manufacturing::validate(*stale_datums.ir_project);
    const auto stale_visual = icad::ai::visual_snapshot_json(*stale_datums.ir_project);
    if (stale_report.passed ||
        std::ranges::none_of(stale_report.issues, [](const auto& issue) {
            return issue.code == "ICAD-M0013";
        }) || !contains(stale_visual, "\"engineeringValid\":false") ||
        !contains(stale_visual, "\"snapState\":\"INVALID_GEOMETRY\"") ||
        !contains(stale_visual, "\"manufacturingPassed\":false")) {
        std::cerr << "resized connection with detached interface datums was accepted\n";
        return 1;
    }

    const auto missing_standard = icad::compiler::compile(
        "PROJECT invalid_connection\nUNITS mm\nPOINT3 p 0 mm 0 mm 0 mm\n"
        "VECTOR z 0 0 1\nBODY a\nFEATURE x\nTYPE BOX\nWIDTH 1 mm\nDEPTH 1 mm\n"
        "HEIGHT 1 mm\nEND\nEND\nBODY b\nFEATURE x\nTYPE BOX\nWIDTH 1 mm\nDEPTH 1 mm\n"
        "HEIGHT 1 mm\nEND\nEND\nINTERFACE ia BODY a AT p AXIS z TYPE FLANGE\n"
        "INTERFACE ib BODY b AT p AXIS z TYPE FLANGE\n"
        "CONNECT bad ia ib METHOD BOLTED FASTENER M8 CLEARANCE 0.1 mm\n");
    if (missing_standard.ok() || missing_standard.diagnostics.back().code != "ICAD-S0042") {
        std::cerr << "connection without manufacturing standard was accepted\n";
        return 1;
    }

    const auto incompatible = icad::compiler::compile(
        "PROJECT incompatible_interfaces\nUNITS mm\nPOINT3 p 0 mm 0 mm 0 mm\n"
        "VECTOR z 0 0 1\nBODY a\nFEATURE x\nTYPE BOX\nWIDTH 1 mm\nDEPTH 1 mm\n"
        "HEIGHT 1 mm\nEND\nEND\nBODY b\nFEATURE x\nTYPE BOX\nWIDTH 1 mm\nDEPTH 1 mm\n"
        "HEIGHT 1 mm\nEND\nEND\nINTERFACE ia BODY a AT p AXIS z TYPE SHAFT SIZE 1 mm\n"
        "INTERFACE ib BODY b AT p AXIS z TYPE FLANGE SIZE 1 mm\n"
        "CONNECT bad ia ib METHOD BEARING STANDARD ISO_492 FIT H7_g6\n");
    if (incompatible.ok() ||
        incompatible.diagnostics.back().message.find("incompatible with INTERFACE types") ==
            std::string::npos) {
        std::cerr << "incompatible manufacturing interface pair was accepted\n";
        return 1;
    }
    return 0;
}
