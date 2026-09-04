#include "icad/viewer/live_session.hpp"
#include "icad/document/revision.hpp"
#include "icad/evidence/compliance.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr std::string_view source =
    "PROJECT live_viewer\nUNITS mm\nBODY part\nFEATURE shape\nTYPE BOX\nWIDTH 10 mm\n"
    "DEPTH 20 mm\nHEIGHT 30 mm\nEND\nEND\n";

constexpr std::string_view connection_source = R"ICAD(
PROJECT live_connection
UNITS mm
TOLERANCE LINEAR 0.01 mm ANGULAR 0.1 deg
POINT3 cover_origin 0 mm 0 mm 10 mm
POINT3 seat 5 mm 5 mm 10 mm
VECTOR up 0 0 1
VECTOR down 0 0 -1
MATERIAL alloy ALUMINUM
BODY base
MATERIAL alloy
FEATURE stock
TYPE BOX
WIDTH 10 mm
DEPTH 10 mm
HEIGHT 10 mm
END
END
INSTANCE cover OF base AT cover_origin ROTATION 0 deg 0 deg 0 deg
MATE seated FACE base Z_MAX cover Z_MIN OFFSET 0 mm
INTERFACE base_flange BODY base AT seat AXIS up TYPE FLANGE SIZE 10 mm
INTERFACE cover_flange BODY cover AT seat AXIS down TYPE FLANGE SIZE 10 mm
CONNECT mount base_flange cover_flange METHOD BOLTED STANDARD ISO_4762 FASTENER M6 CLEARANCE 0.01 mm AUTO
)ICAD";

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

auto replace_all(std::string text, std::string_view before, std::string_view after) -> std::string {
    std::size_t offset{};
    while ((offset = text.find(before, offset)) != std::string::npos) {
        text.replace(offset, before.size(), after);
        offset += after.size();
    }
    return text;
}

} // namespace

