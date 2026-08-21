#include "icad/project/builder.hpp"

#include "icad/ai/inspector.hpp"
#include "icad/cad/topology.hpp"
#include "icad/constraints/validator.hpp"
#include "icad/document/exporter.hpp"
#include "icad/document/revision.hpp"
#include "icad/drawings/exporter.hpp"
#include "icad/exchange/exporter.hpp"
#include "icad/manufacturing/validator.hpp"
#include "icad/scene/exporter.hpp"

#include <array>
#include <atomic>
#include <fstream>
#include <system_error>

namespace icad::project {
namespace {

struct ArtifactSpec {
    std::string_view suffix;
    std::string_view kind;
    std::string_view media_type;
};

constexpr std::array artifact_specs{
    ArtifactSpec{".step", "step", "model/step"},
    ArtifactSpec{".assembly.step", "step-assembly", "model/step"},
    ArtifactSpec{".obj", "obj", "model/obj"},
    ArtifactSpec{".stl", "stl", "model/stl"},
    ArtifactSpec{".gltf", "gltf", "model/gltf+json"},
    ArtifactSpec{".glb", "glb", "model/gltf-binary"},
    ArtifactSpec{".3mf", "3mf", "model/3mf"},
    ArtifactSpec{".scene.json", "scene", "application/json"},
    ArtifactSpec{".viewer.js", "viewer-data", "text/javascript"},
    ArtifactSpec{".html", "viewer", "text/html"},
    ArtifactSpec{".bom.json", "bom", "application/json"},
    ArtifactSpec{".manufacturing.json", "manufacturing", "application/json"},
    ArtifactSpec{".drawing.svg", "drawing", "image/svg+xml"},
    ArtifactSpec{".drawing.dxf", "drawing-dxf", "image/vnd.dxf"},
    ArtifactSpec{".topology.json", "topology", "application/json"},
};

class StageCleanup {
  public:
    explicit StageCleanup(std::filesystem::path path) : path_{std::move(path)} {}
    StageCleanup(const StageCleanup&) = delete;
    auto operator=(const StageCleanup&) -> StageCleanup& = delete;
    ~StageCleanup() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

