#include "icad/ai/inspector.hpp"
#include "icad/compiler/compiler.hpp"
#include "icad/lsp/server.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view valid_source =
    "PROJECT AgentTest\nUNITS mm\nBODY part\nFEATURE cube\nTYPE BOX\nWIDTH 1 mm\nDEPTH 1 "
    "mm\nHEIGHT 1 mm\nEND\nEND\n";

auto frame(std::string_view json) -> std::string {
    return "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n" + std::string{json};
}

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

auto main() -> int {
    const auto compilation = icad::compiler::compile(valid_source);
    if (!compilation.ok()) {
        return fail("agent fixture did not compile");
    }
    const auto inspection = icad::ai::project_json(*compilation.ir_project);
    if (!inspection.contains("\"schema\":\"icad.inspect.v1\"") ||
        !inspection.contains("\"volumeMm3\":") ||
        !inspection.contains("\"topology\":{\"valid\":true") ||
        !inspection.contains("\"intersections\":{\"partPairCandidates\":")) {
        return fail("agent inspection JSON is incomplete");
    }
    const auto visual = icad::ai::visual_snapshot_json(*compilation.ir_project);
    if (!visual.contains("\"schema\":\"icad.visual.snapshot.v1\"") ||
        !visual.contains("\"name\":\"front\"") ||
        !visual.contains("\"name\":\"right\"") ||
        !visual.contains("\"name\":\"top\"") ||
        !visual.contains("\"name\":\"isometric\"") ||
        !visual.contains("\"body\":\"part\"") || !visual.contains("\"rows\":[")) {
        return fail("agent visual snapshot JSON is incomplete");
    }
    constexpr std::string_view alternative_source =
        "PROJECT AgentAlternative\nUNITS mm\nMATERIAL finish TITANIUM\nBODY part\nMATERIAL "
        "finish\nFEATURE cube\nTYPE BOX\nWIDTH 2 mm\nDEPTH 1 mm\nHEIGHT 1 mm\nEND\nEND\n"
        "BODY sensor\nMATERIAL finish\nFEATURE lens\nTYPE SPHERE\nRADIUS 1 mm\nORIGIN_X 4 "
        "mm\nEND\nEND\n";
    const auto alternative = icad::compiler::compile(alternative_source);
    if (!alternative.ok())
        return fail("agent comparison fixture did not compile");
    const auto comparison =
        icad::ai::comparison_json(*compilation.ir_project, *alternative.ir_project);
    if (!comparison.contains("\"schema\":\"icad.agent.comparison.v2\"") ||
        !comparison.contains("\"secondOnlyBodies\":[\"sensor\"]") ||
        !comparison.contains("\"body\":\"part\"") ||
        !comparison.contains("\"materialPreset\":\"TITANIUM\"") ||
        !comparison.contains("\"selectionDimensions\":[") ||
        !comparison.contains("\"mechanismDelta\":{") ||
        !comparison.contains("\"viewDelta\":{") ||
        !comparison.contains("\"differenceGrid\":{") ||
        !comparison.contains("\"silhouetteIntersectionOverUnion\":") ||
        !comparison.contains("\"optimizationMatrix\":[") ||
        !comparison.contains("\"semanticRoleHint\":") ||
        !comparison.contains("\"decisionPolicy\":{") ||
        !comparison.contains("\"visual\":{\"schema\":\"icad.visual.snapshot.v1\"")) {
        return fail("agent structural comparison JSON is incomplete");
    }
    std::string large_source{"PROJECT LargeAgentView\nUNITS mm\n"};
    for (int index = 0; index < 63; ++index) {
        large_source += "BODY body_" + std::to_string(index) + "\nFEATURE box\nTYPE BOX\n"
                        "WIDTH 1 mm\nDEPTH 1 mm\nHEIGHT 1 mm\nORIGIN_X " +
                        std::to_string(index * 2) + " mm\nEND\nEND\n";
    }
    const auto large_compilation = icad::compiler::compile(large_source);
    if (!large_compilation.ok() ||
        !icad::ai::visual_snapshot_json(*large_compilation.ir_project)
             .contains("\"truncatedBodies\":1")) {
        return fail("agent visual snapshot did not report its raster legend limit");
    }
    const auto diagnostics = icad::ai::diagnostics_json("PROJECT bad\n$");
    if (!diagnostics.contains("\"ok\":false") || !diagnostics.contains("ICAD-L0001")) {
        return fail("agent diagnostics JSON is incomplete");
    }

    std::istringstream input{
        frame("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}") +
        frame("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
              "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"file:///"
              "bad.icad\",\"text\":\"PROJECT bad\\nUNITS mm  \\nBODY part\\nEND\"}}}") +
        frame("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/completion\","
              "\"params\":{\"textDocument\":{\"uri\":\"file:///bad.icad\"},"
              "\"position\":{\"line\":1,\"character\":1}}}") +
        frame("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"textDocument/definition\","
              "\"params\":{\"textDocument\":{\"uri\":\"file:///bad.icad\"},"
              "\"position\":{\"line\":2,\"character\":7}}}") +
        frame("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"textDocument/formatting\","
              "\"params\":{\"textDocument\":{\"uri\":\"file:///bad.icad\"}}}") +
        frame("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"textDocument/codeAction\","
              "\"params\":{\"textDocument\":{\"uri\":\"file:///bad.icad\"},"
              "\"range\":{\"start\":{\"line\":0,\"character\":0},"
              "\"end\":{\"line\":0,\"character\":1}},\"context\":{\"diagnostics\":["
              "{\"code\":\"ICAD-P0012\"}]}}}") +
        frame("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"textDocument/codeAction\","
              "\"params\":{\"textDocument\":{\"uri\":\"file:///bad.icad\"},"
              "\"range\":{\"start\":{\"line\":3,\"character\":3},"
              "\"end\":{\"line\":3,\"character\":3}},\"context\":{\"diagnostics\":["
              "{\"code\":\"ICAD-P0005\"}]}}}") +
        frame("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"shutdown\",\"params\":null}") +
        frame("{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}")};
    std::ostringstream output;
    if (icad::lsp::run(input, output) != 0 || !output.str().contains("textDocumentSync") ||
        !output.str().contains("textDocument/publishDiagnostics") ||
        !output.str().contains("completionProvider") ||
        !output.str().contains("documentFormattingProvider") ||
        !output.str().contains("codeActionProvider") ||
        !output.str().contains("\"label\":\"PROJECT\"") ||
        !output.str().contains("Insert default UNITS declaration") ||
        !output.str().contains("Insert missing END") ||
        !output.str().contains("\"line\":3,\"character\":3") ||
        !output.str().contains("\"newText\":\"PROJECT bad\\nUNITS mm\\nBODY part\\nEND\\n\"") ||
        !output.str().contains("\"line\":2,\"character\":5")) {
        return fail("LSP initialize/diagnostics/shutdown flow failed");
    }
    return 0;
}
