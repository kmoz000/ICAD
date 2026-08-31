#include "icad/manufacturing/validator.hpp"

#include "icad/cad/analysis.hpp"
#include "icad/cad/intersection.hpp"
#include "icad/cad/model.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <string_view>

namespace icad::manufacturing {
namespace {

[[nodiscard]] auto property(const compiler::ir::Feature& feature, std::string_view name)
    -> double {
    const auto found = std::ranges::find(feature.properties, name, &compiler::ir::Property::name);
    return found == feature.properties.end() ? 0.0 : found->value.value;
}

[[nodiscard]] auto process_supports(std::string_view process, std::string_view material) -> bool {
    if (process == "GENERAL")
        return true;
    if (process == "SHEET_METAL") {
        constexpr std::string_view metals[]{"STRUCTURAL_STEEL", "ALUMINUM", "COPPER", "BRASS",
                                             "TITANIUM", "RUSTED_STEEL"};
        return std::ranges::find(metals, material) != std::end(metals);
    }
    if (process == "CNC") {
        constexpr std::string_view unsupported[]{"WATER", "ICE", "GRASS", "EARTH", "FABRIC"};
        return std::ranges::find(unsupported, material) == std::end(unsupported);
    }
    if (process == "ADDITIVE")
        return material != "WATER" && material != "GRASS" && material != "EARTH";
    return false;
}

[[nodiscard]] auto occurrence_bounds(const cad::ProjectAnalysis& analysis)
    -> std::map<std::string, cad::Bounds> {
    std::map<std::string, cad::Bounds> result;
    for (const auto& part : analysis.parts) {
        const auto [found, inserted] = result.emplace(part.body, part.bounds);
        if (inserted)
            continue;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            found->second.minimum[axis] =
                std::min(found->second.minimum[axis], part.bounds.minimum[axis]);
            found->second.maximum[axis] =
                std::max(found->second.maximum[axis], part.bounds.maximum[axis]);
        }
    }
    return result;
}

struct Attachment {
    bool on_surface{};
    bool axis_matches{};
    double nearest_surface_mm{};
};

[[nodiscard]] auto attachment(const compiler::ir::Project& project,
                              const compiler::ir::ComponentInterface& interface,
                              const cad::Bounds& bounds) -> Attachment {
    const auto point = std::ranges::find(project.points, interface.point,
                                         &compiler::ir::SpatialPoint::name);
    const auto direction = std::ranges::find(project.vectors, interface.axis,
                                             &compiler::ir::Direction::name);
    if (point == project.points.end() || direction == project.vectors.end())
        return {};

    const auto& position = point->position_mm;
    const double tolerance = project.tolerance.linear_mm;
    const double angular_cosine =
        std::cos(project.tolerance.angular_degrees * std::numbers::pi / 180.0);
    Attachment result{false, false, std::numeric_limits<double>::max()};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        bool inside_projection = true;
        for (std::size_t other = 0; other < 3; ++other) {
            if (other == axis)
                continue;
            inside_projection = inside_projection &&
                                position[other] >= bounds.minimum[other] - tolerance &&
                                position[other] <= bounds.maximum[other] + tolerance;
        }
        for (const bool maximum : {false, true}) {
            const double surface = maximum ? bounds.maximum[axis] : bounds.minimum[axis];
            const double distance = std::abs(position[axis] - surface);
            result.nearest_surface_mm = std::min(result.nearest_surface_mm, distance);
            if (!inside_projection || distance > tolerance)
                continue;
            result.on_surface = true;
            const double outward = maximum ? 1.0 : -1.0;
            result.axis_matches = result.axis_matches ||
                                  direction->unit[axis] * outward + 1e-12 >= angular_cosine;
        }
    }
    return result;
}

[[nodiscard]] auto requires_planar_attachment(std::string_view kind) -> bool {
    return kind == "MOUNT" || kind == "FLANGE" || kind == "WELD_SEAM" ||
           kind == "BOND_FACE";
}

