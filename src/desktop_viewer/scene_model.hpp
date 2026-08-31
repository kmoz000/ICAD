#pragma once

#include <QColor>
#include <QString>
#include <QVector3D>
#include <QVector4D>

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <vector>

namespace icad::desktop {

struct RenderVertex {
    QVector3D position;
    QVector3D normal;
    QVector4D color;
};

struct RenderPart {
    QString name;
    QString body;
    QString material;
    std::uint32_t first_index{};
    std::uint32_t index_count{};
    std::uint32_t first_wire_index{};
    std::uint32_t wire_index_count{};
    QVector3D minimum;
    QVector3D maximum;
};

struct RenderLight {
    QString name;
    bool point{};
    QVector3D color;
    float intensity{};
    QVector3D position;
};

struct RenderJoint {
    QString name;
    QString type;
    QString parent;
    QString child;
    QVector3D pivot;
    QVector3D axis;
};

struct RenderConnection {
    QString name;
    QString method;
    QString standard;
    QString first_body;
    QString second_body;
    QVector3D point;
    float clearance_mm{};
    float gap_mm{};
    bool aligned{};
};

struct RenderSceneInfo {
    QString name;
    double duration_seconds{};
    double frames_per_second{};
    QString background;
    std::vector<RenderLight> lights;
};

struct RenderScene {
    QString project;
    std::vector<RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> wire_indices;
    std::vector<std::uint32_t> mesh_wire_indices;
    std::vector<RenderPart> parts;
    std::vector<RenderJoint> joints;
    std::vector<RenderConnection> connections;
    std::vector<RenderSceneInfo> scenes;
    QVector3D minimum;
    QVector3D maximum;

    [[nodiscard]] auto empty() const noexcept -> bool { return indices.empty(); }
    [[nodiscard]] auto center() const noexcept -> QVector3D { return (minimum + maximum) * 0.5F; }
    [[nodiscard]] auto radius() const noexcept -> float;
};

struct SceneParseResult {
    RenderScene scene;
    QString error;
    std::size_t discarded_degenerate_triangles{};

    [[nodiscard]] auto ok() const noexcept -> bool { return error.isEmpty(); }
};

[[nodiscard]] auto parse_render_scene(std::string_view json) -> SceneParseResult;

} // namespace icad::desktop
