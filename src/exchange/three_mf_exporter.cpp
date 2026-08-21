#include "three_mf_exporter.hpp"

#include "../cad/model.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace icad::exchange {
namespace {

auto append_u16(std::string& bytes, std::uint16_t value) -> void {
    bytes.push_back(static_cast<char>(value & 0xffU));
    bytes.push_back(static_cast<char>((value >> 8U) & 0xffU));
}

auto append_u32(std::string& bytes, std::uint32_t value) -> void {
    for (std::size_t byte = 0; byte < 4; ++byte)
        bytes.push_back(static_cast<char>((value >> (byte * 8U)) & 0xffU));
}

[[nodiscard]] auto crc32(std::string_view value) -> std::uint32_t {
    std::uint32_t crc = 0xffffffffU;
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        crc ^= byte;
        for (std::size_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

struct Entry {
    std::string name;
    std::string data;
    std::uint32_t crc{};
    std::uint32_t offset{};
};

[[nodiscard]] auto xml_escape(std::string_view value) -> std::string {
    std::string result;
    for (const char character : value) {
        if (character == '&')
            result += "&amp;";
        else if (character == '<')
            result += "&lt;";
        else if (character == '"')
            result += "&quot;";
        else
            result.push_back(character);
    }
    return result;
}

[[nodiscard]] auto model_xml(const compiler::ir::Project& project, const cad::Model& model)
    -> std::string {
    std::ostringstream xml;
    xml << std::setprecision(17)
        << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<model unit=\"millimeter\" xml:lang=\"en-US\" "
           "xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">"
           "<metadata name=\"Title\">"
        << xml_escape(project.name) << "</metadata><resources>";
    for (std::size_t object = 0; object < model.parts.size(); ++object) {
        const auto& part = model.parts[object];
        xml << "<object id=\"" << object + 1 << "\" name=\"" << xml_escape(part.name)
            << "\" type=\"model\"><mesh><vertices>";
        for (const auto& vertex : part.vertices)
            xml << "<vertex x=\"" << vertex.x << "\" y=\"" << vertex.y << "\" z=\""
                << vertex.z << "\"/>";
        xml << "</vertices><triangles>";
        for (const auto& triangle : part.triangles)
            xml << "<triangle v1=\"" << triangle[0] << "\" v2=\"" << triangle[1]
                << "\" v3=\"" << triangle[2] << "\"/>";
        xml << "</triangles></mesh></object>";
    }
    xml << "</resources><build>";
    for (std::size_t object = 0; object < model.parts.size(); ++object)
        xml << "<item objectid=\"" << object + 1 << "\"/>";
    xml << "</build></model>";
    return xml.str();
}

auto write_zip(std::ostream& output, std::vector<Entry>& entries) -> bool {
    std::string bytes;
    for (auto& entry : entries) {
        entry.crc = crc32(entry.data);
        entry.offset = static_cast<std::uint32_t>(bytes.size());
        append_u32(bytes, 0x04034b50U);
        append_u16(bytes, 20);
        append_u16(bytes, 0);
        append_u16(bytes, 0);
        append_u16(bytes, 0);
        append_u16(bytes, 0);
        append_u32(bytes, entry.crc);
        append_u32(bytes, static_cast<std::uint32_t>(entry.data.size()));
        append_u32(bytes, static_cast<std::uint32_t>(entry.data.size()));
        append_u16(bytes, static_cast<std::uint16_t>(entry.name.size()));
        append_u16(bytes, 0);
        bytes += entry.name;
        bytes += entry.data;
    }
    const std::uint32_t central_offset = static_cast<std::uint32_t>(bytes.size());
    for (const auto& entry : entries) {
        append_u32(bytes, 0x02014b50U);
        append_u16(bytes, 20);
        append_u16(bytes, 20);
        append_u16(bytes, 0);
        append_u16(bytes, 0);
        append_u16(bytes, 0);
        append_u16(bytes, 0);
        append_u32(bytes, entry.crc);
        append_u32(bytes, static_cast<std::uint32_t>(entry.data.size()));
        append_u32(bytes, static_cast<std::uint32_t>(entry.data.size()));
        append_u16(bytes, static_cast<std::uint16_t>(entry.name.size()));
        append_u16(bytes, 0);
        append_u16(bytes, 0);
        append_u16(bytes, 0);
        append_u16(bytes, 0);
        append_u32(bytes, 0);
        append_u32(bytes, entry.offset);
        bytes += entry.name;
    }
    const std::uint32_t central_size = static_cast<std::uint32_t>(bytes.size()) - central_offset;
    append_u32(bytes, 0x06054b50U);
    append_u16(bytes, 0);
    append_u16(bytes, 0);
    append_u16(bytes, static_cast<std::uint16_t>(entries.size()));
    append_u16(bytes, static_cast<std::uint16_t>(entries.size()));
    append_u32(bytes, central_size);
    append_u32(bytes, central_offset);
    append_u16(bytes, 0);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

} // namespace

auto write_3mf(const compiler::ir::Project& project, const std::filesystem::path& output)
    -> ExportResult {
    const auto model = cad::build_model(project);
    if (!cad::is_valid(model))
        return {false, "ICAD geometry validation failed before 3MF export"};
    std::ofstream stream{output, std::ios::binary};
    if (!stream)
        return {false, "cannot open 3MF output '" + output.string() + "'"};
    std::vector<Entry> entries{
        {"[Content_Types].xml",
         "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/></Types>"},
        {"_rels/.rels",
         "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/></Relationships>"},
        {"3D/3dmodel.model", model_xml(project, model)}};
    if (!write_zip(stream, entries))
        return {false, "failed while writing 3MF package"};
    return {true, "3MF core package export complete", model.parts.size(), model.vertex_count(),
            model.triangle_count()};
}

} // namespace icad::exchange
