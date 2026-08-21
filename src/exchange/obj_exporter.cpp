#include "obj_exporter.hpp"

#include "../cad/model.hpp"

#include <fstream>
#include <iomanip>

namespace icad::exchange {

auto write_obj(const compiler::ir::Project& project, const std::filesystem::path& output)
    -> ExportResult {
    const auto model = cad::build_model(project);
    if (!cad::is_valid(model)) {
        return {false, "ICAD geometry validation failed before OBJ export"};
    }

    std::ofstream stream{output, std::ios::binary};
    if (!stream) {
        return {false, "cannot open OBJ output '" + output.string() + "'"};
    }
    stream << "# ICAD native geometry engine\n" << std::setprecision(17);
    std::size_t vertex_offset = 1;
    for (const auto& part : model.parts) {
        stream << "o " << part.name << '\n';
        if (!part.material.empty()) {
            stream << "usemtl " << part.material << '\n';
        }
        for (const auto& point : part.vertices) {
            stream << "v " << point.x << ' ' << point.y << ' ' << point.z << '\n';
        }
        for (const auto& triangle : part.triangles) {
            stream << "f " << triangle[0] + vertex_offset << ' '
                   << triangle[1] + vertex_offset << ' '
                   << triangle[2] + vertex_offset << '\n';
        }
        vertex_offset += part.vertices.size();
    }
    if (!stream) {
        return {false, "failed while writing OBJ output '" + output.string() + "'"};
    }
    return {true, "ICAD OBJ export complete", model.parts.size(), model.vertex_count(),
            model.triangle_count()};
}

} // namespace icad::exchange
