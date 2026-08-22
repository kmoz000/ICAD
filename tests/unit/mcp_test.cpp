#include "icad/document/revision.hpp"
#include "icad/document/source.hpp"
#include "icad/json/value.hpp"
#include "icad/mcp/server.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view valid_source =
    "PROJECT McpPart\nUNITS mm\nPARAMETER width 10 mm\nMATERIAL finish ALUMINUM\nBODY "
    "part\nMATERIAL finish\nFEATURE cube\nTYPE BOX\nWIDTH width\nDEPTH 20 mm\nHEIGHT 30 "
    "mm\nEND\nEND\n";

constexpr std::string_view query_source = R"ICAD(PROJECT Queries
UNITS mm
BODY first
FEATURE box
TYPE BOX
WIDTH 10 mm
DEPTH 10 mm
HEIGHT 10 mm
END
END
BODY second
FEATURE box
TYPE BOX
WIDTH 10 mm
DEPTH 10 mm
HEIGHT 10 mm
ORIGIN_X 20 mm
END
END
)ICAD";

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

[[nodiscard]] auto request(double id, std::string method,
                           icad::json::Value params = icad::json::Value{
                               icad::json::Value::Object{}}) -> std::string {
    return icad::json::serialize(
               icad::json::Value{icad::json::Value::Object{{"jsonrpc", "2.0"},
                                                           {"id", id},
                                                           {"method", std::move(method)},
                                                           {"params", std::move(params)}}}) +
           '\n';
}

[[nodiscard]] auto tool_call(double id, std::string name, icad::json::Value::Object arguments)
    -> std::string {
    return request(
        id, "tools/call",
        icad::json::Value{icad::json::Value::Object{
            {"name", std::move(name)}, {"arguments", icad::json::Value{std::move(arguments)}}}});
}

} // namespace

