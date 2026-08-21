#include "icad/manufacturing/validator.hpp"

#include "icad/cad/analysis.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
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

} // namespace

auto validate(const compiler::ir::Project& project, const Rules& rules) -> Report {
    const auto analysis = cad::analyze(project);
    Report report;
    report.process = rules.process;
    report.checked_rules = 8;
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
            if (feature.operation == compiler::ir::FeatureOperation::cut &&
                (feature.type == "CYLINDER" || feature.type == "CONE") &&
                property(feature, "RADIUS") * 2.0 < rules.minimum_hole_diameter_mm) {
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
