#pragma once

#include "scene_model.hpp"

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>

#include <functional>
#include <optional>

class QPainter;

namespace icad::desktop {

enum class StandardView { isometric, front, back, left, right, top, bottom };
enum class DisplayMode { solid, solid_with_mesh, cad_wireframe, mesh_wireframe };

class CadViewport final : public QOpenGLWidget, protected QOpenGLFunctions {
  public:
    explicit CadViewport(QWidget* parent = nullptr);
    ~CadViewport() override;

    auto set_scene(RenderScene scene) -> void;
    auto clear_scene() -> void;
    auto fit_all() -> void;
    auto fit_selected() -> void;
    auto set_standard_view(StandardView view) -> void;
    auto set_orthographic(bool enabled) -> void;
    auto set_display_mode(DisplayMode mode) -> void;
    auto set_active_scene(std::size_t index) -> void;
    auto set_scene_lighting(bool enabled) -> void;
    auto set_debug_overlay(bool enabled) -> void;
    auto set_assembly_inspection(bool enabled) -> void;
    auto set_cutaway(bool enabled) -> void;
    auto select_part(std::optional<std::size_t> index) -> void;
    auto save_screenshot(const QString& path) -> bool;

    [[nodiscard]] auto selected_part() const noexcept -> std::optional<std::size_t> {
        return selected_part_;
    }
    [[nodiscard]] auto scene() const noexcept -> const RenderScene& { return scene_; }
    [[nodiscard]] auto display_mode() const noexcept -> DisplayMode { return display_mode_; }

    std::function<void(std::optional<std::size_t>)> selection_changed;

  protected:
    auto initializeGL() -> void override;
    auto resizeGL(int width, int height) -> void override;
    auto paintGL() -> void override;
    auto mousePressEvent(QMouseEvent* event) -> void override;
    auto mouseMoveEvent(QMouseEvent* event) -> void override;
    auto mouseReleaseEvent(QMouseEvent* event) -> void override;
    auto wheelEvent(QWheelEvent* event) -> void override;

  private:
    auto upload_scene() -> void;
    auto update_matrices() -> void;
    auto draw_grid(const QMatrix4x4& view_projection) -> void;
    auto draw_edges(bool mesh, const QVector4D& color) -> void;
    [[nodiscard]] auto pick_part(const QPoint& point) const -> std::optional<std::size_t>;
    [[nodiscard]] auto orientation_cube_rect() const -> QRect;
    auto draw_hud(QPainter& painter) const -> void;
    auto draw_assembly_overlay(QPainter& painter) const -> void;

    RenderScene scene_;
    QOpenGLShaderProgram mesh_program_;
    QOpenGLShaderProgram line_program_;
    QOpenGLBuffer vertex_buffer_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer index_buffer_{QOpenGLBuffer::IndexBuffer};
    QOpenGLBuffer wire_index_buffer_{QOpenGLBuffer::IndexBuffer};
    QOpenGLBuffer mesh_wire_index_buffer_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject vertex_array_;
    QMatrix4x4 projection_;
    QMatrix4x4 view_;
    QVector3D target_;
    float distance_{10.0F};
    float yaw_degrees_{38.0F};
    float pitch_degrees_{24.0F};
    bool orthographic_{false};
    DisplayMode display_mode_{DisplayMode::solid};
    std::size_t active_scene_{};
    bool scene_lighting_{true};
    bool debug_overlay_{false};
    bool assembly_inspection_{false};
    bool cutaway_{false};
    bool initialized_{false};
    bool scene_dirty_{false};
    bool dragging_{false};
    bool panning_{false};
    QPoint press_position_;
    QPoint last_position_;
    std::optional<std::size_t> selected_part_;
};

} // namespace icad::desktop