auto main() -> int {
    const auto root = std::filesystem::current_path() / "live-viewer-test-output";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const auto path = root / "live.icad";
    {
        std::ofstream output{path, std::ios::binary};
        output << source;
    }

    icad::viewer::LiveSession session{path};
    if (!session.ready() || session.source() != source)
        return fail("live session did not load .icad source");
    const auto first = session.preview(session.source());
    if (!first.success || first.bodies != 1 || first.revision != 1 ||
        first.recomputed_bodies != 1 || first.model_json.find("\"project\":\"live_viewer\"") ==
                                             std::string::npos ||
        first.model_json.find("\"parts\":[") == std::string::npos)
        return fail("valid source did not produce an in-memory live model");
    const auto unchanged = session.preview(session.source());
    if (!unchanged.success || !unchanged.unchanged || unchanged.recomputed_bodies != 0 ||
        unchanged.revision != first.revision)
        return fail("unchanged source did not reuse the live preview");
    const auto invalid = session.preview(std::string{source} + "$\n");
    if (invalid.success || invalid.diagnostics.empty() ||
        invalid.diagnostics.front().code != "ICAD-L0001")
        return fail("invalid edit did not preserve the last valid preview and diagnostics");

    std::string changed{source};
    changed.replace(changed.find("WIDTH 10 mm"), std::string_view{"WIDTH 10 mm"}.size(),
                    "WIDTH 12 mm");
    const auto refreshed = session.preview(changed);
    if (!refreshed.success || refreshed.recomputed_bodies != 1 || refreshed.revision != 2)
        return fail("changed body did not refresh incrementally");
    const auto saved = session.save(changed);
    std::ifstream input{path, std::ios::binary};
    const std::string persisted{std::istreambuf_iterator<char>{input},
                                std::istreambuf_iterator<char>{}};
    if (!saved.success || persisted != changed)
        return fail("live editor save did not persist source atomically");
    const auto package = session.export_package(changed, root / "exported");
    if (!package.success || package.artifacts != 13 || package.components != 1 ||
        package.solids != 1 || !std::filesystem::exists(root / "exported/live.step") ||
        !std::filesystem::exists(root / "exported/live.scene.json") ||
        std::filesystem::exists(root / "exported/live.html"))
        return fail("live viewer did not export the complete artifact package");

    const auto connection_path = root / "connection.icad";
    {
        std::ofstream output{connection_path, std::ios::binary};
        output << connection_source;
    }
    icad::viewer::LiveSession connection_session{connection_path};
    const auto seated = connection_session.preview(connection_session.source());
    if (!seated.success || seated.model_json.empty())
        return fail("valid seated manufacturing connection did not preview");
    std::string resized{connection_source};
    const auto height = resized.find("HEIGHT 10 mm");
    resized.replace(height, std::string_view{"HEIGHT 10 mm"}.size(), "HEIGHT 100 mm");
    const auto flagged = connection_session.preview(resized);
    if (!flagged.success || flagged.engineering_valid || flagged.model_json.empty() ||
        flagged.diagnostics.empty() ||
        std::ranges::none_of(flagged.diagnostics, [](const auto& diagnostic) {
            return diagnostic.code == "ICAD-V0001" || diagnostic.code == "ICAD-M0013";
        })) {
        return fail("engineering issue did not retain diagnostics with the current geometry");
    }
    const auto restored = connection_session.preview(connection_source);
    if (!restored.success || !restored.engineering_valid || restored.model_json.empty())
        return fail("valid connection did not render after an engineering issue was corrected");

    const auto evidence_path = root / "evidence.icad";
    const auto input_path = root / "controlled.json";
    const auto manifest_path = root / "evidence.evidence.json";
    constexpr std::string_view controlled_input = "{\"revision\":1}";
    {
        std::ofstream output{evidence_path, std::ios::binary};
        output << source;
    }
    {
        std::ofstream output{input_path, std::ios::binary};
        output << controlled_input;
    }
    std::string manifest = R"JSON({
"schema":"icad.evidence.manifest.v1","project":"live_viewer","basis":"TEST-BASIS","lifecycleState":"DEVELOPMENT",
"model":{"path":"evidence.icad","revision":"@REV@","sha256":"@MODEL_SHA@"},
"controlledInputs":[{"id":"INPUT","kind":"load-case","path":"controlled.json","sha256":"@INPUT_SHA@"}],
"requirements":[{"id":"REQ","title":"Fixture requirement","evidenceState":"assumed","criticality":"ordinary","entities":["part"],"evidence":[]}],
"compliance":[{"paragraph":"TEST","applicability":"not-applicable","rationale":"Viewer cache fixture","reviewer":"Test fixture","status":"open","evidence":[]}],
"artifacts":[],"hazards":[],"approvals":[],"prohibitedClaims":["EASA certified","airworthy","flight approved","TYPE_CERTIFIED"]})JSON";
    manifest = replace_all(std::move(manifest), "@REV@", icad::document::revision_id(source));
    manifest = replace_all(std::move(manifest), "@MODEL_SHA@", icad::evidence::sha256(source));
    manifest = replace_all(std::move(manifest), "@INPUT_SHA@",
                           icad::evidence::sha256(controlled_input));
    {
        std::ofstream output{manifest_path, std::ios::binary};
        output << manifest;
    }
    icad::viewer::LiveSession evidence_session{evidence_path};
    const auto evidence_first = evidence_session.preview(evidence_session.source());
    const auto evidence_reused = evidence_session.preview(evidence_session.source());
    if (!evidence_first.success || evidence_first.evidence_json.empty() ||
        !evidence_first.evidence_json.contains("\"manifestValid\":true") ||
        !evidence_reused.unchanged)
        return fail("viewer did not evaluate and cache adjacent engineering evidence");
    {
        std::ofstream output{input_path, std::ios::binary};
        output << controlled_input << '\n';
    }
    const auto evidence_stale = evidence_session.preview(evidence_session.source());
    if (!evidence_stale.success || evidence_stale.unchanged ||
        !evidence_stale.evidence_json.contains("ICAD-E0017"))
        return fail("viewer did not invalidate evidence after a controlled input changed");
    return 0;
}
