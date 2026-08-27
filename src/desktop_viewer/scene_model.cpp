#include "scene_model.hpp"

#include "icad/json/value.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <string>

namespace icad::desktop {
namespace {

[[nodiscard]] auto text(const json::Value* value) -> std::string {
    if (value == nullptr || value->string() == nullptr)
        return {};
    return *value->string();
}

[[nodiscard]] auto number(const json::Value* value, double fallback = 0.0) -> double {
    if (value == nullptr || value->number() == nullptr)
        return fallback;
    return *value->number();
}

[[nodiscard]] auto vector3(const json::Value& value, QVector3D& result) -> bool {
    const auto* values = value.array();
    if (values == nullptr || values->size() != 3)
        return false;
    const auto* x = (*values)[0].number();
    const auto* y = (*values)[1].number();
    const auto* z = (*values)[2].number();
    if (x == nullptr || y == nullptr || z == nullptr)
        return false;
    result = QVector3D{static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(*z)};
    return true;
}

[[nodiscard]] auto material_colors(const json::Value& root) -> std::map<std::string, QColor> {
    std::map<std::string, QColor> result;
    const auto* materials = root.find("materials");
    if (materials == nullptr || materials->array() == nullptr)
        return result;
    for (const auto& material : *materials->array()) {
        const auto name = text(material.find("name"));
        const auto* color_value = material.find("baseColor");
        if (name.empty() || color_value == nullptr || color_value->array() == nullptr ||
            color_value->array()->size() != 4) {
            continue;
        }
        const auto& channels = *color_value->array();
        if (channels[0].number() == nullptr || channels[1].number() == nullptr ||
            channels[2].number() == nullptr || channels[3].number() == nullptr) {
            continue;
        }
        result.emplace(name, QColor::fromRgbF(static_cast<float>(*channels[0].number()),
                                             static_cast<float>(*channels[1].number()),
                                             static_cast<float>(*channels[2].number()),
                                             static_cast<float>(*channels[3].number())));
    }
    return result;
}

[[nodiscard]] auto fallback_color(std::size_t index) -> QColor {
    constexpr int hue_step = 47;
    return QColor::fromHsv(static_cast<int>((index * hue_step + 206U) % 360U), 150, 205);
}

} // namespace

auto RenderScene::radius() const noexcept -> float {
    return std::max(1.0F, (maximum - minimum).length() * 0.5F);
}

auto parse_render_scene(std::string_view source) -> SceneParseResult {
    SceneParseResult result;
    const auto parsed = json::parse(source);
    if (!parsed.ok()) {
        result.error = QStringLiteral("Invalid native scene data at byte %1: %2")
                           .arg(static_cast<qulonglong>(parsed.offset))
                           .arg(QString::fromStdString(parsed.error));
        return result;
    }
    const auto& root = *parsed.value;
    if (text(root.find("format")) != "ICAD_SCENE") {
        result.error = QStringLiteral("Unsupported render-scene format");
        return result;
    }
    result.scene.project = QString::fromStdString(text(root.find("project")));
    const auto colors = material_colors(root);
    const auto* parts = root.find("parts");
    if (parts == nullptr || parts->array() == nullptr) {
        result.error = QStringLiteral("Native scene contains no render parts");
        return result;
    }

    const float limit = std::numeric_limits<float>::max();
    result.scene.minimum = QVector3D{limit, limit, limit};
    result.scene.maximum = QVector3D{-limit, -limit, -limit};
    for (const auto& source_part : *parts->array()) {
        const auto* source_vertices = source_part.find("vertices");
        const auto* source_triangles = source_part.find("triangles");
        if (source_vertices == nullptr || source_vertices->array() == nullptr ||
            source_triangles == nullptr || source_triangles->array() == nullptr) {
            continue;
        }
        const auto source_triangle_count = source_triangles->array()->size();
        const auto available_indices = std::numeric_limits<std::uint32_t>::max() -
                                       result.scene.vertices.size();
        if (source_triangle_count > available_indices / 3U) {
            result.error = QStringLiteral("Scene exceeds the 32-bit native mesh index limit");
            return result;
        }
        const auto material_name = text(source_part.find("material"));
        const auto color_it = colors.find(material_name);
        const QColor color = color_it == colors.end() ? fallback_color(result.scene.parts.size())
                                                       : color_it->second;
        RenderPart part;
        part.name = QString::fromStdString(text(source_part.find("name")));
        part.body = QString::fromStdString(text(source_part.find("body")));
        part.material = QString::fromStdString(material_name);
        part.first_index = static_cast<std::uint32_t>(result.scene.indices.size());
        part.first_wire_index = static_cast<std::uint32_t>(result.scene.wire_indices.size());
        part.minimum = QVector3D{limit, limit, limit};
        part.maximum = QVector3D{-limit, -limit, -limit};

        std::vector<QVector3D> positions;
        positions.reserve(source_vertices->array()->size());
        for (const auto& source_vertex : *source_vertices->array()) {
            QVector3D position;
            if (!vector3(source_vertex, position)) {
                result.error = QStringLiteral("Part '%1' has an invalid vertex").arg(part.name);
                return result;
            }
            positions.push_back(position);
            part.minimum.setX(std::min(part.minimum.x(), position.x()));
            part.minimum.setY(std::min(part.minimum.y(), position.y()));
            part.minimum.setZ(std::min(part.minimum.z(), position.z()));
            part.maximum.setX(std::max(part.maximum.x(), position.x()));
            part.maximum.setY(std::max(part.maximum.y(), position.y()));
            part.maximum.setZ(std::max(part.maximum.z(), position.z()));
        }

        struct SourceTriangle {
            std::array<std::uint32_t, 3> vertices{};
            QVector3D normal;
            std::array<float, 3> corner_angles{};
        };
        std::vector<SourceTriangle> triangles;
        triangles.reserve(source_triangle_count);
        std::vector<std::vector<std::size_t>> vertex_faces(positions.size());
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<QVector3D>> edge_normals;
        for (const auto& source_triangle : *source_triangles->array()) {
            const auto* corners = source_triangle.array();
            if (corners == nullptr || corners->size() != 3 || (*corners)[0].number() == nullptr ||
                (*corners)[1].number() == nullptr || (*corners)[2].number() == nullptr) {
                result.error = QStringLiteral("Part '%1' has an invalid triangle").arg(part.name);
                return result;
            }
            std::uint32_t local[3]{};
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const double raw = *(*corners)[corner].number();
                if (raw < 0.0 || std::floor(raw) != raw ||
                    raw >= static_cast<double>(source_vertices->array()->size())) {
                    result.error = QStringLiteral("Part '%1' has an out-of-range triangle")
                                       .arg(part.name);
                    return result;
                }
                local[corner] = static_cast<std::uint32_t>(raw);
            }
            const QVector3D first_edge = positions[local[1]] - positions[local[0]];
            const QVector3D second_edge = positions[local[2]] - positions[local[0]];
            QVector3D normal = QVector3D::crossProduct(first_edge, second_edge);
            if (qFuzzyIsNull(normal.lengthSquared())) {
                result.error = QStringLiteral("Part '%1' has a degenerate triangle").arg(part.name);
                return result;
            }
            normal.normalize();
            SourceTriangle triangle{{local[0], local[1], local[2]}, normal, {}};
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const QVector3D first =
                    positions[local[(corner + 1U) % 3U]] - positions[local[corner]];
                const QVector3D second =
                    positions[local[(corner + 2U) % 3U]] - positions[local[corner]];
                const float divisor = first.length() * second.length();
                const float cosine = divisor <= 1.0e-12F
                                         ? 1.0F
                                         : std::clamp(QVector3D::dotProduct(first, second) /
                                                          divisor,
                                                      -1.0F, 1.0F);
                triangle.corner_angles[corner] = std::acos(cosine);
                vertex_faces[local[corner]].push_back(triangles.size());
            }
            triangles.push_back(triangle);
            for (std::size_t edge = 0; edge < 3; ++edge) {
                auto edge_first = local[edge];
                auto edge_second = local[(edge + 1U) % 3U];
                if (edge_first > edge_second)
                    std::swap(edge_first, edge_second);
                edge_normals[{edge_first, edge_second}].push_back(normal);
            }
        }

