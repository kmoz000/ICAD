#include "icad/mcp/server.hpp"

#include "icad/ai/inspector.hpp"
#include "icad/agent/workflow.hpp"
#include "icad/cad/intersection.hpp"
#include "icad/cad/queries.hpp"
#include "icad/compiler/compiler.hpp"
#include "icad/constraints/validator.hpp"
#include "icad/document/source.hpp"
#include "icad/json/value.hpp"
#include "icad/manufacturing/validator.hpp"
#include "icad/materials/library.hpp"
#include "icad/project/builder.hpp"

#include <cctype>
#include <filesystem>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>

namespace icad::mcp {
namespace {

constexpr std::string_view current_protocol = "2026-07-28";
constexpr std::string_view server_version = "0.21.0";

[[nodiscard]] auto object(std::initializer_list<json::Value::Object::value_type> values)
    -> json::Value {
    return json::Value{json::Value::Object{values}};
}

[[nodiscard]] auto array(std::initializer_list<json::Value> values) -> json::Value {
    return json::Value{json::Value::Array{values}};
}

[[nodiscard]] auto rpc_response(const json::Value& id, std::string_view field, json::Value payload)
    -> json::Value {
    json::Value::Object response{{"jsonrpc", "2.0"}, {"id", id}};
    response.emplace(std::string{field}, std::move(payload));
    return json::Value{std::move(response)};
}

[[nodiscard]] auto rpc_error(const json::Value& id, int code, std::string message) -> json::Value {
    return rpc_response(
        id, "error",
        object({{"code", static_cast<double>(code)}, {"message", std::move(message)}}));
}

auto send(std::ostream& output, const json::Value& message) -> void {
    output << json::serialize(message) << '\n';
    output.flush();
}

[[nodiscard]] auto string_at(const json::Value* value, std::string_view key) -> const std::string* {
    return value == nullptr || value->find(key) == nullptr ? nullptr : value->find(key)->string();
}

[[nodiscard]] auto parsed_value(std::string_view serialized) -> json::Value {
    auto parsed = json::parse(serialized);
    return parsed.ok() ? std::move(*parsed.value) : object({{"internalError", true}});
}

[[nodiscard]] auto tool_result(json::Value structured, bool is_error = false) -> json::Value {
    const auto text = json::serialize(structured);
    return object({{"resultType", "complete"},
                   {"content", array({object({{"type", "text"}, {"text", text}})})},
                   {"structuredContent", std::move(structured)},
                   {"isError", is_error}});
}

[[nodiscard]] auto tool_error(std::string code, std::string message,
                              json::Value details = json::Value{nullptr}) -> json::Value {
    return tool_result(object({{"ok", false},
                               {"code", std::move(code)},
                               {"message", std::move(message)},
                               {"details", std::move(details)}}),
                       true);
}

[[nodiscard]] auto source_argument(const json::Value* arguments) -> const std::string* {
    return string_at(arguments, "source");
}

[[nodiscard]] auto number_at(const json::Value* value, std::string_view key) -> const double* {
    return value == nullptr || value->find(key) == nullptr ? nullptr : value->find(key)->number();
}

[[nodiscard]] auto compile_source(const json::Value* arguments) -> json::Value {
    const auto* source = source_argument(arguments);
    if (source == nullptr) {
        return tool_error("ICAD-MCP-ARGS", "required string argument 'source' is missing");
    }
    const auto diagnostics = parsed_value(ai::diagnostics_json(*source));
    const auto* ok = diagnostics.find("ok");
    return tool_result(diagnostics, ok == nullptr || ok->boolean() == nullptr || !*ok->boolean());
}

[[nodiscard]] auto agent_bootstrap(const json::Value* arguments) -> json::Value {
    const auto* prompt = string_at(arguments, "prompt");
    if (prompt == nullptr || prompt->empty())
        return tool_error("ICAD-MCP-ARGS", "required non-empty string argument 'prompt' is missing");
    return tool_result(parsed_value(agent::bootstrap(*prompt).json));
}

[[nodiscard]] auto agent_review(const json::Value* arguments) -> json::Value {
    const auto* source = source_argument(arguments);
    if (source == nullptr)
        return tool_error("ICAD-MCP-ARGS", "required string argument 'source' is missing");
    auto review = parsed_value(agent::review_json(*source));
    const auto* ready = review.find("ready");
    const bool failed = ready == nullptr || ready->boolean() == nullptr || !*ready->boolean();
    return tool_result(std::move(review), failed);
}

[[nodiscard]] auto inspect_source(const json::Value* arguments, bool metrics_only) -> json::Value {
    const auto* source = source_argument(arguments);
    if (source == nullptr) {
        return tool_error("ICAD-MCP-ARGS", "required string argument 'source' is missing");
    }
    const auto compilation = compiler::compile(*source);
    if (!compilation.ok()) {
        return tool_result(parsed_value(ai::diagnostics_json(*source)), true);
    }
    auto inspection = parsed_value(ai::project_json(*compilation.ir_project));
    if (!metrics_only) {
        return tool_result(std::move(inspection));
    }
    const auto* metrics = inspection.find("metrics");
    return metrics == nullptr ? tool_error("ICAD-MCP-INTERNAL", "metrics are unavailable")
                              : tool_result(*metrics);
}

[[nodiscard]] auto topology_source(const json::Value* arguments) -> json::Value {
    const auto* source = source_argument(arguments);
    if (source == nullptr) {
        return tool_error("ICAD-MCP-ARGS", "required string argument 'source' is missing");
    }
    const auto compilation = compiler::compile(*source);
    if (!compilation.ok()) {
        return tool_result(parsed_value(ai::diagnostics_json(*source)), true);
    }
    return tool_result(parsed_value(ai::topology_json(*compilation.ir_project)));
}

[[nodiscard]] auto distance_source(const json::Value* arguments) -> json::Value {
    const auto* source = source_argument(arguments);
    const auto* first = string_at(arguments, "firstBody");
    const auto* second = string_at(arguments, "secondBody");
    if (source == nullptr || first == nullptr || second == nullptr)
        return tool_error("ICAD-MCP-ARGS", "distance requires source, firstBody, and secondBody");
    const auto compilation = compiler::compile(*source);
    if (!compilation.ok())
        return tool_result(parsed_value(ai::diagnostics_json(*source)), true);
    const auto query = cad::exact_polyhedral_distance(*compilation.ir_project, *first, *second);
    if (!query.found)
        return tool_error("ICAD-MCP-QUERY", "distance requires two different existing bodies");
    return tool_result(object({
        {"schema", "icad.distance.v1"},
        {"representation", query.representation},
        {"firstBody", query.first_body},
        {"secondBody", query.second_body},
        {"distanceMm", query.distance_mm},
        {"firstPointMm", array({query.first_point.x, query.first_point.y, query.first_point.z})},
        {"secondPointMm", array({query.second_point.x, query.second_point.y, query.second_point.z})},
    }));
}

[[nodiscard]] auto interference_source(const json::Value* arguments) -> json::Value {
    const auto* source = source_argument(arguments);
    if (source == nullptr)
        return tool_error("ICAD-MCP-ARGS", "interference requires source");
    const auto compilation = compiler::compile(*source);
    if (!compilation.ok())
        return tool_result(parsed_value(ai::diagnostics_json(*source)), true);
    const auto analysis = cad::analyze_intersections(*compilation.ir_project,
                                                      compilation.ir_project->tolerance.linear_mm);
    json::Value::Array pairs;
    for (const auto& contact : analysis.body_contacts) {
        pairs.push_back(object({{"firstBody", contact.first_body},
                                {"secondBody", contact.second_body},
                                {"penetratingPartPairs",
                                 static_cast<double>(contact.penetrating_part_pairs)},
                                {"containedPartPairs",
                                 static_cast<double>(contact.contained_part_pairs)},
                                {"surfaceContactOnlyPartPairs",
                                 static_cast<double>(contact.surface_contact_only_part_pairs)}}));
    }
    return tool_result(object({
        {"schema", "icad.interference.v1"},
        {"representation", "polyhedralSolidClassification"},
        {"penetratingPartPairs", static_cast<double>(analysis.penetrating_part_pairs)},
        {"containedPartPairs", static_cast<double>(analysis.contained_part_pairs)},
        {"surfaceContactOnlyPartPairs",
         static_cast<double>(analysis.surface_contact_only_part_pairs)},
        {"bodyPairs", json::Value{std::move(pairs)}},
    }));
}

[[nodiscard]] auto section_source(const json::Value* arguments) -> json::Value {
    const auto* source = source_argument(arguments);
    const auto* px = number_at(arguments, "px");
    const auto* py = number_at(arguments, "py");
    const auto* pz = number_at(arguments, "pz");
    const auto* nx = number_at(arguments, "nx");
    const auto* ny = number_at(arguments, "ny");
    const auto* nz = number_at(arguments, "nz");
    if (source == nullptr || px == nullptr || py == nullptr || pz == nullptr || nx == nullptr ||
        ny == nullptr || nz == nullptr)
        return tool_error("ICAD-MCP-ARGS", "section requires source and px, py, pz, nx, ny, nz");
    const auto compilation = compiler::compile(*source);
    if (!compilation.ok())
        return tool_result(parsed_value(ai::diagnostics_json(*source)), true);
    const auto* body = string_at(arguments, "body");
    const auto query = cad::section(*compilation.ir_project, {{*px, *py, *pz}, {*nx, *ny, *nz}},
                                    body == nullptr ? std::string_view{} : std::string_view{*body});
    json::Value::Array segments;
    for (const auto& segment : query.segments) {
        segments.push_back(object({
            {"body", segment.body},
            {"part", segment.part},
            {"startMm", array({segment.segment.start.x, segment.segment.start.y,
                                segment.segment.start.z})},
            {"endMm", array({segment.segment.end.x, segment.segment.end.y,
                              segment.segment.end.z})},
        }));
    }
    return tool_result(object({{"schema", "icad.section.v1"},
                               {"representation", query.representation},
                               {"toleranceMm", query.tolerance_mm},
                               {"segments", json::Value{std::move(segments)}}}));
}

[[nodiscard]] auto validation_source(const json::Value* arguments) -> json::Value {
    const auto* source = source_argument(arguments);
    if (source == nullptr) {
        return tool_error("ICAD-MCP-ARGS", "required string argument 'source' is missing");
    }
    const auto compilation = compiler::compile(*source);
    if (!compilation.ok()) {
        return tool_result(parsed_value(ai::diagnostics_json(*source)), true);
    }
    const auto& project = *compilation.ir_project;
    const auto constraint_results = constraints::validate(project);
    json::Value::Array constraint_json;
    for (const auto& result : constraint_results) {
        constraint_json.push_back(object({{"name", result.name},
                                          {"passed", result.passed},
                                          {"required", result.required_mm},
                                          {"actual", result.actual_mm},
                                          {"unit", result.unit}}));
    }
    const auto manufacturing_report = manufacturing::validate(project);
    json::Value::Array issue_json;
    for (const auto& issue : manufacturing_report.issues) {
        const auto severity = issue.severity == manufacturing::Severity::error     ? "error"
                              : issue.severity == manufacturing::Severity::warning ? "warning"
                                                                                   : "information";
        issue_json.push_back(object({{"code", issue.code},
                                     {"severity", severity},
                                     {"subject", issue.subject},
                                     {"message", issue.message}}));
    }
    const bool passed = constraints::all_passed(constraint_results) && manufacturing_report.passed;
    return tool_result(
        object({{"ok", passed},
                {"constraints", json::Value{std::move(constraint_json)}},
                {"manufacturing", object({{"passed", manufacturing_report.passed},
                                          {"issues", json::Value{std::move(issue_json)}}})}}),
        !passed);
}

[[nodiscard]] auto material_library() -> json::Value {
    json::Value::Array presets;
    for (const auto& material : materials::all()) {
        presets.push_back(object({
            {"name", std::string{material.name}},
            {"baseColor", array({material.base_color[0], material.base_color[1],
                                 material.base_color[2], material.base_color[3]})},
            {"metallic", material.metallic},
            {"roughness", material.roughness},
            {"texture", std::string{material.texture}},
            {"textureSeed", static_cast<double>(material.texture_seed)},
        }));
    }
    return tool_result(object({{"presets", json::Value{std::move(presets)}}}));
}

[[nodiscard]] auto language_guide() -> json::Value {
    constexpr std::string_view guide =
        "ICAD source is line-oriented. Start with PROJECT name and UNITS mm. "
        "Declare PARAMETER name quantity and MATERIAL symbol PRESET. Spatial mechanism source "
        "uses ANGLE name quantity, POINT3 name X Y Z where coordinates may reference compatible "
        "parameters, normalized VECTOR name X Y Z, derived POINT3 name FROM point ALONG vector "
        "DISTANCE value, derived VECTOR name FROM point TO point, axis-angle VECTOR name ROTATE "
        "source AROUND axis BY angle, and POSE body AT point "
        "ROTATION X Y Z. "
        "INSTANCE name OF body AT point ROTATION X Y Z reuses one body definition as a named "
        "occurrence. Joint values solve instance delivery geometry through the parent chain. "
        "Declare FIXED, REVOLUTE, or PRISMATIC JOINT relationships with named points and axes; "
        "moving joints require VALUE and LIMIT. A PROFILE uses at least "
        "three POINT x-unit y-unit lines, START followed by LINE or ARC endpoint CENTER center "
        "CW|CCW statements and CLOSE, or one CIRCLE center-x center-y radius statement. "
        "BODY blocks contain optional MATERIAL symbol "
        "and FEATURE blocks. Solid TYPE values are BOX, CYLINDER, CONE, SPHERE, EXTRUDE, "
        "REVOLVE, SWEEP, LOFT, or FREEFORM. SWEEP uses PROFILE and a PATH of at least two "
        "named POINT3 values. LOFT adds TARGET_PROFILE and HEIGHT. FREEFORM adds TWIST and "
        "COUNT sections. Ordered operands use OPERATION UNION, CUT, or INTERSECT. Modeling "
        "modifiers are CHAMFER or FILLET with SELECT EDGE NEAREST point, LINEAR_PATTERN with "
        "DIRECTION vector, COUNT integer, and SPACING, and MIRROR with PLANE point NORMAL vector. "
        "EXTRUDE preserves analytic path arcs and circles. Full REVOLVE accepts line, arc, or "
        "circle profiles; curved results use validated faceted topology. Profiles are referenced "
        "with PROFILE name. Every physical "
        "property has "
        "a unit. A SKETCH block declares named POINT X Y values, optional FIXED anchors, and "
        "HORIZONTAL, VERTICAL, COINCIDENT, DISTANCE, or unsigned ANGLE constraints. Solved "
        "closed sketches are profiles; inspection reports coordinates, residual, and remaining "
        "degrees of freedom. A top-level TOLERANCE LINEAR length ANGULAR angle policy drives "
        "contact and geometric query classification. Constraints support MIN_DISTANCE bodies, "
        "COINCIDENT points with tolerance, PARALLEL or PERPENDICULAR vectors, and ANGLE_BETWEEN "
        "vectors. Constraint distances, "
        "tolerances, and angles may use compatible named parameters or ANGLE values. "
        "MATE supports FACE occurrence X_MIN|X_MAX|Y_MIN|Y_MAX|Z_MIN|Z_MAX pairs with OFFSET, "
        "or axis-aligned semantic EDGE selectors with TOLERANCE. SCENE blocks contain duration, "
        "FPS, background, BODY/CAMERA transform tracks, or JOINT tracks whose keyframes use "
        "VALUE within the joint limits. "
        "Close every block with END. Call icad.compile, then icad.validate, then icad.topology "
        "to discover stable exact face and edge IDs, and only then icad.build.";
    return tool_result(
        object({{"language", "ICAD"},
                {"version", std::string{server_version}},
                {"guide", std::string{guide}},
                {"examples", array({"examples/minimal.icad", "examples/advanced.icad",
                                    "examples/robotic_arm.icad",
                                    "examples/boolean_showcase.icad",
                                    "examples/modeling_tools.icad",
                                    "examples/advanced_surfaces.icad",
                                    "examples/constrained_sketch.icad",
                                    "examples/assembly_instances.icad",
                                    "examples/assembly_semantics.icad"})}}));
}

[[nodiscard]] auto safe_relative_directory(const std::filesystem::path& workspace,
                                           std::string_view requested, std::string& error_message)
    -> std::filesystem::path {
    const std::filesystem::path relative{requested};
    if (relative.empty() || relative.is_absolute()) {
        error_message = "outputDirectory must be a non-empty relative path";
        return {};
    }
    for (const auto& component : relative) {
        if (component == "..") {
            error_message = "outputDirectory cannot traverse outside the workspace";
            return {};
        }
    }
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(workspace, error);
    if (error) {
        error_message = "cannot resolve MCP workspace: " + error.message();
        return {};
    }
    const auto candidate = std::filesystem::weakly_canonical(root / relative, error);
    if (error) {
        error_message = "cannot resolve outputDirectory: " + error.message();
        return {};
    }
    const auto back_to_root = std::filesystem::relative(candidate, root, error);
    if (error || back_to_root.empty() || back_to_root.is_absolute()) {
        error_message = "outputDirectory is outside the MCP workspace";
        return {};
    }
    for (const auto& component : back_to_root) {
        if (component == "..") {
            error_message = "outputDirectory resolves outside the MCP workspace";
            return {};
        }
    }
    return candidate;
}

[[nodiscard]] auto safe_model_name(std::string_view name) -> bool {
    if (name.empty() || name.size() > 128 || !std::isalnum(static_cast<unsigned char>(name[0]))) {
        return false;
    }
    for (const char character : name) {
        if (!std::isalnum(static_cast<unsigned char>(character)) && character != '-' &&
            character != '_' && character != '.') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto build_source(const json::Value* arguments,
                                const std::filesystem::path& workspace) -> json::Value {
    const auto* source = source_argument(arguments);
    const auto* output_directory = string_at(arguments, "outputDirectory");
    const auto* model_name = string_at(arguments, "modelName");
    if (source == nullptr || output_directory == nullptr || model_name == nullptr) {
        return tool_error("ICAD-MCP-ARGS",
                          "build requires string arguments source, outputDirectory, and modelName");
    }
    if (!safe_model_name(*model_name)) {
        return tool_error("ICAD-MCP-PATH", "modelName must start with an alphanumeric character "
                                           "and contain only alphanumerics, '.', '-', or '_'");
    }
    std::string path_error;
    const auto destination = safe_relative_directory(workspace, *output_directory, path_error);
    if (destination.empty()) {
        return tool_error("ICAD-MCP-PATH", std::move(path_error));
    }
    const auto compilation = compiler::compile(*source);
    if (!compilation.ok()) {
        return tool_result(parsed_value(ai::diagnostics_json(*source)), true);
    }
    const auto built = project::build(*compilation.ir_project, destination, *model_name);
    if (!built.success) {
        return tool_error("ICAD-MCP-BUILD", built.message);
    }
    json::Value::Array artifacts;
    std::error_code error;
    for (const auto& artifact : built.artifacts) {
        const auto relative = std::filesystem::relative(artifact.path, workspace, error);
        if (error) {
            return tool_error("ICAD-MCP-PATH", "cannot report an artifact path");
        }
        artifacts.push_back(object({{"kind", artifact.kind},
                                    {"mediaType", artifact.media_type},
                                    {"path", relative.generic_string()},
                                    {"bytes", static_cast<double>(artifact.bytes)}}));
    }
    return tool_result(object({
        {"ok", true},
        {"components", static_cast<double>(built.components)},
        {"solids", static_cast<double>(built.solids)},
        {"vertices", static_cast<double>(built.vertices)},
        {"triangles", static_cast<double>(built.triangles)},
        {"topologyVertices", static_cast<double>(built.topology_vertices)},
        {"topologyEdges", static_cast<double>(built.topology_edges)},
        {"topologyFaces", static_cast<double>(built.topology_faces)},
        {"materials", static_cast<double>(built.materials)},
        {"scenes", static_cast<double>(built.scenes)},
        {"keyframes", static_cast<double>(built.keyframes)},
        {"artifacts", json::Value{std::move(artifacts)}},
    }));
}

[[nodiscard]] auto status_code(document::SourceStatus status) -> std::string_view {
    switch (status) {
    case document::SourceStatus::ok:
        return "ICAD-PROJECT-OK";
    case document::SourceStatus::path_error:
        return "ICAD-PROJECT-PATH";
    case document::SourceStatus::not_found:
        return "ICAD-PROJECT-NOT-FOUND";
    case document::SourceStatus::conflict:
        return "ICAD-PROJECT-CONFLICT";
    case document::SourceStatus::invalid_source:
        return "ICAD-PROJECT-INVALID";
    case document::SourceStatus::io_error:
        return "ICAD-PROJECT-IO";
    }
    return "ICAD-PROJECT-IO";
}

[[nodiscard]] auto diagnostic_values(const std::vector<compiler::Diagnostic>& diagnostics)
    -> json::Value {
    json::Value::Array values;
    for (const auto& diagnostic : diagnostics) {
        values.push_back(object({{"code", diagnostic.code},
                                 {"message", diagnostic.message},
                                 {"line", static_cast<double>(diagnostic.location.line)},
                                 {"column", static_cast<double>(diagnostic.location.column)}}));
    }
    return json::Value{std::move(values)};
}

[[nodiscard]] auto project_read(const json::Value* arguments,
                                const std::filesystem::path& workspace) -> json::Value {
    const auto* path = string_at(arguments, "path");
    if (path == nullptr) {
        return tool_error("ICAD-MCP-ARGS", "project read requires string argument 'path'");
    }
    const auto snapshot = document::read_source(workspace, *path);
    if (!snapshot.ok()) {
        return tool_error(std::string{status_code(snapshot.status)}, snapshot.message,
                          object({{"path", *path}}));
    }
    return tool_result(object({{"ok", true},
                               {"path", snapshot.path.generic_string()},
                               {"revision", snapshot.revision},
                               {"source", snapshot.source}}));
}

[[nodiscard]] auto change_result(const document::SourceChange& change) -> json::Value {
    const auto details = object({{"path", change.path.generic_string()},
                                 {"previousRevision", change.previous_revision},
                                 {"revision", change.revision},
                                 {"diagnostics", diagnostic_values(change.diagnostics)}});
    if (!change.ok()) {
        return tool_error(std::string{status_code(change.status)}, change.message, details);
    }
    return tool_result(object({{"ok", true},
                               {"message", change.message},
                               {"path", change.path.generic_string()},
                               {"previousRevision", change.previous_revision},
                               {"revision", change.revision}}));
}

[[nodiscard]] auto project_write(const json::Value* arguments,
                                 const std::filesystem::path& workspace) -> json::Value {
    const auto* path = string_at(arguments, "path");
    const auto* source = source_argument(arguments);
    const auto* expected = string_at(arguments, "expectedRevision");
    if (path == nullptr || source == nullptr || expected == nullptr) {
        return tool_error("ICAD-MCP-ARGS",
                          "project write requires path, source, and expectedRevision strings");
    }
    return change_result(document::write_source(workspace, *path, *source, *expected));
}

[[nodiscard]] auto project_set_parameter(const json::Value* arguments,
                                         const std::filesystem::path& workspace) -> json::Value {
    const auto* path = string_at(arguments, "path");
    const auto* parameter = string_at(arguments, "parameter");
    const auto* unit = string_at(arguments, "unit");
    const auto* expected = string_at(arguments, "expectedRevision");
    const auto* value_node = arguments == nullptr ? nullptr : arguments->find("value");
    const auto* value = value_node == nullptr ? nullptr : value_node->number();
    if (path == nullptr || parameter == nullptr || unit == nullptr || expected == nullptr ||
        value == nullptr) {
        return tool_error(
            "ICAD-MCP-ARGS",
            "parameter edit requires path, parameter, numeric value, unit, and expectedRevision");
    }
    return change_result(
        document::set_parameter(workspace, *path, *parameter, *value, *unit, *expected));
}

[[nodiscard]] auto project_set_parameters(const json::Value* arguments,
                                          const std::filesystem::path& workspace) -> json::Value {
    const auto* path = string_at(arguments, "path");
    const auto* expected = string_at(arguments, "expectedRevision");
    const auto* edits_node = arguments == nullptr ? nullptr : arguments->find("edits");
    const auto* edits_array = edits_node == nullptr ? nullptr : edits_node->array();
    if (path == nullptr || expected == nullptr || edits_array == nullptr || edits_array->empty()) {
        return tool_error("ICAD-MCP-ARGS",
                          "batch parameter edit requires path, expectedRevision, and non-empty edits");
    }
    std::vector<document::ParameterEdit> edits;
    edits.reserve(edits_array->size());
    for (const auto& item : *edits_array) {
        const auto* name = string_at(&item, "parameter");
        const auto* unit = string_at(&item, "unit");
        const auto* value = number_at(&item, "value");
        if (name == nullptr || unit == nullptr || value == nullptr) {
            return tool_error("ICAD-MCP-ARGS",
                              "each batch edit requires parameter, numeric value, and unit");
        }
        edits.push_back({*name, *value, *unit});
    }
    return change_result(document::set_parameters(workspace, *path, edits, *expected));
}

[[nodiscard]] auto project_history(const json::Value* arguments,
                                   const std::filesystem::path& workspace) -> json::Value {
    const auto* path = string_at(arguments, "path");
    if (path == nullptr) {
        return tool_error("ICAD-MCP-ARGS", "project history requires string argument 'path'");
    }
    const auto history = document::source_history(workspace, *path);
    if (!history.ok()) {
        return tool_error(std::string{status_code(history.status)}, history.message);
    }
    json::Value::Array revisions;
    for (const auto& revision : history.revisions)
        revisions.emplace_back(revision);
    return tool_result(
        object({{"ok", true}, {"path", *path}, {"revisions", json::Value{std::move(revisions)}}}));
}

[[nodiscard]] auto project_restore(const json::Value* arguments,
                                   const std::filesystem::path& workspace) -> json::Value {
    const auto* path = string_at(arguments, "path");
    const auto* target = string_at(arguments, "targetRevision");
    const auto* expected = string_at(arguments, "expectedRevision");
    if (path == nullptr || target == nullptr || expected == nullptr) {
        return tool_error("ICAD-MCP-ARGS",
                          "project restore requires path, targetRevision, and expectedRevision");
    }
    return change_result(document::restore_source(workspace, *path, *target, *expected));
}

[[nodiscard]] auto project_build(const json::Value* arguments,
                                 const std::filesystem::path& workspace) -> json::Value {
    const auto* path = string_at(arguments, "path");
    const auto* expected = string_at(arguments, "expectedRevision");
    const auto* output = string_at(arguments, "outputDirectory");
    const auto* name = string_at(arguments, "modelName");
    if (path == nullptr || expected == nullptr || output == nullptr || name == nullptr) {
        return tool_error(
            "ICAD-MCP-ARGS",
            "project build requires path, expectedRevision, outputDirectory, and modelName");
    }
    const auto snapshot = document::read_source(workspace, *path);
    if (!snapshot.ok()) {
        return tool_error(std::string{status_code(snapshot.status)}, snapshot.message);
    }
    if (snapshot.revision != *expected) {
        return tool_error("ICAD-PROJECT-CONFLICT",
                          "expected revision does not match current source",
                          object({{"currentRevision", snapshot.revision}}));
    }
    const json::Value call_arguments =
        object({{"source", snapshot.source}, {"outputDirectory", *output}, {"modelName", *name}});
    return build_source(&call_arguments, workspace);
}

[[nodiscard]] auto agent_create(const json::Value* arguments,
                                const std::filesystem::path& workspace) -> json::Value {
    const auto* prompt = string_at(arguments, "prompt");
    const auto* path = string_at(arguments, "path");
    const auto* expected = string_at(arguments, "expectedRevision");
    const auto* output = string_at(arguments, "outputDirectory");
    const auto* name = string_at(arguments, "modelName");
    if (prompt == nullptr || prompt->empty() || path == nullptr || expected == nullptr ||
        output == nullptr || name == nullptr) {
        return tool_error("ICAD-MCP-ARGS", "agent create requires non-empty prompt, path, "
                                           "expectedRevision, outputDirectory, and modelName");
    }

    auto bootstrapped = agent::bootstrap(*prompt);
    const auto bootstrap_plan = parsed_value(bootstrapped.json);
    const auto plan_value = [&](std::string_view key) -> json::Value {
        const auto* value = bootstrap_plan.find(key);
        return value == nullptr ? json::Value{nullptr} : *value;
    };
    const auto brief = object({
        {"promptUnderstanding", plan_value("promptUnderstanding")},
        {"acceptanceCriteria", plan_value("acceptanceCriteria")},
        {"editableParameters", plan_value("editableParameters")},
        {"editableAngles", plan_value("editableAngles")},
        {"parameterStrategy", plan_value("parameterStrategy")},
    });
    auto review = parsed_value(agent::review_json(bootstrapped.source));
    const auto* ready = review.find("ready");
    if (ready == nullptr || ready->boolean() == nullptr || !*ready->boolean()) {
        return tool_result(object({{"ok", false},
                                   {"schema", "icad.agent.create.v1"},
                                   {"stage", "review"},
                                   {"brief", brief},
                                   {"review", std::move(review)}}),
                           true);
    }

    const auto change =
        document::write_source(workspace, *path, bootstrapped.source, *expected);
    if (!change.ok())
        return change_result(change);

    const json::Value build_arguments = object({{"source", bootstrapped.source},
                                                 {"outputDirectory", *output},
                                                 {"modelName", *name}});
    auto build = build_source(&build_arguments, workspace);
    const auto* build_content = build.find("structuredContent");
    const auto* build_error = build.find("isError");
    if (build_content == nullptr ||
        (build_error != nullptr && build_error->boolean() != nullptr && *build_error->boolean())) {
        return tool_result(object({{"ok", false},
                                   {"schema", "icad.agent.create.v1"},
                                   {"stage", "build"},
                                   {"sourceCommitted", true},
                                   {"path", change.path.generic_string()},
                                   {"revision", change.revision},
                                   {"brief", brief},
                                   {"build", build_content == nullptr ? json::Value{nullptr}
                                                                        : *build_content}}),
                           true);
    }

    return tool_result(object({
        {"ok", true},
        {"schema", "icad.agent.create.v1"},
        {"intent", std::string{agent::intent_name(bootstrapped.intent)}},
        {"expectedModelIterations",
         bootstrapped.intent == agent::DesignIntent::generic_part ? 2.0 : 1.0},
        {"path", change.path.generic_string()},
        {"revision", change.revision},
        {"brief", brief},
        {"source", std::move(bootstrapped.source)},
        {"review", std::move(review)},
        {"build", *build_content},
    }));
}

[[nodiscard]] auto tools_catalog() -> json::Value {
    constexpr std::string_view catalog = R"JSON([
{"name":"icad.language","title":"ICAD language guide","description":"Return the compact ICAD authoring and workflow guide.","inputSchema":{"type":"object","additionalProperties":false},"annotations":{"readOnlyHint":true}},
{"name":"icad.agent.bootstrap","title":"Bootstrap design from prompt","description":"Classify a short design prompt and return a complete compiler-valid ICAD source template, acceptance criteria, parameter strategy, and shortest tool workflow.","inputSchema":{"type":"object","properties":{"prompt":{"type":"string","minLength":1}},"required":["prompt"],"additionalProperties":false},"annotations":{"readOnlyHint":true}},
{"name":"icad.agent.create","title":"Create complete design from prompt","description":"In one call, classify a prompt, compile and review a maintained parametric design, commit its source with optimistic concurrency, and build the complete artifact package.","inputSchema":{"type":"object","properties":{"prompt":{"type":"string","minLength":1},"path":{"type":"string"},"expectedRevision":{"type":"string"},"outputDirectory":{"type":"string"},"modelName":{"type":"string","pattern":"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"}},"required":["prompt","path","expectedRevision","outputDirectory","modelName"],"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false}},
{"name":"icad.agent.review","title":"Review design readiness","description":"Compile and perform constraints, manufacturing, topology, metrics, and interference review in one call with focused next actions.","inputSchema":{"type":"object","properties":{"source":{"type":"string"}},"required":["source"],"additionalProperties":false},"annotations":{"readOnlyHint":true}},
{"name":"icad.materials","title":"ICAD material presets","description":"List deterministic embedded PBR material presets and texture metadata.","inputSchema":{"type":"object","additionalProperties":false},"annotations":{"readOnlyHint":true}},
{"name":"icad.compile","title":"Compile ICAD source","description":"Lex, parse, resolve, type-check, and semantically validate ICAD source with stable diagnostics.","inputSchema":{"type":"object","properties":{"source":{"type":"string","description":"Complete ICAD source text"}},"required":["source"],"additionalProperties":false},"annotations":{"readOnlyHint":true}},
{"name":"icad.validate","title":"Validate engineering rules","description":"Compile ICAD source and evaluate geometric constraints and manufacturing rules.","inputSchema":{"type":"object","properties":{"source":{"type":"string"}},"required":["source"],"additionalProperties":false},"annotations":{"readOnlyHint":true}},
{"name":"icad.measure","title":"Measure ICAD design","description":"Return surface area, volume, and world bounds for compiled ICAD source.","inputSchema":{"type":"object","properties":{"source":{"type":"string"}},"required":["source"],"additionalProperties":false},"annotations":{"readOnlyHint":true}},
{"name":"icad.inspect","title":"Inspect ICAD design","description":"Return canonical design counts, revision fingerprint, body ownership, metrics, and validation state.","inputSchema":{"type":"object","properties":{"source":{"type":"string"}},"required":["source"],"additionalProperties":false},"annotations":{"readOnlyHint":true}},
{"name":"icad.topology","title":"Inspect exact ICAD topology","description":"Return stable solid, shell, face, edge, and vertex IDs with analytic curve and surface kinds for agent references.","inputSchema":{"type":"object","properties":{"source":{"type":"string"}},"required":["source"],"additionalProperties":false},"annotations":{"readOnlyHint":true}},
{"name":"icad.distance","title":"Query body distance","description":"Return exact-polyhedral minimum distance and closest points between two named bodies.","inputSchema":{"type":"object","properties":{"source":{"type":"string"},"firstBody":{"type":"string"},"secondBody":{"type":"string"}},"required":["source","firstBody","secondBody"],"additionalProperties":false},"annotations":{"readOnlyHint":true}},
{"name":"icad.interference","title":"Query assembly interference","description":"Classify penetrating, contained, and surface-only body contacts.","inputSchema":{"type":"object","properties":{"source":{"type":"string"}},"required":["source"],"additionalProperties":false},"annotations":{"readOnlyHint":true}},
{"name":"icad.section","title":"Query plane section","description":"Intersect a plane with the complete design or one named body.","inputSchema":{"type":"object","properties":{"source":{"type":"string"},"px":{"type":"number"},"py":{"type":"number"},"pz":{"type":"number"},"nx":{"type":"number"},"ny":{"type":"number"},"nz":{"type":"number"},"body":{"type":"string"}},"required":["source","px","py","pz","nx","ny","nz"],"additionalProperties":false},"annotations":{"readOnlyHint":true}},
{"name":"icad.build","title":"Build ICAD artifact package","description":"Compile and atomically stage a complete CAD, mesh, viewer, BOM, manufacturing, and drawing package inside the configured workspace.","inputSchema":{"type":"object","properties":{"source":{"type":"string"},"outputDirectory":{"type":"string","description":"Workspace-relative output directory"},"modelName":{"type":"string","pattern":"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"}},"required":["source","outputDirectory","modelName"],"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false}}
,{"name":"icad.project.read","title":"Read durable ICAD project","description":"Read workspace-confined ICAD source with an exact hexadecimal revision for optimistic concurrency.","inputSchema":{"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":false},"annotations":{"readOnlyHint":true}}
,{"name":"icad.project.write","title":"Commit durable ICAD project","description":"Compiler-validate and atomically commit complete ICAD source when expectedRevision matches; use absent to create.","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"source":{"type":"string"},"expectedRevision":{"type":"string"}},"required":["path","source","expectedRevision"],"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false}}
,{"name":"icad.project.set_parameter","title":"Edit ICAD parameter","description":"Atomically update one named parameter with optimistic concurrency while preserving surrounding source.","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"parameter":{"type":"string"},"value":{"type":"number"},"unit":{"type":"string"},"expectedRevision":{"type":"string"}},"required":["path","parameter","value","unit","expectedRevision"],"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false}}
,{"name":"icad.project.set_parameters","title":"Edit multiple ICAD parameters","description":"Atomically update multiple named parameters in one compiler-validated optimistic-concurrency transaction.","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"edits":{"type":"array","minItems":1,"items":{"type":"object","properties":{"parameter":{"type":"string"},"value":{"type":"number"},"unit":{"type":"string"}},"required":["parameter","value","unit"],"additionalProperties":false}},"expectedRevision":{"type":"string"}},"required":["path","edits","expectedRevision"],"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false}}
,{"name":"icad.project.history","title":"List ICAD revisions","description":"List immutable archived revisions for a durable project.","inputSchema":{"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":false},"annotations":{"readOnlyHint":true}}
,{"name":"icad.project.restore","title":"Restore ICAD revision","description":"Restore an archived compiler-valid source revision when expectedRevision matches the current project.","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"targetRevision":{"type":"string"},"expectedRevision":{"type":"string"}},"required":["path","targetRevision","expectedRevision"],"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false}}
,{"name":"icad.project.build","title":"Build durable ICAD project","description":"Build a durable project only when its exact expectedRevision matches, preventing stale-agent output.","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"expectedRevision":{"type":"string"},"outputDirectory":{"type":"string"},"modelName":{"type":"string","pattern":"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"}},"required":["path","expectedRevision","outputDirectory","modelName"],"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false}}
])JSON";
    auto parsed = json::parse(catalog);
    return parsed.ok() ? std::move(*parsed.value) : json::Value{json::Value::Array{}};
}

[[nodiscard]] auto call_tool(std::string_view name, const json::Value* arguments,
                             const std::filesystem::path& workspace) -> std::optional<json::Value> {
    if (name == "icad.language")
        return language_guide();
    if (name == "icad.agent.bootstrap")
        return agent_bootstrap(arguments);
    if (name == "icad.agent.create")
        return agent_create(arguments, workspace);
    if (name == "icad.agent.review")
        return agent_review(arguments);
    if (name == "icad.materials")
        return material_library();
    if (name == "icad.compile")
        return compile_source(arguments);
    if (name == "icad.validate")
        return validation_source(arguments);
    if (name == "icad.measure")
        return inspect_source(arguments, true);
    if (name == "icad.inspect")
        return inspect_source(arguments, false);
    if (name == "icad.topology")
        return topology_source(arguments);
    if (name == "icad.distance")
        return distance_source(arguments);
    if (name == "icad.interference")
        return interference_source(arguments);
    if (name == "icad.section")
        return section_source(arguments);
    if (name == "icad.build")
        return build_source(arguments, workspace);
    if (name == "icad.project.read")
        return project_read(arguments, workspace);
    if (name == "icad.project.write")
        return project_write(arguments, workspace);
    if (name == "icad.project.set_parameter") {
        return project_set_parameter(arguments, workspace);
    }
    if (name == "icad.project.set_parameters")
        return project_set_parameters(arguments, workspace);
    if (name == "icad.project.history")
        return project_history(arguments, workspace);
    if (name == "icad.project.restore")
        return project_restore(arguments, workspace);
    if (name == "icad.project.build")
        return project_build(arguments, workspace);
    return std::nullopt;
}

[[nodiscard]] auto handle(const json::Value& request, const std::filesystem::path& workspace)
    -> std::optional<json::Value> {
    const auto* id = request.find("id");
    const json::Value null_id{nullptr};
    const auto* response_id = id == nullptr ? &null_id : id;
    const auto* version = string_at(&request, "jsonrpc");
    const auto* method = string_at(&request, "method");
    if (version == nullptr || *version != "2.0" || method == nullptr) {
        return rpc_error(*response_id, -32600, "Invalid Request");
    }
    if (id == nullptr) {
        return std::nullopt;
    }
    if (*method == "server/discover") {
        return rpc_response(
            *id, "result",
            object(
                {{"resultType", "complete"},
                 {"supportedVersions", array({std::string{current_protocol}})},
                 {"capabilities", object({{"tools", object({})}})},
                 {"_meta",
                  object({{"io.modelcontextprotocol/serverInfo",
                           object({{"name", "icad"}, {"version", std::string{server_version}}})}})},
                 {"instructions", "Compile and validate before building. All build paths are "
                                  "confined to the configured workspace."},
                 {"ttlMs", 3600000.0},
                 {"cacheScope", "public"}}));
    }
    if (*method == "initialize") {
        const auto* params = request.find("params");
        const auto* requested = string_at(params, "protocolVersion");
        return rpc_response(
            *id, "result",
            object(
                {{"protocolVersion", requested == nullptr ? "2025-11-25" : *requested},
                 {"capabilities", object({{"tools", object({{"listChanged", false}})}})},
                 {"serverInfo",
                  object({{"name", "icad"}, {"version", std::string{server_version}}})},
                 {"instructions", "Compile and validate ICAD source before building artifacts."}}));
    }
    if (*method == "ping") {
        return rpc_response(*id, "result", object({}));
    }
    if (*method == "tools/list") {
        return rpc_response(*id, "result",
                            object({{"resultType", "complete"},
                                    {"tools", tools_catalog()},
                                    {"ttlMs", 300000.0},
                                    {"cacheScope", "public"}}));
    }
    if (*method == "tools/call") {
        const auto* params = request.find("params");
        const auto* name = string_at(params, "name");
        if (name == nullptr) {
            return rpc_error(*id, -32602, "tools/call requires params.name");
        }
        const auto* arguments = params == nullptr ? nullptr : params->find("arguments");
        auto result = call_tool(*name, arguments, workspace);
        if (!result) {
            return rpc_error(*id, -32602, "unknown ICAD tool: " + *name);
        }
        return rpc_response(*id, "result", std::move(*result));
    }
    return rpc_error(*id, -32601, "Method not found");
}

} // namespace

auto run(std::istream& input, std::ostream& output, const std::filesystem::path& workspace) -> int {
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        const auto parsed = json::parse(line);
        if (!parsed.ok()) {
            send(output, rpc_error(json::Value{nullptr}, -32700,
                                   "Parse error at byte " + std::to_string(parsed.offset)));
            continue;
        }
        const auto response = handle(*parsed.value, workspace);
        if (response)
            send(output, *response);
    }
    return 0;
}

} // namespace icad::mcp
