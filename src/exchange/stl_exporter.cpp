#include "stl_exporter.hpp"

#include "../cad/model.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <string>

namespace icad::exchange {
namespace {

[[nodiscard]] auto normal(const cad::Point3& a, const cad::Point3& b, const cad::Point3& c)
    -> cad::Point3 {
    const cad::Point3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
    const cad::Point3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
    cad::Point3 value{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z,
                      ab.x * ac.y - ab.y * ac.x};
    const double length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length > 0.0) {
        value.x /= length;
        value.y /= length;
        value.z /= length;
    }
    return value;
}

} // namespace

auto write_stl(const compiler::ir::Project& project, const std::filesystem::path& output)
    -> ExportResult {
    const auto model = cad::build_model(project);
    if (!cad::is_valid(model)) {
        return {false, "ICAD geometry validation failed before STL export"};
    }
    std::ofstream stream{output, std::ios::binary};
    if (!stream) {
        return {false, "cannot open STL output '" + output.string() + "'"};
    }
    stream << std::setprecision(17);
    for (const auto& part : model.parts) {
        stream << "solid " << part.name << '\n';
        for (const auto& triangle : part.triangles) {
            const auto& first = part.vertices[triangle[0]];
            const auto& second = part.vertices[triangle[1]];
            const auto& third = part.vertices[triangle[2]];
            const auto face_normal = normal(first, second, third);
            stream << "  facet normal " << face_normal.x << ' ' << face_normal.y << ' '
                   << face_normal.z << "\n    outer loop\n"
                   << "      vertex " << first.x << ' ' << first.y << ' ' << first.z << '\n'
                   << "      vertex " << second.x << ' ' << second.y << ' ' << second.z << '\n'
                   << "      vertex " << third.x << ' ' << third.y << ' ' << third.z
                   << "\n    endloop\n  endfacet\n";
        }
        stream << "endsolid " << part.name << '\n';
    }
    if (!stream) {
        return {false, "failed while writing STL output '" + output.string() + "'"};
    }
    return {true, "ASCII STL export complete", model.parts.size(), model.vertex_count(),
            model.triangle_count()};
}

auto inspect_stl(const std::filesystem::path& input) -> StlInspection {
    std::ifstream stream{input, std::ios::binary};
    if (!stream) {
        return {false, "cannot open STL file"};
    }
    std::size_t solid_starts = 0;
    std::size_t solid_ends = 0;
    std::size_t facets = 0;
    std::size_t facet_ends = 0;
    std::string line;
    while (std::getline(stream, line)) {
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) {
            continue;
        }
        const auto last = line.find_last_not_of(" \t\r");
        const std::string_view trimmed{line.data() + first, last - first + 1};
        if (trimmed.starts_with("solid "))
            ++solid_starts;
        if (trimmed.starts_with("endsolid"))
            ++solid_ends;
        if (trimmed.starts_with("facet normal "))
            ++facets;
        if (trimmed == "endfacet")
            ++facet_ends;
    }
    const bool valid = solid_starts > 0 && facets > 0 && solid_starts == solid_ends &&
                       facets == facet_ends;
    return {valid, valid ? "ASCII STL validation passed" : "STL structure is incomplete",
            solid_starts, facets};
}

} // namespace icad::exchange
