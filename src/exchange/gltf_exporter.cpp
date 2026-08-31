#include "gltf_exporter.hpp"

#include "../cad/model.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace icad::exchange {
namespace {

[[nodiscard]] auto quoted(std::string_view value) -> std::string {
    std::string result{"\""};
    for (const char character : value) {
        if (character == '\\' || character == '"')
            result.push_back('\\');
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

auto append_u32(std::string& bytes, std::uint32_t value) -> void {
    for (std::size_t byte = 0; byte < 4; ++byte)
        bytes.push_back(static_cast<char>((value >> (byte * 8U)) & 0xffU));
}

auto append_float(std::string& bytes, float value) -> void {
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

auto align4(std::string& bytes, char padding = '\0') -> void {
    while (bytes.size() % 4 != 0)
        bytes.push_back(padding);
}

[[nodiscard]] auto base64(std::string_view bytes) -> std::string {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t index = 0; index < bytes.size(); index += 3) {
        const std::uint32_t first = static_cast<unsigned char>(bytes[index]);
        const std::uint32_t second =
            index + 1 < bytes.size() ? static_cast<unsigned char>(bytes[index + 1]) : 0;
        const std::uint32_t third =
            index + 2 < bytes.size() ? static_cast<unsigned char>(bytes[index + 2]) : 0;
        const std::uint32_t value = (first << 16U) | (second << 8U) | third;
        result.push_back(alphabet[(value >> 18U) & 63U]);
        result.push_back(alphabet[(value >> 12U) & 63U]);
        result.push_back(index + 1 < bytes.size() ? alphabet[(value >> 6U) & 63U] : '=');
        result.push_back(index + 2 < bytes.size() ? alphabet[value & 63U] : '=');
    }
    return result;
}

struct Primitive {
    std::size_t position_offset{};
    std::size_t position_bytes{};
    std::size_t index_offset{};
    std::size_t index_bytes{};
    std::size_t vertices{};
    std::size_t indices{};
    std::array<double, 3> minimum{};
    std::array<double, 3> maximum{};
    std::size_t material{};
};

[[nodiscard]] auto document(const compiler::ir::Project& project, const cad::Model& model,
                            std::string& buffer, bool embedded) -> std::string {
    std::map<std::string, std::size_t> material_indices;
    for (std::size_t index = 0; index < project.materials.size(); ++index)
        material_indices.emplace(project.materials[index].name, index);
    std::vector<Primitive> primitives;
    primitives.reserve(model.parts.size());
    for (const auto& part : model.parts) {
        Primitive primitive;
        primitive.position_offset = buffer.size();
        primitive.vertices = part.vertices.size();
        primitive.minimum = {part.vertices.front().x, part.vertices.front().y,
                             part.vertices.front().z};
        primitive.maximum = primitive.minimum;
        for (const auto& vertex : part.vertices) {
            append_float(buffer, static_cast<float>(vertex.x));
            append_float(buffer, static_cast<float>(vertex.y));
            append_float(buffer, static_cast<float>(vertex.z));
            const double values[]{vertex.x, vertex.y, vertex.z};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                primitive.minimum[axis] = std::min(primitive.minimum[axis], values[axis]);
                primitive.maximum[axis] = std::max(primitive.maximum[axis], values[axis]);
            }
        }
        primitive.position_bytes = buffer.size() - primitive.position_offset;
        primitive.index_offset = buffer.size();
        primitive.indices = part.triangles.size() * 3;
        for (const auto& triangle : part.triangles) {
            append_u32(buffer, static_cast<std::uint32_t>(triangle[0]));
            append_u32(buffer, static_cast<std::uint32_t>(triangle[1]));
            append_u32(buffer, static_cast<std::uint32_t>(triangle[2]));
        }
        primitive.index_bytes = buffer.size() - primitive.index_offset;
        const auto material = material_indices.find(part.material);
        primitive.material = material == material_indices.end() ? 0 : material->second;
        primitives.push_back(primitive);
    }
    std::ostringstream output;
    output << std::setprecision(17) << "{\"asset\":{\"version\":\"2.0\",\"generator\":"
           << quoted("ICAD 0.21 native glTF") << "},\"scene\":0,\"scenes\":[{\"nodes\":[";
    for (std::size_t index = 0; index < model.parts.size(); ++index) {
        if (index != 0)
            output << ',';
        output << index;
    }
    output << "]}],\"nodes\":[";
    for (std::size_t index = 0; index < model.parts.size(); ++index) {
        if (index != 0)
            output << ',';
        output << "{\"name\":" << quoted(model.parts[index].body) << ",\"mesh\":" << index
               << '}';
    }
    output << "],\"meshes\":[";
    for (std::size_t index = 0; index < model.parts.size(); ++index) {
        if (index != 0)
            output << ',';
        output << "{\"name\":" << quoted(model.parts[index].name)
               << ",\"primitives\":[{\"attributes\":{\"POSITION\":" << index * 2
               << "},\"indices\":" << index * 2 + 1 << ",\"material\":"
               << primitives[index].material << "}]}";
    }
    output << "],\"materials\":[";
    if (project.materials.empty()) {
        output << "{\"name\":\"ICAD_DEFAULT\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.6,0.65,0.7,1],\"metallicFactor\":0,\"roughnessFactor\":0.7}}";
    } else {
        for (std::size_t index = 0; index < project.materials.size(); ++index) {
            if (index != 0)
                output << ',';
            const auto& material = project.materials[index];
            output << "{\"name\":" << quoted(material.name)
                   << ",\"pbrMetallicRoughness\":{\"baseColorFactor\":["
                   << material.base_color[0] << ',' << material.base_color[1] << ','
                   << material.base_color[2] << ',' << material.base_color[3]
                   << "],\"metallicFactor\":" << material.metallic
                   << ",\"roughnessFactor\":" << material.roughness << "}}";
        }
    }
    output << "],\"buffers\":[{\"byteLength\":" << buffer.size();
    if (embedded)
        output << ",\"uri\":\"data:application/octet-stream;base64," << base64(buffer) << '"';
    output << "}],\"bufferViews\":[";
    for (std::size_t index = 0; index < primitives.size(); ++index) {
        if (index != 0)
            output << ',';
        output << "{\"buffer\":0,\"byteOffset\":" << primitives[index].position_offset
               << ",\"byteLength\":" << primitives[index].position_bytes
               << ",\"target\":34962},{\"buffer\":0,\"byteOffset\":"
               << primitives[index].index_offset << ",\"byteLength\":"
               << primitives[index].index_bytes << ",\"target\":34963}";
    }
    output << "],\"accessors\":[";
    for (std::size_t index = 0; index < primitives.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& primitive = primitives[index];
        output << "{\"bufferView\":" << index * 2
               << ",\"componentType\":5126,\"count\":" << primitive.vertices
               << ",\"type\":\"VEC3\",\"min\":[" << primitive.minimum[0] << ','
               << primitive.minimum[1] << ',' << primitive.minimum[2] << "],\"max\":["
               << primitive.maximum[0] << ',' << primitive.maximum[1] << ','
               << primitive.maximum[2] << "]},{\"bufferView\":" << index * 2 + 1
               << ",\"componentType\":5125,\"count\":" << primitive.indices
               << ",\"type\":\"SCALAR\"}";
    }
    output << "]}";
    return output.str();
}

} // namespace

auto write_gltf(const compiler::ir::Project& project, const std::filesystem::path& output,
                bool binary) -> ExportResult {
    const auto model = cad::build_model(project);
    return write_gltf(project, model, output, binary);
}

auto write_gltf(const compiler::ir::Project& project, const cad::Model& model,
                const std::filesystem::path& output, bool binary) -> ExportResult {
    if (!cad::is_valid(model))
        return {false, "ICAD geometry validation failed before glTF export"};
    std::string buffer;
    const std::string json = document(project, model, buffer, !binary);
    std::ofstream stream{output, std::ios::binary};
    if (!stream)
        return {false, "cannot open glTF output '" + output.string() + "'"};
    if (!binary) {
        stream << json;
    } else {
        std::string padded_json = json;
        align4(padded_json, ' ');
        align4(buffer);
        stream.write("glTF", 4);
        std::string header;
        append_u32(header, 2);
        append_u32(header, static_cast<std::uint32_t>(12 + 8 + padded_json.size() + 8 +
                                                      buffer.size()));
        append_u32(header, static_cast<std::uint32_t>(padded_json.size()));
        append_u32(header, 0x4e4f534aU);
        stream.write(header.data(), static_cast<std::streamsize>(header.size()));
        stream.write(padded_json.data(), static_cast<std::streamsize>(padded_json.size()));
        std::string bin_header;
        append_u32(bin_header, static_cast<std::uint32_t>(buffer.size()));
        append_u32(bin_header, 0x004e4942U);
        stream.write(bin_header.data(), static_cast<std::streamsize>(bin_header.size()));
        stream.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    }
    return {static_cast<bool>(stream), binary ? "GLB 2.0 export complete"
                                              : "glTF 2.0 embedded export complete",
            model.parts.size(), model.vertex_count(), model.triangle_count()};
}

} // namespace icad::exchange