auto main() -> int {
    const auto workspace = std::filesystem::current_path() / "mcp-test-workspace";
    std::filesystem::remove_all(workspace);
    std::filesystem::create_directories(workspace);
    const auto initial_revision = icad::document::revision_id(valid_source);
    std::string edited_source{valid_source};
    edited_source.replace(edited_source.find("PARAMETER width 10 mm"),
                          std::string_view{"PARAMETER width 10 mm"}.size(),
                          "PARAMETER width 12 mm");
    const auto edited_revision = icad::document::revision_id(edited_source);
    std::string batched_source{valid_source};
    batched_source.replace(batched_source.find("PARAMETER width 10 mm"),
                           std::string_view{"PARAMETER width 10 mm"}.size(),
                           "PARAMETER width 14 mm");
    const auto batched_revision = icad::document::revision_id(batched_source);

    std::string messages;
    messages += request(1, "server/discover");
    messages +=
        request(2, "initialize",
                icad::json::Value{icad::json::Value::Object{{"protocolVersion", "2025-11-25"}}});
    messages += request(3, "tools/list");
    messages += tool_call(4, "icad.compile", {{"source", "PROJECT broken\n$"}});
    messages += tool_call(5, "icad.inspect", {{"source", std::string{valid_source}}});
    messages += tool_call(16, "icad.topology", {{"source", std::string{valid_source}}});
    messages += tool_call(25, "icad.visualize", {{"source", std::string{valid_source}}});
    messages += tool_call(17, "icad.language", {});
    messages += tool_call(18, "icad.distance",
                          {{"source", std::string{query_source}},
                           {"firstBody", "first"}, {"secondBody", "second"}});
    messages += tool_call(19, "icad.interference", {{"source", std::string{query_source}}});
    messages += tool_call(20, "icad.section",
                          {{"source", std::string{query_source}}, {"px", 5.0}, {"py", 0.0},
                           {"pz", 0.0}, {"nx", 1.0}, {"ny", 0.0}, {"nz", 0.0}});
    messages += tool_call(21, "icad.agent.bootstrap",
                          {{"prompt", "Create an articulated robot arm with a gripper"}});
    messages += tool_call(22, "icad.agent.review", {{"source", std::string{valid_source}}});
    messages += tool_call(23, "icad.agent.create",
                          {{"prompt", "Create an articulated robot arm with a gripper"},
                           {"path", "projects/agent_robot.icad"},
                           {"expectedRevision", "absent"},
                           {"outputDirectory", "agent-artifacts"},
                           {"modelName", "agent_robot"}});
    messages += tool_call(6, "icad.build",
                          {{"source", std::string{valid_source}},
                           {"outputDirectory", "artifacts"},
                           {"modelName", "mcp_part"}});
    messages += tool_call(7, "icad.build",
                          {{"source", std::string{valid_source}},
                           {"outputDirectory", "../escape"},
                           {"modelName", "forbidden"}});
    messages += tool_call(8, "icad.project.write",
                          {{"path", "projects/part.icad"},
                           {"source", std::string{valid_source}},
                           {"expectedRevision", "absent"}});
    messages += tool_call(9, "icad.project.read", {{"path", "projects/part.icad"}});
    messages += tool_call(10, "icad.project.set_parameter",
                          {{"path", "projects/part.icad"},
                           {"parameter", "width"},
                           {"value", 12.0},
                           {"unit", "mm"},
                           {"expectedRevision", initial_revision}});
    messages += tool_call(11, "icad.project.set_parameter",
                          {{"path", "projects/part.icad"},
                           {"parameter", "width"},
                           {"value", 15.0},
                           {"unit", "mm"},
                           {"expectedRevision", initial_revision}});
    messages += tool_call(12, "icad.project.history", {{"path", "projects/part.icad"}});
    messages += tool_call(13, "icad.project.restore",
                          {{"path", "projects/part.icad"},
                           {"targetRevision", initial_revision},
                           {"expectedRevision", edited_revision}});
    messages += tool_call(14, "icad.project.build",
                          {{"path", "projects/part.icad"},
                           {"expectedRevision", initial_revision},
                           {"outputDirectory", "durable-artifacts"},
                           {"modelName", "durable_part"}});
    messages += tool_call(
        24, "icad.project.set_parameters",
        {{"path", "projects/part.icad"},
         {"edits", icad::json::Value{icad::json::Value::Array{icad::json::Value{
                       icad::json::Value::Object{{"parameter", "width"},
                                                {"value", 14.0},
                                                {"unit", "mm"}}}}}},
         {"expectedRevision", initial_revision}});
    messages += tool_call(15, "icad.unknown", {});
    messages += "{bad json\n";

    std::istringstream input{messages};
    std::ostringstream output;
    if (icad::mcp::run(input, output, workspace) != 0) {
        return fail("MCP server returned a failure status");
    }
    std::istringstream responses{output.str()};
    std::string line;
    std::vector<icad::json::Value> parsed_responses;
    while (std::getline(responses, line)) {
        auto parsed = icad::json::parse(line);
        if (!parsed.ok())
            return fail("MCP emitted invalid JSON");
        parsed_responses.push_back(std::move(*parsed.value));
    }
    if (parsed_responses.size() != 26) {
        return fail("MCP emitted an unexpected response count");
    }
    const auto serialized = output.str();
    if (!serialized.contains("\"supportedVersions\":[\"2026-07-28\"]") ||
        !serialized.contains("\"protocolVersion\":\"2025-11-25\"") ||
        !serialized.contains("\"name\":\"icad.build\"") ||
        !serialized.contains("\"name\":\"icad.project.restore\"") ||
        !serialized.contains("\"name\":\"icad.project.set_parameters\"") ||
        !serialized.contains("\"name\":\"icad.agent.create\"") ||
        !serialized.contains("\"name\":\"icad.topology\"") ||
        !serialized.contains("\"name\":\"icad.visualize\"") ||
        !serialized.contains("\"name\":\"icad.distance\"") ||
        !serialized.contains("\"schema\":\"icad.distance.v1\"") ||
        !serialized.contains("\"distanceMm\":10") ||
        !serialized.contains("\"schema\":\"icad.interference.v1\"") ||
        !serialized.contains("\"schema\":\"icad.section.v1\"") ||
        !serialized.contains("\"schema\":\"icad.topology.v1\"") ||
        !serialized.contains("\"schema\":\"icad.visual.snapshot.v1\"") ||
        !serialized.contains("\"schema\":\"icad.agent.bootstrap.v1\"") ||
        !serialized.contains("\"schema\":\"icad.agent.review.v1\"") ||
        !serialized.contains("\"schema\":\"icad.agent.create.v1\"") ||
        !serialized.contains("\"schema\":\"icad.agent.design-map.v1\"") ||
        !serialized.contains("\"selectedTemplate\":\"robotic_arm\"") ||
        !serialized.contains("\"editableParameters\"") ||
        !serialized.contains("\"name\":\"preview_forearm_axis\"") ||
        !serialized.contains("\"intent\":\"ROBOTIC_ARM\"") ||
        !serialized.contains("CIRCLE center-x center-y radius") || !serialized.contains("CW|CCW") ||
        !serialized.contains("POINT3 name") ||
        !serialized.contains("VECTOR name ROTATE") ||
        !serialized.contains("REVOLUTE, or PRISMATIC JOINT") ||
        !serialized.contains("ICAD-L0001") || !serialized.contains("\"volumeMm3\":6000") ||
        !serialized.contains("\"triangles\":12") || !serialized.contains("ICAD-MCP-PATH") ||
        !serialized.contains("ICAD-PROJECT-CONFLICT") || !serialized.contains(initial_revision) ||
        !serialized.contains(edited_revision) || !serialized.contains(batched_revision) ||
        !serialized.contains("unknown ICAD tool") ||
        !serialized.contains("Parse error at byte")) {
        return fail("MCP protocol responses are incomplete");
    }
    if (icad::document::read_source(workspace, "projects/part.icad").revision != batched_revision) {
        return fail("MCP batch parameter edit did not commit the requested source");
    }
    for (const std::string_view suffix :
         {".step", ".assembly.step", ".obj", ".stl", ".gltf", ".glb", ".3mf",
          ".scene.json", ".html", ".bom.json", ".manufacturing.json", ".drawing.svg",
          ".drawing.dxf", ".topology.json"}) {
        if (!std::filesystem::exists(workspace / "artifacts" /
                                     ("mcp_part" + std::string{suffix}))) {
            return fail("MCP build omitted an artifact");
        }
    }
    if (!std::filesystem::exists(workspace / "durable-artifacts" / "durable_part.assembly.step")) {
        return fail("MCP durable project build omitted its assembly");
    }
    if (!std::filesystem::exists(workspace / "agent-artifacts" / "agent_robot.assembly.step") ||
        !std::filesystem::exists(workspace / "projects" / "agent_robot.icad")) {
        return fail("MCP one-call agent creation omitted source or assembly artifacts");
    }
    if (std::filesystem::exists(workspace.parent_path() / "escape")) {
        return fail("MCP build escaped its configured workspace");
    }
    return 0;
}
