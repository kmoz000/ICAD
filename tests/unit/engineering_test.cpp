#include "icad/cad/analysis.hpp"
#include "icad/compiler/compiler.hpp"
#include "icad/document/exporter.hpp"
#include "icad/document/revision.hpp"
#include "icad/drawings/exporter.hpp"
#include "icad/manufacturing/validator.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view source = R"ICAD(
PROJECT EngineeringTest
UNITS mm
MATERIAL frame STRUCTURAL_STEEL
BODY chassis
MATERIAL frame
FEATURE rail
TYPE BOX
WIDTH 20 mm
DEPTH 10 mm
HEIGHT 5 mm
END
END
)ICAD";

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

[[nodiscard]] auto contents(const std::filesystem::path& path) -> std::string {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

} // namespace

auto main() -> int {
    const auto compilation = icad::compiler::compile(source);
    if (!compilation.ok()) {
        return fail("engineering fixture did not compile");
    }
    const auto& project = *compilation.ir_project;
    const auto analysis = icad::cad::analyze(project);
    if (analysis.parts.size() != 1 || analysis.volume_mm3 < 999.9 ||
        analysis.surface_area_mm2 < 699.9) {
        return fail("engineering analysis produced incorrect metrics");
    }
    icad::document::RevisionStore revisions{project};
    const auto first_revision = revisions.revision();
    auto changed = project;
    changed.name = "EngineeringTestV2";
    if (revisions.commit(changed, first_revision + 1) ||
        !revisions.commit(std::move(changed), first_revision) || !revisions.undo() ||
        !revisions.redo()) {
        return fail("optimistic revision history failed");
    }

    const auto report = icad::manufacturing::validate(project);
    if (!report.passed || !report.issues.empty() || report.process != "GENERAL" ||
        report.checked_rules != 8) {
        return fail("manufacturing validation rejected a sound solid");
    }
    auto sheet_rules = icad::manufacturing::Rules{};
    sheet_rules.process = "SHEET_METAL";
    const auto sheet_report = icad::manufacturing::validate(project, sheet_rules);
    if (!sheet_report.passed) {
        return fail("sheet-metal rules rejected structural steel");
    }
    auto incompatible = project;
    incompatible.materials.front().preset = "PLASTIC";
    if (icad::manufacturing::validate(incompatible, sheet_rules).passed) {
        return fail("sheet-metal rules accepted an incompatible material");
    }

    const auto output_root = std::filesystem::current_path() / "engineering-test-output";
    std::filesystem::create_directories(output_root);
    const auto bom_path = output_root / "model.bom.json";
    const auto report_path = output_root / "model.manufacturing.json";
    const auto drawing_path = output_root / "model.drawing.svg";
    const auto dxf_path = output_root / "model.drawing.dxf";
    if (!icad::document::write_bom(project, bom_path).success ||
        !icad::manufacturing::write_report(project, report_path) ||
        !icad::drawings::write_svg(project, drawing_path).success ||
        !icad::drawings::write_dxf(project, dxf_path).success ||
        !icad::drawings::inspect_dxf(dxf_path).success) {
        return fail("engineering artifact export failed");
    }
    if (!contents(bom_path).contains("\"volumeMm3\":") ||
        !contents(report_path).contains("\"passed\":true") ||
        !contents(report_path).contains("\"checkedRules\":8") ||
        !contents(drawing_path).contains("<svg") ||
        !contents(drawing_path).contains("data-sheet-kind=\"part\"") ||
        !contents(drawing_path).contains("PART DETAIL") ||
        !contents(drawing_path).contains("FEATURE AND PARAMETER SCHEDULE") ||
        !contents(drawing_path).contains("SKETCH / PROFILE SCHEDULE") ||
        !contents(drawing_path).contains("data-sheet-kind=\"assembly\"") ||
        !contents(drawing_path).contains("ASSEMBLY CONNECTION SCHEDULE") ||
        !contents(drawing_path).contains("Third-angle projected native edges") ||
        !contents(drawing_path).contains("DATUMS: A | B | C") ||
        !contents(dxf_path).contains("VISIBLE_TOP") ||
        !contents(dxf_path).contains("TITLE_BLOCK") ||
        !contents(dxf_path).contains("GENERAL TOLERANCE")) {
        return fail("engineering artifacts do not contain required data");
    }
    return 0;
}