[[nodiscard]] auto contact_only_connection(std::string_view method) -> bool {
    return method == "BOLTED" || method == "SCREWED" || method == "WELDED" ||
           method == "BRAZED" || method == "BONDED";
}

} // namespace

auto validate(const compiler::ir::Project& project, const Rules& rules) -> Report {
    const auto model = cad::build_model(project);
    return validate(project, cad::analyze(project, model),
                    cad::analyze_intersections(project, model, project.tolerance.linear_mm),
                    rules);
}

auto validate(const compiler::ir::Project& project, const cad::ProjectAnalysis& analysis,
              const cad::IntersectionAnalysis& intersections, const Rules& rules) -> Report {
    Report report;
    report.process = rules.process;
    report.checked_rules = 8 + project.interfaces.size() * 2 + project.connections.size() * 2;
    constexpr std::string_view processes[]{"GENERAL", "CNC", "ADDITIVE", "SHEET_METAL"};
    if (std::ranges::find(processes, rules.process) == std::end(processes)) {
        report.issues.push_back({"ICAD-M0005", Severity::error, rules.process,
                                 "unknown manufacturing process"});
    }
    std::set<std::string> bodies_with_material;
    for (const auto& body : project.bodies) {
        if (!body.material.empty()) {
            bodies_with_material.insert(body.name);
        }
    }
    for (const auto& connection : project.connections) {
        if (!connection.aligned) {
            report.issues.push_back(
                {"ICAD-M0012", Severity::error, connection.name,
                 connection.automatic
                     ? "magnetic assembly interface still requires a snap transform before release"
                     : "manufacturing connection interfaces are not seated within clearance"});
        }
    }
    const auto bounds = occurrence_bounds(analysis);
    for (const auto& interface : project.interfaces) {
        if (!requires_planar_attachment(interface.kind))
            continue;
        const auto occurrence = bounds.find(interface.occurrence);
        if (occurrence == bounds.end()) {
            report.issues.push_back({"ICAD-M0013", Severity::error, interface.name,
                                     "interface occurrence has no delivery geometry"});
            continue;
        }
        const auto result = attachment(project, interface, occurrence->second);
        if (!result.on_surface) {
            report.issues.push_back(
                {"ICAD-M0013", Severity::error, interface.name,
                 "interface datum is not attached to the referenced occurrence surface; "
                 "update its POINT3 after changing part dimensions (nearest boundary " +
                     std::to_string(result.nearest_surface_mm) + " mm)"});
        } else if (!result.axis_matches) {
            report.issues.push_back(
                {"ICAD-M0014", Severity::error, interface.name,
                 "interface axis does not match the outward normal of its attachment face"});
        }
    }
    for (const auto& contact : intersections.body_contacts) {
        if (!contact.declared_connection || contact.penetrating_part_pairs == 0 ||
            !contact_only_connection(contact.connection_method)) {
            continue;
        }
        report.issues.push_back(
            {"ICAD-M0015", Severity::error, contact.connection_name,
             contact.connection_method +
                 " connection has solid-volume penetration; contact-only connections may "
                 "touch at their interface but may not overlap"});
    }
    for (const auto& instance : project.instances) {
        const auto definition =
            std::ranges::find(project.bodies, instance.body, &compiler::ir::Body::name);
        if (definition != project.bodies.end() && !definition->material.empty())
            bodies_with_material.insert(instance.name);
    }
    for (const auto& part : analysis.parts) {
        if (part.volume_mm3 <= 1e-9) {
            report.issues.push_back(
                {"ICAD-M0001", Severity::error, part.name, "solid volume is zero"});
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double extent = part.bounds.maximum[axis] - part.bounds.minimum[axis];
            if (extent < rules.minimum_extent_mm) {
                report.issues.push_back({"ICAD-M0002", Severity::error, part.name,
                                         "part extent is below the configured minimum"});
            }
            if (extent > rules.maximum_extent_mm) {
                report.issues.push_back({"ICAD-M0003", Severity::error, part.name,
                                         "part extent exceeds the configured maximum"});
            }
        }
        const double minimum_extent = std::min({part.bounds.maximum[0] - part.bounds.minimum[0],
                                                part.bounds.maximum[1] - part.bounds.minimum[1],
                                                part.bounds.maximum[2] - part.bounds.minimum[2]});
        if (minimum_extent < rules.minimum_wall_thickness_mm) {
            report.issues.push_back({"ICAD-M0006", Severity::warning, part.name,
                                     "minimum part extent is below the wall-thickness rule"});
        }
        if (rules.require_material && !bodies_with_material.contains(part.body)) {
            report.issues.push_back({"ICAD-M0004", Severity::warning, part.body,
                                     "body has no manufacturing material"});
        }
    }
    for (const auto& body : project.bodies) {
        const auto material = std::ranges::find(project.materials, body.material,
                                                &compiler::ir::Material::name);
        if (rules.enforce_process_material_compatibility && material != project.materials.end() &&
            !process_supports(rules.process, material->preset)) {
            report.issues.push_back({"ICAD-M0007", Severity::error, body.name,
                                     "material preset is incompatible with manufacturing process"});
        }
        for (const auto& feature : body.features) {
            const double hole_radius = feature.type == "CONE"
                                           ? std::min(property(feature, "RADIUS1"),
                                                      property(feature, "RADIUS2"))
                                           : property(feature, "RADIUS");
            if (feature.operation == compiler::ir::FeatureOperation::cut &&
                (feature.type == "CYLINDER" || feature.type == "CONE") &&
                hole_radius * 2.0 < rules.minimum_hole_diameter_mm) {
                report.issues.push_back({"ICAD-M0008", Severity::error, feature.name,
                                         "cut hole diameter is below the process rule"});
            }
            if (rules.process == "CNC" && feature.type == "FILLET" &&
                property(feature, "RADIUS") < rules.minimum_tool_radius_mm) {
                report.issues.push_back({"ICAD-M0009", Severity::error, feature.name,
                                         "fillet radius is below available tooling radius"});
            }
            if (rules.process == "SHEET_METAL" && feature.type == "BEND" &&
                property(feature, "RADIUS") < rules.minimum_bend_radius_mm) {
                report.issues.push_back({"ICAD-M0010", Severity::error, feature.name,
                                         "bend radius is below the sheet-metal rule"});
            }
            if (rules.process == "ADDITIVE" && feature.type == "OVERHANG" &&
                property(feature, "ANGLE") > rules.maximum_overhang_degrees) {
                report.issues.push_back({"ICAD-M0011", Severity::warning, feature.name,
                                         "overhang angle requires support material"});
            }
        }
    }
    report.passed = std::ranges::none_of(report.issues, [](const auto& issue) {
        return issue.severity == Severity::error;
    });
    return report;
}

auto write_report(const compiler::ir::Project& project, const std::filesystem::path& output,
                  const Rules& rules) -> bool {
    const auto report = validate(project, rules);
    return write_report(project, report, output);
}

auto write_report(const compiler::ir::Project& project, const Report& report,
                  const std::filesystem::path& output) -> bool {
    std::ofstream stream{output, std::ios::binary};
    if (!stream) {
        return false;
    }
    stream << "{\"project\":\"" << project.name << "\",\"process\":\"" << report.process
           << "\",\"checkedRules\":" << report.checked_rules << ",\"passed\":"
           << (report.passed ? "true" : "false") << ",\"issues\":[";
    for (std::size_t index = 0; index < report.issues.size(); ++index) {
        const auto& issue = report.issues[index];
        if (index != 0) {
            stream << ',';
        }
        stream << "{\"code\":\"" << issue.code << "\",\"severity\":\""
               << (issue.severity == Severity::error
                       ? "error"
                       : issue.severity == Severity::warning ? "warning" : "information")
               << "\",\"subject\":\"" << issue.subject << "\",\"message\":\""
               << issue.message << "\"}";
    }
    stream << "]}";
    return static_cast<bool>(stream);
}

} // namespace icad::manufacturing
