#include "icad/document/exporter.hpp"

#include "icad/cad/analysis.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <string_view>

namespace icad::document {
namespace {

[[nodiscard]] auto json_string(std::string_view value) -> std::string {
    std::string escaped{"\""};
    for (const char character : value) {
        if (character == '\\' || character == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    escaped.push_back('"');
    return escaped;
}

} // namespace

auto write_bom(const compiler::ir::Project& project, const std::filesystem::path& output)
    -> ExportResult {
    const auto analysis = cad::analyze(project);
    std::map<std::string, std::pair<std::size_t, double>> body_metrics;
    for (const auto& part : analysis.parts) {
        auto& metrics = body_metrics[part.body];
        ++metrics.first;
        metrics.second += part.volume_mm3;
    }
    std::ofstream stream{output, std::ios::binary};
    if (!stream) {
        return {false, "cannot open BOM output"};
    }
    stream << std::setprecision(17) << "{\"project\":" << json_string(project.name)
           << ",\"components\":[";
    const std::size_t component_count = project.bodies.size() + project.instances.size();
    for (std::size_t index = 0; index < component_count; ++index) {
        const bool definition = index < project.bodies.size();
        const auto& body = definition ? project.bodies[index]
                                      : *std::ranges::find(
                                            project.bodies,
                                            project.instances[index - project.bodies.size()].body,
                                            &compiler::ir::Body::name);
        const std::string& occurrence =
            definition ? body.name : project.instances[index - project.bodies.size()].name;
        const auto metrics = body_metrics[occurrence];
        if (index != 0) {
            stream << ',';
        }
        stream << "{\"item\":" << index + 1 << ",\"body\":" << json_string(occurrence)
               << ",\"definition\":" << json_string(body.name)
               << ",\"material\":" << json_string(body.material) << ",\"quantity\":1"
               << ",\"solidCount\":" << metrics.first << ",\"volumeMm3\":"
               << metrics.second << '}';
    }
    stream << "]}";
    return {static_cast<bool>(stream), "BOM export complete"};
}

} // namespace icad::document