        // One source point may belong to both a smooth cylindrical surface and
        // a sharp end face. A single averaged normal produces long triangular
        // lighting streaks across that boundary. Emit a render corner for each
        // triangle and average only angle-weighted faces inside the same smooth
        // (30 degree) group. Geometry stays indexed and watertight; shading gets
        // the split normals expected from a CAD viewer.
        constexpr float crease_cosine = 0.8660254F; // 30 degrees
        constexpr auto missing_index = std::numeric_limits<std::uint32_t>::max();
        std::vector<std::uint32_t> first_render_index(positions.size(), missing_index);
        const QVector4D render_color{color.redF(), color.greenF(), color.blueF(), color.alphaF()};
        result.scene.vertices.reserve(result.scene.vertices.size() + triangles.size() * 3U);
        result.scene.indices.reserve(result.scene.indices.size() + triangles.size() * 3U);
        result.scene.mesh_wire_indices.reserve(result.scene.mesh_wire_indices.size() +
                                               triangles.size() * 6U);
        for (const auto& triangle : triangles) {
            std::array<std::uint32_t, 3> render_corners{};
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto local_vertex = triangle.vertices[corner];
                QVector3D smooth_normal;
                for (const auto adjacent_index : vertex_faces[local_vertex]) {
                    const auto& adjacent = triangles[adjacent_index];
                    if (QVector3D::dotProduct(triangle.normal, adjacent.normal) < crease_cosine)
                        continue;
                    const auto adjacent_corner = std::find(adjacent.vertices.begin(),
                                                           adjacent.vertices.end(), local_vertex);
                    const auto angle_index = static_cast<std::size_t>(
                        std::distance(adjacent.vertices.begin(), adjacent_corner));
                    smooth_normal += adjacent.normal * adjacent.corner_angles[angle_index];
                }
                if (qFuzzyIsNull(smooth_normal.lengthSquared()))
                    smooth_normal = triangle.normal;
                else
                    smooth_normal.normalize();
                const auto render_index =
                    static_cast<std::uint32_t>(result.scene.vertices.size());
                result.scene.vertices.push_back(
                    {positions[local_vertex], smooth_normal, render_color});
                result.scene.indices.push_back(render_index);
                render_corners[corner] = render_index;
                if (first_render_index[local_vertex] == missing_index)
                    first_render_index[local_vertex] = render_index;
            }
            result.scene.mesh_wire_indices.insert(
                result.scene.mesh_wire_indices.end(),
                {render_corners[0], render_corners[1], render_corners[1], render_corners[2],
                 render_corners[2], render_corners[0]});
        }
        part.index_count = static_cast<std::uint32_t>(result.scene.indices.size()) - part.first_index;
        // A CAD wireframe shows boundaries and creases, not triangulation used
        // internally for the GPU. Coplanar diagonals and smooth circular facet
        // seams are deliberately suppressed.
        for (const auto& [edge, normals] : edge_normals) {
            const bool boundary = normals.size() != 2;
            const bool crease = !boundary && QVector3D::dotProduct(normals[0], normals[1]) <
                                                  crease_cosine;
            if (!boundary && !crease)
                continue;
            result.scene.wire_indices.push_back(first_render_index[edge.first]);
            result.scene.wire_indices.push_back(first_render_index[edge.second]);
        }
        part.wire_index_count =
            static_cast<std::uint32_t>(result.scene.wire_indices.size()) - part.first_wire_index;
        if (part.index_count != 0) {
            result.scene.minimum.setX(std::min(result.scene.minimum.x(), part.minimum.x()));
            result.scene.minimum.setY(std::min(result.scene.minimum.y(), part.minimum.y()));
            result.scene.minimum.setZ(std::min(result.scene.minimum.z(), part.minimum.z()));
            result.scene.maximum.setX(std::max(result.scene.maximum.x(), part.maximum.x()));
            result.scene.maximum.setY(std::max(result.scene.maximum.y(), part.maximum.y()));
            result.scene.maximum.setZ(std::max(result.scene.maximum.z(), part.maximum.z()));
            result.scene.parts.push_back(std::move(part));
        }
    }
    if (const auto* scenes = root.find("scenes"); scenes != nullptr && scenes->array() != nullptr) {
        for (const auto& scene : *scenes->array()) {
            RenderSceneInfo info{QString::fromStdString(text(scene.find("name"))),
                                 number(scene.find("duration")), number(scene.find("fps"), 30.0),
                                 QString::fromStdString(text(scene.find("background"))), {}};
            if (const auto* lights = scene.find("lights");
                lights != nullptr && lights->array() != nullptr) {
                for (const auto& light : *lights->array()) {
                    QVector3D color{1.0F, 1.0F, 1.0F};
                    QVector3D position;
                    if (const auto* source_color = light.find("color");
                        source_color == nullptr || !vector3(*source_color, color)) {
                        result.error = QStringLiteral("Scene '%1' has an invalid light color")
                                           .arg(info.name);
                        return result;
                    }
                    if (const auto* source_position = light.find("positionMm");
                        source_position == nullptr || !vector3(*source_position, position)) {
                        result.error = QStringLiteral("Scene '%1' has an invalid light position")
                                           .arg(info.name);
                        return result;
                    }
                    info.lights.push_back(
                        {QString::fromStdString(text(light.find("name"))),
                         text(light.find("kind")) == "POINT", color,
                         static_cast<float>(number(light.find("intensity"))), position});
                }
            }
            result.scene.scenes.push_back(std::move(info));
        }
    }
    if (result.scene.empty())
        result.error = QStringLiteral("Native scene contains no triangles");
    return result;
}

} // namespace icad::desktop
