#include "icad/drawings/exporter.hpp"

#include "icad/cad/analysis.hpp"
#include "../cad/model.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <set>
#include <string_view>

namespace icad::drawings {

auto write_svg(const compiler::ir::Project& project, const std::filesystem::path& output)
    -> ExportResult {
    const auto analysis = cad::analyze(project);
    const auto model = cad::build_model(project);
    if (analysis.parts.empty()) {
        return {false, "project has no drawable parts"};
    }
    std::ofstream stream{output, std::ios::binary};
    if (!stream) {
        return {false, "cannot open SVG drawing output"};
    }
    const double x_extent = analysis.bounds.maximum[0] - analysis.bounds.minimum[0];
    const double y_extent = analysis.bounds.maximum[1] - analysis.bounds.minimum[1];
    const double z_extent = analysis.bounds.maximum[2] - analysis.bounds.minimum[2];
    const double scale = std::min({320.0 / std::max(x_extent, 1.0),
                                   260.0 / std::max(y_extent, 1.0),
                                   260.0 / std::max(z_extent, 1.0)});
    stream << std::setprecision(12)
           << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1200\" height=\"800\" "
              "viewBox=\"0 0 1200 800\"><rect width=\"1200\" height=\"800\" fill=\"white\"/>"
              "<g fill=\"none\" stroke=\"#102030\" stroke-width=\"1\">";
    for (const auto& part : model.parts) {
        std::set<std::pair<std::size_t, std::size_t>> edges;
        for (const auto& triangle : part.triangles) {
            for (std::size_t edge = 0; edge < 3; ++edge) {
                edges.emplace(std::min(triangle[edge], triangle[(edge + 1) % 3]),
                              std::max(triangle[edge], triangle[(edge + 1) % 3]));
            }
        }
        for (const auto [first, second] : edges) {
            const auto& a = part.vertices[first];
            const auto& b = part.vertices[second];
            stream << "<line x1=\"" << 80 + (a.x - analysis.bounds.minimum[0]) * scale
                   << "\" y1=\"" << 340 - (a.y - analysis.bounds.minimum[1]) * scale
                   << "\" x2=\"" << 80 + (b.x - analysis.bounds.minimum[0]) * scale
                   << "\" y2=\"" << 340 - (b.y - analysis.bounds.minimum[1]) * scale << "\"/>"
                   << "<line x1=\"" << 450 + (a.x - analysis.bounds.minimum[0]) * scale
                   << "\" y1=\"" << 340 - (a.z - analysis.bounds.minimum[2]) * scale
                   << "\" x2=\"" << 450 + (b.x - analysis.bounds.minimum[0]) * scale
                   << "\" y2=\"" << 340 - (b.z - analysis.bounds.minimum[2]) * scale << "\"/>"
                   << "<line x1=\"" << 850 + (a.y - analysis.bounds.minimum[1]) * scale
                   << "\" y1=\"" << 340 - (a.z - analysis.bounds.minimum[2]) * scale
                   << "\" x2=\"" << 850 + (b.y - analysis.bounds.minimum[1]) * scale
                   << "\" y2=\"" << 340 - (b.z - analysis.bounds.minimum[2]) * scale << "\"/>";
        }
    }
    stream << "</g><g stroke=\"#2563eb\" fill=\"none\"><line x1=\"80\" y1=\"420\" x2=\""
           << 80 + x_extent * scale << "\" y2=\"420\"/><line x1=\"80\" y1=\"414\" x2=\"80\" y2=\"426\"/>"
           << "<line x1=\"" << 80 + x_extent * scale << "\" y1=\"414\" x2=\""
           << 80 + x_extent * scale << "\" y2=\"426\"/></g>"
              "<g font-family=\"system-ui\" fill=\"#102030\">"
              "<text x=\"80\" y=\"55\" font-size=\"26\">"
           << project.name
           << "</text><text x=\"80\" y=\"380\">TOP</text>"
              "<text x=\"450\" y=\"380\">FRONT</text><text x=\"850\" y=\"380\">RIGHT</text>"
              "<text x=\"80\" y=\"445\">OVERALL WIDTH: "
           << x_extent << " mm</text><text x=\"80\" y=\"740\">Units: mm | Third-angle projected native edges</text>"
              "<text x=\"735\" y=\"625\" font-size=\"18\">TITLE: " << project.name
           << "</text><text x=\"735\" y=\"650\">SCALE: AUTO</text>"
              "<text x=\"900\" y=\"650\">DATUMS: A | B | C</text>"
              "<text x=\"735\" y=\"675\">GENERAL TOLERANCE: ±" << project.tolerance.linear_mm
           << " mm</text><text x=\"735\" y=\"700\">BOM ITEMS: " << project.bodies.size()
           << "</text></g><rect x=\"720\" y=\"600\" width=\"440\" height=\"125\" fill=\"none\" stroke=\"#102030\"/>"
              "<rect x=\"20\" y=\"20\" width=\"1160\" height=\"760\" fill=\"none\" "
              "stroke=\"#102030\"/></svg>";
    return {static_cast<bool>(stream), "SVG drawing export complete"};
}

namespace {

auto line(std::ostream& stream, std::string_view layer, double x1, double y1, double x2,
          double y2) -> void {
    stream << "0\nLINE\n8\n" << layer << "\n10\n" << x1 << "\n20\n" << y1
           << "\n30\n0\n11\n" << x2 << "\n21\n" << y2 << "\n31\n0\n";
}

auto rectangle(std::ostream& stream, std::string_view layer, double x1, double y1, double x2,
               double y2) -> void {
    line(stream, layer, x1, y1, x2, y1);
    line(stream, layer, x2, y1, x2, y2);
    line(stream, layer, x2, y2, x1, y2);
    line(stream, layer, x1, y2, x1, y1);
}

auto text(std::ostream& stream, std::string_view layer, std::string_view value, double x,
          double y, double height) -> void {
    stream << "0\nTEXT\n8\n" << layer << "\n10\n" << x << "\n20\n" << y
           << "\n30\n0\n40\n" << height << "\n1\n" << value << "\n";
}

} // namespace

auto write_dxf(const compiler::ir::Project& project, const std::filesystem::path& output)
    -> ExportResult {
    const auto analysis = cad::analyze(project);
    const auto model = cad::build_model(project);
    if (analysis.parts.empty())
        return {false, "project has no drawable parts"};
    std::ofstream stream{output, std::ios::binary};
    if (!stream)
        return {false, "cannot open DXF drawing output"};
    stream << std::setprecision(17)
           << "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1027\n9\n$INSUNITS\n70\n4\n"
              "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
    const double width = analysis.bounds.maximum[0] - analysis.bounds.minimum[0];
    const double depth = analysis.bounds.maximum[1] - analysis.bounds.minimum[1];
    const double front_offset = width + 20.0;
    const double right_offset = front_offset + width + 20.0;
    for (const auto& part : model.parts) {
        std::set<std::pair<std::size_t, std::size_t>> edges;
        for (const auto& triangle : part.triangles) {
            for (std::size_t edge = 0; edge < 3; ++edge) {
                edges.emplace(std::min(triangle[edge], triangle[(edge + 1) % 3]),
                              std::max(triangle[edge], triangle[(edge + 1) % 3]));
            }
        }
        for (const auto [first, second] : edges) {
            const auto& a = part.vertices[first];
            const auto& b = part.vertices[second];
            line(stream, "VISIBLE_TOP", a.x, a.y, b.x, b.y);
            line(stream, "VISIBLE_FRONT", front_offset + a.x, a.z, front_offset + b.x, b.z);
            line(stream, "VISIBLE_RIGHT", right_offset + a.y, a.z, right_offset + b.y, b.z);
        }
    }
    const double dimension_y = analysis.bounds.maximum[1] + 8.0;
    line(stream, "DIMENSION", analysis.bounds.minimum[0], dimension_y,
         analysis.bounds.maximum[0], dimension_y);
    line(stream, "DIMENSION", analysis.bounds.minimum[0], dimension_y - 2.0,
         analysis.bounds.minimum[0], dimension_y + 2.0);
    line(stream, "DIMENSION", analysis.bounds.maximum[0], dimension_y - 2.0,
         analysis.bounds.maximum[0], dimension_y + 2.0);
    text(stream, "DIMENSION", "OVERALL WIDTH " + std::to_string(width) + " mm",
         analysis.bounds.minimum[0], dimension_y + 2.0, 2.5);
    text(stream, "ANNOTATION", project.name, 0.0, depth + 12.0, 5.0);
    text(stream, "ANNOTATION", "TOP", 0.0, -8.0, 3.0);
    text(stream, "ANNOTATION", "FRONT", front_offset, -8.0, 3.0);
    text(stream, "ANNOTATION", "RIGHT", right_offset, -8.0, 3.0);
    const double title_y = dimension_y + 18.0;
    rectangle(stream, "TITLE_BLOCK", 0.0, title_y, right_offset + depth, title_y + 20.0);
    text(stream, "TITLE_BLOCK", "TITLE " + project.name, 2.0, title_y + 14.0, 3.0);
    text(stream, "TITLE_BLOCK", "DATUMS A B C", 2.0, title_y + 9.0, 2.5);
    text(stream, "TITLE_BLOCK",
         "GENERAL TOLERANCE +/- " + std::to_string(project.tolerance.linear_mm) + " mm", 2.0,
         title_y + 4.0, 2.5);
    stream << "0\nENDSEC\n0\nEOF\n";
    return {static_cast<bool>(stream), "DXF R2013 orthographic drawing export complete"};
}

auto inspect_dxf(const std::filesystem::path& input) -> ExportResult {
    std::ifstream stream{input, std::ios::binary};
    if (!stream)
        return {false, "cannot open DXF drawing"};
    const std::string content{std::istreambuf_iterator<char>{stream},
                              std::istreambuf_iterator<char>{}};
    if (!content.contains("$ACADVER\n1\nAC1027") || !content.contains("0\nSECTION\n") ||
        !content.ends_with("0\nEOF\n") || !content.contains("0\nLINE\n"))
        return {false, "DXF structure is invalid"};
    return {true, "DXF structural validation passed"};
}

} // namespace icad::drawings