  private:
    std::filesystem::path path_;
};

[[nodiscard]] auto make_stage(const std::filesystem::path& output, std::uint64_t fingerprint_value,
                              std::error_code& error) -> std::filesystem::path {
    static std::atomic_uint64_t sequence{0};
    for (std::size_t attempt = 0; attempt < 100; ++attempt) {
        const auto number = sequence.fetch_add(1, std::memory_order_relaxed);
        const auto candidate = output / (".icad-stage-" + std::to_string(fingerprint_value) + "-" +
                                         std::to_string(number));
        error.clear();
        if (std::filesystem::create_directory(candidate, error)) {
            return candidate;
        }
        if (error) {
            return {};
        }
    }
    error = std::make_error_code(std::errc::file_exists);
    return {};
}

[[nodiscard]] auto fail(std::string message) -> BuildResult {
    BuildResult result;
    result.message = std::move(message);
    return result;
}

} // namespace

auto build(const compiler::ir::Project& project, const std::filesystem::path& output_directory,
           std::string_view model_name) -> BuildResult {
    if (model_name.empty() || model_name == "." || model_name == ".." ||
        std::filesystem::path{model_name}.filename() != model_name) {
        return fail("model name must be one safe filename component");
    }
    const auto constraint_results = constraints::validate(project);
    if (!constraints::all_passed(constraint_results)) {
        for (const auto& constraint : constraint_results) {
            if (!constraint.passed) {
                return fail("constraint '" + constraint.name + "' failed: required " +
                            std::to_string(constraint.required_mm) + " " + constraint.unit +
                            ", actual " + std::to_string(constraint.actual_mm) + " " +
                            constraint.unit);
            }
        }
    }
    const auto manufacturing_report = manufacturing::validate(project);
    if (!manufacturing_report.passed) {
        return fail("manufacturing validation failed");
    }
    const auto topology = cad::build_topology(project);
    const auto topology_validation = cad::validate_topology(topology);
    if (!topology_validation.valid()) {
        const auto& issue = topology_validation.issues.front();
        return fail("topology validation failed: " + issue.code + " " + issue.entity + ": " +
                    issue.message);
    }

    std::error_code error;
    std::filesystem::create_directories(output_directory, error);
    if (error) {
        return fail("cannot create output directory: " + error.message());
    }
    const auto stage = make_stage(output_directory, document::fingerprint(project), error);
    if (error || stage.empty()) {
        return fail("cannot create artifact staging directory: " + error.message());
    }
    const StageCleanup cleanup{stage};
    const auto stage_base = stage / std::string{model_name};

    const auto step = exchange::export_project(project, stage_base.string() + ".step");
    if (!step.success)
        return fail("STEP export failed: " + step.message);
    const auto assembly =
        exchange::export_assembly_step(project, stage_base.string() + ".assembly.step");
    if (!assembly.success)
        return fail("STEP assembly export failed: " + assembly.message);
    const auto object = exchange::export_project(project, stage_base.string() + ".obj");
    if (!object.success)
        return fail("OBJ export failed: " + object.message);
    const auto stl = exchange::export_project(project, stage_base.string() + ".stl");
    if (!stl.success)
        return fail("STL export failed: " + stl.message);
    const auto gltf = exchange::export_project(project, stage_base.string() + ".gltf");
    if (!gltf.success)
        return fail("glTF export failed: " + gltf.message);
    const auto glb = exchange::export_project(project, stage_base.string() + ".glb");
    if (!glb.success)
        return fail("GLB export failed: " + glb.message);
    const auto three_mf = exchange::export_project(project, stage_base.string() + ".3mf");
    if (!three_mf.success)
        return fail("3MF export failed: " + three_mf.message);
    const auto web = scene::export_web_bundle(project, stage_base);
    if (!web.success)
        return fail("web bundle export failed: " + web.message);
    const auto bom = document::write_bom(project, stage_base.string() + ".bom.json");
    if (!bom.success)
        return fail("BOM export failed: " + bom.message);
    if (!manufacturing::write_report(project, stage_base.string() + ".manufacturing.json")) {
        return fail("manufacturing report export failed");
    }
    const auto drawing = drawings::write_svg(project, stage_base.string() + ".drawing.svg");
    if (!drawing.success)
        return fail("drawing export failed: " + drawing.message);
    const auto dxf = drawings::write_dxf(project, stage_base.string() + ".drawing.dxf");
    if (!dxf.success)
        return fail("DXF drawing export failed: " + dxf.message);
    {
        std::ofstream topology_output{stage_base.string() + ".topology.json",
                                      std::ios::binary | std::ios::trunc};
        topology_output << ai::topology_json(project) << '\n';
        if (!topology_output)
            return fail("topology artifact export failed");
    }

    BuildResult result;
    result.components = project.bodies.size() + project.instances.size();
    result.solids = step.objects;
    result.vertices = object.vertices;
    result.triangles = object.triangles;
    result.topology_vertices = topology.vertex_count();
    result.topology_edges = topology.edge_count();
    result.topology_faces = topology.face_count();
    result.materials = web.materials;
    result.scenes = web.scenes;
    result.keyframes = web.keyframes;

    for (const auto& spec : artifact_specs) {
        const auto staged_path = stage_base.string() + std::string{spec.suffix};
        const auto final_path =
            output_directory / (std::string{model_name} + std::string{spec.suffix});
        if (!std::filesystem::exists(staged_path)) {
            return fail("staged artifact is missing: " + staged_path);
        }
        error.clear();
        std::filesystem::remove(final_path, error);
        if (error)
            return fail("cannot replace artifact: " + error.message());
        std::filesystem::rename(staged_path, final_path, error);
        if (error)
            return fail("cannot commit artifact: " + error.message());
        const auto bytes = std::filesystem::file_size(final_path, error);
        if (error)
            return fail("cannot inspect committed artifact: " + error.message());
        result.artifacts.push_back(
            {std::string{spec.kind}, std::string{spec.media_type}, final_path, bytes});
    }

    const auto staged_library = stage / "icad-viewer.js";
    const auto final_library = output_directory / "icad-viewer.js";
    if (!std::filesystem::exists(staged_library)) {
        return fail("staged viewer library is missing");
    }
    error.clear();
    std::filesystem::remove(final_library, error);
    if (error)
        return fail("cannot replace viewer library: " + error.message());
    std::filesystem::rename(staged_library, final_library, error);
    if (error)
        return fail("cannot commit viewer library: " + error.message());
    const auto library_bytes = std::filesystem::file_size(final_library, error);
    if (error)
        return fail("cannot inspect viewer library: " + error.message());
    result.artifacts.push_back({"viewer-library", "text/javascript", final_library, library_bytes});
    result.success = true;
    result.message = "artifact package committed";
    return result;
}

} // namespace icad::project
