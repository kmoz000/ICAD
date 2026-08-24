#pragma once

#include <QColor>
#include <QString>
#include <QVector3D>
#include <QVector4D>

#include <cstdint>
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
    QVector3D minimum;
    QVector3D maximum;
};

struct RenderSceneInfo {
    QString name;
    double duration_seconds{};
    double frames_per_second{};
};

struct RenderScene {
    QString project;
    std::vector<RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<RenderPart> parts;
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

    [[nodiscard]] auto ok() const noexcept -> bool { return error.isEmpty(); }
};

[[nodiscard]] auto parse_render_scene(std::string_view json) -> SceneParseResult;

} // namespace icad::desktop
