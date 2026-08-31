#include "step_exporter.hpp"

#include "../cad/model.hpp"
#include "icad/cad/topology.hpp"

#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace icad::exchange {
namespace {

class StepWriter {
  public:
    explicit StepWriter(std::ostream& output) : output_(output) {}

    template <typename... Values>
    auto entity(const Values&... values) -> std::size_t {
        const std::size_t current = next_++;
        output_ << '#' << current << '=';
        (output_ << ... << values);
        output_ << ";\n";
        return current;
    }

  private:
    std::ostream& output_;
    std::size_t next_{1};
};

[[nodiscard]] auto count_step_entities(std::string_view content,
                                       std::string_view entity_name) -> std::size_t {
    std::size_t count = 0;
    for (std::size_t cursor = content.find('='); cursor != std::string_view::npos;
         cursor = content.find('=', cursor + 1)) {
        std::size_t name = cursor + 1;
        while (name < content.size() &&
               std::isspace(static_cast<unsigned char>(content[name])) != 0)
            ++name;
        if (!content.substr(name).starts_with(entity_name))
            continue;
        std::size_t opening = name + entity_name.size();
        while (opening < content.size() &&
               std::isspace(static_cast<unsigned char>(content[opening])) != 0)
            ++opening;
        if (opening < content.size() && content[opening] == '(')
            ++count;
    }
    return count;
}

[[nodiscard]] auto references(const std::vector<std::size_t>& identifiers) -> std::string {
    std::ostringstream output;
    output << '(';
    for (std::size_t index = 0; index < identifiers.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << '#' << identifiers[index];
    }
    output << ')';
    return output.str();
}

[[nodiscard]] auto quoted(std::string value) -> std::string {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('\'');
    for (const char character : value) {
        escaped.push_back(character == '\'' ? '_' : character);
    }
    escaped.push_back('\'');
    return escaped;
}

[[nodiscard]] auto point_text(const cad::Point3& point) -> std::string {
    std::ostringstream output;
    output << std::setprecision(17) << "('',(" << point.x << ',' << point.y << ',' << point.z
           << "))";
    return output.str();
}

[[nodiscard]] auto direction_text(const cad::Vector3& direction) -> std::string {
    std::ostringstream output;
    output << std::setprecision(17) << "('',(" << direction.x << ',' << direction.y << ','
           << direction.z << "))";
    return output.str();
}

[[nodiscard]] auto reference_direction(const cad::Vector3& axis) -> cad::Vector3 {
    const cad::Vector3 seed = std::abs(axis.x) < 0.8 ? cad::Vector3{1.0, 0.0, 0.0}
                                                     : cad::Vector3{0.0, 1.0, 0.0};
    const cad::Vector3 cross{axis.y * seed.z - axis.z * seed.y,
                             axis.z * seed.x - axis.x * seed.z,
                             axis.x * seed.y - axis.y * seed.x};
    const double length = std::sqrt(cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
    return {cross.x / length, cross.y / length, cross.z / length};
}

[[nodiscard]] auto axis_placement(StepWriter& writer, const cad::Point3& origin,
                                  const cad::Vector3& axis,
                                  const cad::Vector3& reference) -> std::size_t {
    const auto point = writer.entity("CARTESIAN_POINT", point_text(origin));
    const auto axis_direction = writer.entity("DIRECTION", direction_text(axis));
    const auto reference_axis = writer.entity("DIRECTION", direction_text(reference));
    return writer.entity("AXIS2_PLACEMENT_3D('',#", point, ",#", axis_direction, ",#",
                         reference_axis, ")");
}

[[nodiscard]] auto begins_with_entity_id(std::string_view line, std::size_t& identifier) -> bool {
    if (line.empty() || line.front() != '#') {
        return false;
    }
    std::size_t cursor = 1;
    identifier = 0;
    while (cursor < line.size() && std::isdigit(static_cast<unsigned char>(line[cursor])) != 0) {
        identifier = identifier * 10 + static_cast<std::size_t>(line[cursor] - '0');
        ++cursor;
    }
    return cursor > 1 && cursor < line.size() && line[cursor] == '=';
}

} // namespace

auto write_step_file(const compiler::ir::Project& project, const cad::Model& model,
                     const cad::TopologyModel& topology,
                     const std::filesystem::path& output, bool assembly) -> ExportResult {
    if (!cad::is_valid(model)) {
        return {false, "ICAD geometry validation failed before STEP export"};
    }
    if (!cad::validate_topology(topology).valid())
        return {false, "ICAD topology validation failed before STEP export"};

    std::ofstream stream{output, std::ios::binary};
    if (!stream) {
        return {false, "cannot open STEP output '" + output.string() + "'"};
    }
    stream << "ISO-10303-21;\nHEADER;\n"
              "FILE_DESCRIPTION(('ICAD native faceted "
           << (assembly ? "assembly" : "solid model") << "'),'2;1');\n"
              "FILE_NAME('" << output.filename().string()
           << "','',('ICAD'),('ICAD'),'ICAD native compiler','ICAD','');\n"
              "FILE_SCHEMA(('AUTOMOTIVE_DESIGN_CC2'));\nENDSEC;\nDATA;\n";

    StepWriter writer{stream};
    const auto application = writer.entity("APPLICATION_CONTEXT('automotive design')");
    writer.entity("APPLICATION_PROTOCOL_DEFINITION('international standard',"
                  "'automotive_design',2000,#",
                  application, ")");
    const auto product_context = writer.entity("PRODUCT_CONTEXT('',#", application,
                                               ",'mechanical')");
    const auto product = writer.entity("PRODUCT(", quoted(project.name), ",",
                                       quoted(project.name), ",'',(#", product_context, "))");
    const auto formation = writer.entity(
        "PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE('','',#", product,
        ",.NOT_KNOWN.)");
    const auto definition_context = writer.entity("PRODUCT_DEFINITION_CONTEXT('part definition',#",
                                                  application, ",'design')");
    const auto definition =
        writer.entity("PRODUCT_DEFINITION('design','',#", formation, ",#", definition_context, ")");
    const auto definition_shape = writer.entity("PRODUCT_DEFINITION_SHAPE('','',#", definition, ")");
    const auto length_unit =
        writer.entity("(LENGTH_UNIT()NAMED_UNIT(*)SI_UNIT(.MILLI.,.METRE.))");
    const auto angle_unit =
        writer.entity("(NAMED_UNIT(*)PLANE_ANGLE_UNIT()SI_UNIT($,.RADIAN.))");
    const auto solid_angle_unit =
        writer.entity("(NAMED_UNIT(*)SOLID_ANGLE_UNIT()SI_UNIT($,.STERADIAN.))");
    const auto uncertainty = writer.entity("UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-6),#",
                                           length_unit,
                                           ",'distance_accuracy_value','confusion accuracy')");
    const auto context = writer.entity(
        "(GEOMETRIC_REPRESENTATION_CONTEXT(3)GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#",
        uncertainty, "))GLOBAL_UNIT_ASSIGNED_CONTEXT((#", length_unit, ",#", angle_unit, ",#",
        solid_angle_unit, "))REPRESENTATION_CONTEXT('',''))");

    std::vector<std::size_t> solids;
    std::map<std::string, std::vector<std::size_t>> solids_by_body;
    for (const auto& solid_topology : topology.solids) {
        std::unordered_map<std::string, std::size_t> vertices;
        for (const auto& vertex : solid_topology.vertices) {
            const auto point = writer.entity("CARTESIAN_POINT", point_text(vertex.point));
            vertices.emplace(vertex.id, writer.entity("VERTEX_POINT('',#", point, ")"));
        }
        std::unordered_map<std::string, std::size_t> edges;
        for (const auto& edge : solid_topology.edges) {
            std::size_t curve = 0;
            if (edge.curve.kind == cad::CurveKind::line) {
                const auto origin = writer.entity("CARTESIAN_POINT", point_text(edge.curve.origin));
                const auto direction =
                    writer.entity("DIRECTION", direction_text(edge.curve.direction));
                const auto vector = writer.entity("VECTOR('',#", direction, ",1.)");
                curve = writer.entity("LINE('',#", origin, ",#", vector, ")");
            } else {
                const auto placement =
                    axis_placement(writer, edge.curve.origin, edge.curve.axis,
                                   edge.curve.direction);
                curve = writer.entity("CIRCLE('',#", placement, ",", edge.curve.radius, ")");
            }
            edges.emplace(edge.id,
                          writer.entity("EDGE_CURVE('',#", vertices.at(edge.start_vertex), ",#",
                                        vertices.at(edge.end_vertex), ",#", curve, ",.T.)"));
        }
        std::vector<std::size_t> faces;
        for (const auto& face : solid_topology.faces) {
            std::vector<std::size_t> bounds;
            for (std::size_t boundary_index = 0; boundary_index < face.boundaries.size();
                 ++boundary_index) {
                std::vector<std::size_t> oriented_edges;
                for (const auto& oriented : face.boundaries[boundary_index].edges) {
                    oriented_edges.push_back(writer.entity(
                        "ORIENTED_EDGE('',*,*,#", edges.at(oriented.edge), ",",
                        oriented.reversed ? ".F.)" : ".T.)"));
                }
                const auto loop = writer.entity("EDGE_LOOP('',", references(oriented_edges), ")");
                bounds.push_back(writer.entity(boundary_index == 0 ? "FACE_OUTER_BOUND('',#"
                                                                  : "FACE_BOUND('',#",
                                               loop, ",.T.)"));
            }
            const auto reference = reference_direction(face.surface.axis);
            const auto placement =
                axis_placement(writer, face.surface.origin, face.surface.axis, reference);
            std::size_t surface = 0;
            switch (face.surface.kind) {
            case cad::SurfaceKind::plane:
                surface = writer.entity("PLANE('',#", placement, ")");
                break;
            case cad::SurfaceKind::cylinder:
                surface = writer.entity("CYLINDRICAL_SURFACE('',#", placement, ",",
                                        face.surface.radius, ")");
                break;
            case cad::SurfaceKind::cone:
                surface = writer.entity("CONICAL_SURFACE('',#", placement, ",",
                                        face.surface.radius, ",", face.surface.semi_angle_radians,
                                        ")");
                break;
            case cad::SurfaceKind::sphere:
                surface = writer.entity("SPHERICAL_SURFACE('',#", placement, ",",
                                        face.surface.radius, ")");
                break;
            }
            faces.push_back(
                writer.entity("ADVANCED_FACE('',", references(bounds), ",#", surface, ",.T.)"));
        }
        const auto shell = writer.entity("CLOSED_SHELL(", quoted(solid_topology.id), ",",
                                         references(faces), ")");
        const auto solid = writer.entity("MANIFOLD_SOLID_BREP(", quoted(solid_topology.id), ",#",
                                         shell, ")");
        solids.push_back(solid);
        solids_by_body[solid_topology.body].push_back(solid);
    }
    const auto representation = writer.entity("ADVANCED_BREP_SHAPE_REPRESENTATION('',",
                                              references(solids), ",#", context, ")");
    writer.entity("SHAPE_DEFINITION_REPRESENTATION(#", definition_shape, ",#", representation,
                  ")");
    if (assembly) {
        std::size_t occurrence_index = 1;
        for (const auto& [body_name, body_solids] : solids_by_body) {
            const auto child_product = writer.entity("PRODUCT(", quoted(body_name), ",",
                                                     quoted(body_name), ",'',(#", product_context,
                                                     "))");
            const auto child_formation = writer.entity(
                "PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE('','',#", child_product,
                ",.NOT_KNOWN.)");
            const auto child_definition = writer.entity("PRODUCT_DEFINITION('design','',#",
                                                        child_formation, ",#", definition_context,
                                                        ")");
            const auto child_shape =
                writer.entity("PRODUCT_DEFINITION_SHAPE('','',#", child_definition, ")");
            const auto child_representation = writer.entity(
                "ADVANCED_BREP_SHAPE_REPRESENTATION('',", references(body_solids), ",#", context,
                ")");
            writer.entity("SHAPE_DEFINITION_REPRESENTATION(#", child_shape, ",#",
                          child_representation, ")");
            const auto occurrence = writer.entity(
                "NEXT_ASSEMBLY_USAGE_OCCURRENCE('", occurrence_index, "',", quoted(body_name),
                ",'',#", definition, ",#", child_definition, ",$)");
            writer.entity("PRODUCT_DEFINITION_SHAPE('','',#", occurrence, ")");
            ++occurrence_index;
        }
    }
    stream << "ENDSEC;\nEND-ISO-10303-21;\n";
    if (!stream) {
        return {false, "failed while writing STEP output '" + output.string() + "'"};
    }
    return {true, assembly ? "STEP AP214 assembly export complete"
                           : "STEP AP214 faceted B-Rep export complete",
            model.parts.size(),
            model.vertex_count(), model.triangle_count()};
}

auto write_step(const compiler::ir::Project& project, const std::filesystem::path& output)
    -> ExportResult {
    const auto model = cad::build_model(project);
    const auto topology = cad::build_topology(project);
    return write_step(project, model, topology, output);
}

auto write_step(const compiler::ir::Project& project, const cad::Model& model,
                const cad::TopologyModel& topology,
                const std::filesystem::path& output) -> ExportResult {
    return write_step_file(project, model, topology, output, false);
}

auto write_assembly_step(const compiler::ir::Project& project,
                         const std::filesystem::path& output) -> ExportResult {
    const auto model = cad::build_model(project);
    const auto topology = cad::build_topology(project);
    return write_assembly_step(project, model, topology, output);
}

auto write_assembly_step(const compiler::ir::Project& project, const cad::Model& model,
                         const cad::TopologyModel& topology,
                         const std::filesystem::path& output) -> ExportResult {
    return write_step_file(project, model, topology, output, true);
}

auto export_assembly_step(const compiler::ir::Project& project,
                          const std::filesystem::path& output) -> ExportResult {
    return write_assembly_step(project, output);
}

auto export_assembly_step(const compiler::ir::Project& project, const cad::Model& model,
                          const cad::TopologyModel& topology,
                          const std::filesystem::path& output) -> ExportResult {
    return write_assembly_step(project, model, topology, output);
}

auto inspect_step(const std::filesystem::path& input) -> StepInspection {
    std::ifstream stream{input, std::ios::binary};
    if (!stream) {
        return {false, "cannot open STEP file"};
    }
    const std::string content{std::istreambuf_iterator<char>{stream},
                              std::istreambuf_iterator<char>{}};
    const bool header = content.find("ISO-10303-21;") != std::string::npos;
    const bool footer = content.find("END-ISO-10303-21;") != std::string::npos;
    const auto solids = count_step_entities(content, "FACETED_BREP") +
                        count_step_entities(content, "MANIFOLD_SOLID_BREP");
    const auto components = count_step_entities(content, "NEXT_ASSEMBLY_USAGE_OCCURRENCE");
    if (!header || !footer || solids == 0) {
        return {false, "STEP envelope contains no recognized solids"};
    }

    if (content.find("ICAD native") == std::string::npos) {
        return {true, "external STEP structure recognized", solids, solids, components};
    }

    stream.clear();
    stream.seekg(0);
    std::set<std::size_t> identifiers;
    std::vector<std::string> entity_lines;
    std::string line;
    std::size_t shells = 0;
    while (std::getline(stream, line)) {
        std::size_t identifier = 0;
        if (begins_with_entity_id(line, identifier)) {
            if (!identifiers.insert(identifier).second || line.back() != ';') {
                return {false, "STEP contains a duplicate or unterminated entity"};
            }
            entity_lines.push_back(line);
            if (line.find("=CLOSED_SHELL(") != std::string::npos)
                ++shells;
        }
    }
    if (solids != shells) {
        return {false, "STEP envelope or faceted solid topology is invalid"};
    }
    for (const auto& entity : entity_lines) {
        for (std::size_t cursor = entity.find('#', 1); cursor != std::string::npos;
             cursor = entity.find('#', cursor + 1)) {
            std::size_t reference = 0;
            std::size_t digit = cursor + 1;
            while (digit < entity.size() &&
                   std::isdigit(static_cast<unsigned char>(entity[digit])) != 0) {
                reference = reference * 10 + static_cast<std::size_t>(entity[digit] - '0');
                ++digit;
            }
            if (digit == cursor + 1 || !identifiers.contains(reference)) {
                return {false, "STEP contains a dangling entity reference"};
            }
            cursor = digit - 1;
        }
    }
    return {true, "STEP structural validation passed", solids, solids, components};
}

} // namespace icad::exchange
