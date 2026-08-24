#include "scene_model.hpp"

#include "icad/json/value.hpp"

#include <algorithm>
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
        const auto base = result.scene.vertices.size();
        if (base > std::numeric_limits<std::uint32_t>::max()) {
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
        part.minimum = QVector3D{limit, limit, limit};
        part.maximum = QVector3D{-limit, -limit, -limit};

        result.scene.vertices.reserve(result.scene.vertices.size() + source_vertices->array()->size());
        for (const auto& source_vertex : *source_vertices->array()) {
            QVector3D position;
            if (!vector3(source_vertex, position)) {
                result.error = QStringLiteral("Part '%1' has an invalid vertex").arg(part.name);
                return result;
            }
            result.scene.vertices.push_back(
                {position, {}, QVector4D{color.redF(), color.greenF(), color.blueF(), color.alphaF()}});
            part.minimum.setX(std::min(part.minimum.x(), position.x()));
            part.minimum.setY(std::min(part.minimum.y(), position.y()));
            part.minimum.setZ(std::min(part.minimum.z(), position.z()));
            part.maximum.setX(std::max(part.maximum.x(), position.x()));
            part.maximum.setY(std::max(part.maximum.y(), position.y()));
            part.maximum.setZ(std::max(part.maximum.z(), position.z()));
        }

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
            const auto first = static_cast<std::uint32_t>(base) + local[0];
            const auto second = static_cast<std::uint32_t>(base) + local[1];
            const auto third = static_cast<std::uint32_t>(base) + local[2];
            const QVector3D normal = QVector3D::normal(result.scene.vertices[first].position,
                                                       result.scene.vertices[second].position,
                                                       result.scene.vertices[third].position);
            result.scene.vertices[first].normal += normal;
            result.scene.vertices[second].normal += normal;
            result.scene.vertices[third].normal += normal;
            result.scene.indices.insert(result.scene.indices.end(), {first, second, third});
        }
        part.index_count = static_cast<std::uint32_t>(result.scene.indices.size()) - part.first_index;
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
    for (auto& vertex : result.scene.vertices) {
        if (!qFuzzyIsNull(vertex.normal.lengthSquared()))
            vertex.normal.normalize();
        else
            vertex.normal = QVector3D{0.0F, 0.0F, 1.0F};
    }

    if (const auto* scenes = root.find("scenes"); scenes != nullptr && scenes->array() != nullptr) {
        for (const auto& scene : *scenes->array()) {
            result.scene.scenes.push_back(
                {QString::fromStdString(text(scene.find("name"))), number(scene.find("duration")),
                 number(scene.find("fps"), 30.0)});
        }
    }
    if (result.scene.empty())
        result.error = QStringLiteral("Native scene contains no triangles");
    return result;
}

} // namespace icad::desktop
