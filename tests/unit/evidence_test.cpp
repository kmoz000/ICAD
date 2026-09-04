#include "icad/compiler/compiler.hpp"
#include "icad/document/revision.hpp"
#include "icad/evidence/compliance.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view model = R"ICAD(PROJECT EvidencePart
UNITS mm
PARAMETER width 10 mm
MATERIAL alloy TITANIUM
BODY rotor
MATERIAL alloy
FEATURE blank
TYPE CYLINDER
RADIUS width
HEIGHT 5 mm
END
END
)ICAD";

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

auto replace_all(std::string source, std::string_view before, std::string_view after) -> std::string {
    std::size_t offset{};
    while ((offset = source.find(before, offset)) != std::string::npos) {
        source.replace(offset, before.size(), after);
        offset += after.size();
    }
    return source;
}

} // namespace

auto main() -> int {
    const auto compiled = icad::compiler::compile(model);
    if (!compiled.ok())
        return fail("evidence test model did not compile");
    if (icad::evidence::sha256("abc") !=
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
        return fail("SHA-256 implementation failed its standard vector");

    const auto root = std::filesystem::current_path() / "evidence-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto revision = icad::document::revision_id(model);
    const auto model_sha = icad::evidence::sha256(model);
    const std::string load_case = R"JSON({"schema":"test-load-case.v1","speedRpm":0,"status":"fixture-only"})JSON";
    const auto load_case_path = root / "load-case.json";
    {
        std::ofstream output{load_case_path};
        output << load_case;
    }
    const auto load_case_sha = icad::evidence::sha256(load_case);
    std::string analysis = R"JSON({
  "schema":"icad.analysis.result.v1",
  "analysisId":"AN-ROTOR-001",
  "discipline":"rotor-integrity",
  "solver":{"name":"partner-solver","version":"validated-1"},
  "inputModelRevision":"@REV@",
  "inputModelSha256":"@MODEL_SHA@",
  "inputDigests":{"LOAD-CASE":"@LOAD_SHA@"},
  "units":"SI",
  "assumptions":[],"results":{},"margins":{},"limitations":[],"artifacts":[],
  "author":{"name":"Engineer A","organization":"Partner A"},
  "independentReviewer":{"name":"Engineer B","organization":"Partner B","approvedAt":"2026-09-03"},
  "disposition":"accepted"
})JSON";
    analysis = replace_all(std::move(analysis), "@REV@", revision);
    analysis = replace_all(std::move(analysis), "@MODEL_SHA@", model_sha);
    analysis = replace_all(std::move(analysis), "@LOAD_SHA@", load_case_sha);
    const auto analysis_path = root / "rotor.json";
    {
        std::ofstream output{analysis_path};
        output << analysis;
    }
    const auto analysis_sha = icad::evidence::sha256(analysis);
    std::string manifest = R"JSON({
  "schema":"icad.evidence.manifest.v1",
  "project":"EvidencePart",
  "basis":"EASA-CS-E-AMENDMENT-8",
  "lifecycleState":"DEVELOPMENT",
  "model":{"revision":"@REV@","sha256":"@MODEL_SHA@"},
  "controlledInputs":[{"id":"LOAD-CASE","kind":"load-case","path":"load-case.json","sha256":"@LOAD_SHA@"}],
  "requirements":[{"id":"REQ-ROTOR","title":"Rotor integrity","evidenceState":"independently-reviewed","criticality":"safety-critical","entities":["rotor"],"evidence":["AN-ROTOR-001"]}],
  "compliance":[{"paragraph":"CS-E 810","applicability":"development-analogue","rationale":"Containment development evidence","reviewer":"Engineer B, Partner B","status":"satisfied","evidence":["AN-ROTOR-001"]}],
  "artifacts":[{"id":"AN-ROTOR-001","kind":"analysis","path":"rotor.json","sha256":"@ANALYSIS_SHA@","disposition":"accepted","inputModelRevision":"@REV@","inputModelSha256":"@MODEL_SHA@","inputDigests":{"LOAD-CASE":"@LOAD_SHA@"},"units":"SI","author":{"name":"Engineer A","organization":"Partner A"},"independentReviewer":{"name":"Engineer B","organization":"Partner B","approvedAt":"2026-09-03"}}],
  "hazards":[],"approvals":[],"prohibitedClaims":["EASA certified","airworthy","flight approved","TYPE_CERTIFIED"]
})JSON";
    manifest = replace_all(std::move(manifest), "@REV@", revision);
    manifest = replace_all(std::move(manifest), "@MODEL_SHA@", model_sha);
    manifest = replace_all(std::move(manifest), "@ANALYSIS_SHA@", analysis_sha);
    manifest = replace_all(std::move(manifest), "@LOAD_SHA@", load_case_sha);
    const auto evaluated = icad::evidence::evaluate(model, *compiled.ir_project, manifest,
                                                     root / "model.evidence.json",
                                                     "EASA-CS-E-AMENDMENT-8");
    if (!evaluated.manifest_valid || evaluated.release_ready ||
        !icad::evidence::compliance_json(evaluated).contains("icad.compliance.v1") ||
        !icad::evidence::evidence_json(evaluated).contains("icad.evidence.manifest.v1") ||
        !icad::evidence::compliance_html(evaluated).contains("Ground-test release blocked"))
        return fail("valid development evidence was rejected");

    {
        std::ofstream output{load_case_path};
        output << load_case << '\n';
    }
    const auto input_stale_evaluation = icad::evidence::evaluate(
        model, *compiled.ir_project, manifest, root / "model.evidence.json");
    if (input_stale_evaluation.manifest_valid ||
        !icad::evidence::compliance_json(input_stale_evaluation).contains("ICAD-E0017"))
        return fail("changed controlled input did not stale the evidence");
    {
        std::ofstream output{load_case_path};
        output << load_case;
    }

    const auto malformed = icad::evidence::evaluate(
        model, *compiled.ir_project, "{", root / "model.evidence.json");
    if (malformed.manifest_valid ||
        !icad::evidence::compliance_json(malformed).contains("ICAD-E0001"))
        return fail("malformed evidence manifest was not rejected");

    const auto unreviewed_manifest = replace_all(
        manifest, "\"approvedAt\":\"2026-09-03\"", "\"approvedAt\":\"\"");
    const auto unreviewed = icad::evidence::evaluate(
        model, *compiled.ir_project, unreviewed_manifest, root / "model.evidence.json");
    if (unreviewed.manifest_valid ||
        !icad::evidence::compliance_json(unreviewed).contains("ICAD-E0028"))
        return fail("unreviewed accepted evidence was not rejected");

    const auto wrong_units_manifest = replace_all(
        manifest, "\"units\":\"SI\"", "\"units\":\"millimetres\"");
    const auto wrong_units = icad::evidence::evaluate(
        model, *compiled.ir_project, wrong_units_manifest, root / "model.evidence.json");
    if (wrong_units.manifest_valid ||
        !icad::evidence::compliance_json(wrong_units).contains("ICAD-E0027"))
        return fail("mismatched artifact units were not rejected");

    auto rejected_manifest = replace_all(manifest, "\"disposition\":\"accepted\"",
                                         "\"disposition\":\"rejected\"");
    rejected_manifest = replace_all(std::move(rejected_manifest), "DEVELOPMENT",
                                    "GROUND_TEST_RELEASED");
    const auto rejected = icad::evidence::evaluate(
        model, *compiled.ir_project, rejected_manifest, root / "model.evidence.json");
    if (rejected.manifest_valid ||
        !icad::evidence::compliance_json(rejected).contains("ICAD-E0052"))
        return fail("rejected evidence was allowed to support a release");

    {
        std::ofstream output{analysis_path};
        output << analysis << '\n';
    }
    const auto tampered = icad::evidence::evaluate(
        model, *compiled.ir_project, manifest, root / "model.evidence.json");
    if (tampered.manifest_valid ||
        !icad::evidence::compliance_json(tampered).contains("ICAD-E0026"))
        return fail("tampered evidence artifact was not rejected");
    {
        std::ofstream output{analysis_path};
        output << analysis;
    }

    const auto stale = replace_all(manifest, model_sha, std::string(64, '0'));
    const auto stale_evaluation = icad::evidence::evaluate(
        model, *compiled.ir_project, stale, root / "model.evidence.json");
    if (stale_evaluation.manifest_valid ||
        !icad::evidence::compliance_json(stale_evaluation).contains("ICAD-E0012"))
        return fail("stale evidence was not rejected");

    const auto certified = replace_all(manifest, "DEVELOPMENT", "TYPE_CERTIFIED");
    const auto certified_evaluation = icad::evidence::evaluate(
        model, *compiled.ir_project, certified, root / "model.evidence.json");
    if (certified_evaluation.manifest_valid ||
        !icad::evidence::compliance_json(certified_evaluation).contains("ICAD-E0006"))
        return fail("self-declared type certification was not rejected");
    std::filesystem::remove_all(root);
    return 0;
}
