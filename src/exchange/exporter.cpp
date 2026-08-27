#include "icad/exchange/exporter.hpp"

#include "obj_exporter.hpp"
#include "gltf_exporter.hpp"
#include "step_exporter.hpp"
#include "stl_exporter.hpp"
#include "three_mf_exporter.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <system_error>

namespace icad::exchange {

auto format_from_extension(const std::filesystem::path& output) -> ExportFormat {
    std::string extension = output.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char character) {
                               return static_cast<char>(std::tolower(character));
                           });
    if (extension == ".obj") {
        return ExportFormat::obj;
    }
    if (extension == ".step" || extension == ".stp") {
        return ExportFormat::step;
    }
    if (extension == ".stl") {
        return ExportFormat::stl;
    }
    if (extension == ".gltf")
        return ExportFormat::gltf;
    if (extension == ".glb")
        return ExportFormat::glb;
    if (extension == ".3mf")
        return ExportFormat::three_mf;
    return ExportFormat::unsupported;
}

auto export_project(const compiler::ir::Project& project, const std::filesystem::path& output)
    -> ExportResult {
    if (!output.parent_path().empty()) {
        std::error_code error;
        std::filesystem::create_directories(output.parent_path(), error);
        if (error) {
            return {false, "cannot create output directory: " + error.message()};
        }
    }

    switch (format_from_extension(output)) {
    case ExportFormat::obj:
        return write_obj(project, output);
    case ExportFormat::step:
        return write_step(project, output);
    case ExportFormat::stl:
        return write_stl(project, output);
    case ExportFormat::gltf:
        return write_gltf(project, output, false);
    case ExportFormat::glb:
        return write_gltf(project, output, true);
    case ExportFormat::three_mf:
        return write_3mf(project, output);
    case ExportFormat::unsupported:
        return {false, "unsupported output extension '" + output.extension().string() + "'"};
    }
    return {false, "unsupported export format"};
}

auto inspect_gltf(const std::filesystem::path& input) -> MeshPackageInspection {
    std::ifstream stream{input, std::ios::binary};
    if (!stream)
        return {false, "cannot open glTF/GLB file"};
    const std::string content{std::istreambuf_iterator<char>{stream},
                              std::istreambuf_iterator<char>{}};
    if (input.extension() == ".glb") {
        if (content.size() < 20 || content.substr(0, 4) != "glTF" ||
            static_cast<unsigned char>(content[4]) != 2)
            return {false, "invalid GLB 2.0 envelope"};
    } else if (content.find("\"asset\"") == std::string::npos ||
               content.find("\"version\":\"2.0\"") == std::string::npos ||
               content.find("data:application/octet-stream;base64,") == std::string::npos) {
        return {false, "invalid embedded glTF 2.0 document"};
    }
    const std::regex mesh_pattern{R"("primitives"\s*:)"};
    const auto objects = static_cast<std::size_t>(
        std::distance(std::sregex_iterator{content.begin(), content.end(), mesh_pattern},
                      std::sregex_iterator{}));
    return objects == 0 ? MeshPackageInspection{false, "glTF contains no meshes", 0}
                        : MeshPackageInspection{true, "glTF/GLB structural validation passed",
                                                objects};
}

auto inspect_3mf(const std::filesystem::path& input) -> MeshPackageInspection {
    std::ifstream stream{input, std::ios::binary};
    if (!stream)
        return {false, "cannot open 3MF file"};
    const std::string content{std::istreambuf_iterator<char>{stream},
                              std::istreambuf_iterator<char>{}};
    if (content.size() < 4 || static_cast<unsigned char>(content[0]) != 0x50U ||
        static_cast<unsigned char>(content[1]) != 0x4bU ||
        content.find("[Content_Types].xml") == std::string::npos ||
        content.find("3D/3dmodel.model") == std::string::npos)
        return {false, "invalid 3MF OPC package"};
    const std::regex object_pattern{R"(<object id=)"};
    const auto objects = static_cast<std::size_t>(
        std::distance(std::sregex_iterator{content.begin(), content.end(), object_pattern},
                      std::sregex_iterator{}));
    return objects == 0 ? MeshPackageInspection{false, "3MF contains no objects", 0}
                        : MeshPackageInspection{true, "3MF structural validation passed", objects};
}

} // namespace icad::exchange
