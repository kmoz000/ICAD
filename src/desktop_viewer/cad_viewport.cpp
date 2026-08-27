#include "cad_viewport.hpp"

#include <QMouseEvent>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace icad::desktop {
namespace {

constexpr auto vertex_shader = R"glsl(
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 color;
uniform mat4 mvp;
uniform mat4 model_view;
out vec3 eye_normal;
out vec3 eye_position;
out vec4 base_color;
void main() {
    vec4 transformed = model_view * vec4(position, 1.0);
    gl_Position = mvp * vec4(position, 1.0);
    eye_normal = normalize(mat3(model_view) * normal);
    eye_position = transformed.xyz;
    base_color = color;
}
)glsl";

constexpr auto fragment_shader = R"glsl(
#version 330 core
in vec3 eye_normal;
in vec3 eye_position;
in vec4 base_color;
uniform float selected;
uniform int light_count;
uniform int light_is_point[4];
uniform vec3 light_position[4];
uniform vec3 light_color[4];
uniform float light_intensity[4];
uniform float scene_radius;
out vec4 fragment;
void main() {
    vec3 n = normalize(gl_FrontFacing ? eye_normal : -eye_normal);
    vec3 illumination = vec3(0.18);
    if (light_count == 0) {
        vec3 key = normalize(vec3(0.35, 0.55, 0.75));
        vec3 fill = normalize(vec3(-0.7, 0.1, 0.4));
        illumination += vec3(0.60 * max(dot(n, key), 0.0));
        illumination += vec3(0.16 * max(dot(n, fill), 0.0));
    } else {
        for (int index = 0; index < light_count; ++index) {
            vec3 direction = normalize(vec3(0.35, 0.55, 0.75));
            float attenuation = 1.0;
            if (light_is_point[index] == 1) {
                vec3 delta = light_position[index] - eye_position;
                float normalized_distance = length(delta) / max(scene_radius, 0.001);
                direction = normalize(delta);
                attenuation = 1.0 / (1.0 + normalized_distance * normalized_distance);
            }
            float strength = 0.18 * light_intensity[index] * attenuation *
                             max(dot(n, direction), 0.0);
            illumination += light_color[index] * strength;
        }
    }
    vec3 color = base_color.rgb * min(illumination, vec3(1.4));
    color = mix(color, vec3(0.12, 0.72, 1.0), selected * 0.48);
    fragment = vec4(color, 1.0);
}
)glsl";

constexpr auto line_vertex_shader = R"glsl(
#version 330 core
layout(location = 0) in vec3 position;
uniform mat4 mvp;
void main() { gl_Position = mvp * vec4(position, 1.0); }
)glsl";

constexpr auto line_fragment_shader = R"glsl(
#version 330 core
uniform vec4 line_color;
out vec4 fragment;
void main() { fragment = line_color; }
)glsl";

[[nodiscard]] auto point_in_triangle(const QPointF& point, const QPointF& first,
                                     const QPointF& second, const QPointF& third) -> bool {
    const auto sign = [](const QPointF& a, const QPointF& b, const QPointF& c) {
        return (a.x() - c.x()) * (b.y() - c.y()) - (b.x() - c.x()) * (a.y() - c.y());
    };
    const double first_sign = sign(point, first, second);
    const double second_sign = sign(point, second, third);
    const double third_sign = sign(point, third, first);
    const bool negative = first_sign < 0.0 || second_sign < 0.0 || third_sign < 0.0;
    const bool positive = first_sign > 0.0 || second_sign > 0.0 || third_sign > 0.0;
    return !(negative && positive);
}

[[nodiscard]] auto rounded_hexagon(const QRectF& bounds, qreal corner_radius) -> QPainterPath {
    const qreal center_x = bounds.center().x();
    const std::array<QPointF, 6> vertices{
        QPointF{center_x, bounds.top()},
        QPointF{bounds.right(), bounds.top() + bounds.height() * 0.24},
        QPointF{bounds.right(), bounds.bottom() - bounds.height() * 0.24},
        QPointF{center_x, bounds.bottom()},
        QPointF{bounds.left(), bounds.bottom() - bounds.height() * 0.24},
        QPointF{bounds.left(), bounds.top() + bounds.height() * 0.24},
    };
    const auto toward = [corner_radius](const QPointF& from, const QPointF& to) {
        QLineF line{from, to};
        line.setLength(std::min(corner_radius, line.length() * 0.35));
        return line.p2();
    };
    QPainterPath path;
    path.moveTo(toward(vertices.front(), vertices.back()));
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        const auto next = (index + 1U) % vertices.size();
        path.quadTo(vertices[index], toward(vertices[index], vertices[next]));
        path.lineTo(toward(vertices[next], vertices[index]));
    }
    path.closeSubpath();
    return path;
}

} // namespace

CadViewport::CadViewport(QWidget* parent) : QOpenGLWidget{parent} {
    // QOpenGLWidget renders into its own framebuffer. Request depth storage on
    // the widget itself as well as on the application default format; without
    // it, later triangles overwrite nearer surfaces and appear as radial
    // saw-tooth bands even though the CAD mesh is watertight.
    auto viewport_format = format();
    viewport_format.setDepthBufferSize(24);
    viewport_format.setStencilBufferSize(8);
    viewport_format.setSamples(4);
    setFormat(viewport_format);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(480, 360);
}

CadViewport::~CadViewport() {
    if (!initialized_)
        return;
    makeCurrent();
    vertex_array_.destroy();
    vertex_buffer_.destroy();
    index_buffer_.destroy();
    wire_index_buffer_.destroy();
    mesh_wire_index_buffer_.destroy();
    doneCurrent();
}

auto CadViewport::set_scene(RenderScene scene) -> void {
    scene_ = std::move(scene);
    selected_part_.reset();
    active_scene_ = 0;
    scene_dirty_ = true;
    fit_all();
    update();
}

auto CadViewport::clear_scene() -> void {
    scene_ = {};
    selected_part_.reset();
    active_scene_ = 0;
    scene_dirty_ = true;
    update();
}

auto CadViewport::fit_all() -> void {
    if (scene_.empty())
        return;
    target_ = scene_.center();
    distance_ = scene_.radius() * 2.7F;
    update();
}

auto CadViewport::fit_selected() -> void {
    if (!selected_part_ || *selected_part_ >= scene_.parts.size()) {
        fit_all();
        return;
    }
    const auto& part = scene_.parts[*selected_part_];
    target_ = (part.minimum + part.maximum) * 0.5F;
    distance_ = std::max(1.0F, (part.maximum - part.minimum).length() * 0.5F) * 2.7F;
    update();
}

auto CadViewport::set_standard_view(StandardView standard) -> void {
    switch (standard) {
    case StandardView::isometric:
        yaw_degrees_ = 38.0F;
        pitch_degrees_ = 24.0F;
        break;
    case StandardView::front:
        yaw_degrees_ = 0.0F;
        pitch_degrees_ = 0.0F;
        break;
    case StandardView::back:
        yaw_degrees_ = 180.0F;
        pitch_degrees_ = 0.0F;
        break;
    case StandardView::left:
        yaw_degrees_ = -90.0F;
        pitch_degrees_ = 0.0F;
        break;
    case StandardView::right:
        yaw_degrees_ = 90.0F;
        pitch_degrees_ = 0.0F;
        break;
    case StandardView::top:
        yaw_degrees_ = 0.0F;
        pitch_degrees_ = 89.9F;
        break;
    case StandardView::bottom:
        yaw_degrees_ = 0.0F;
        pitch_degrees_ = -89.9F;
        break;
    }
    update();
}

auto CadViewport::set_orthographic(bool enabled) -> void {
    orthographic_ = enabled;
    update();
}

auto CadViewport::set_display_mode(DisplayMode mode) -> void {
    display_mode_ = mode;
    update();
}

auto CadViewport::set_active_scene(std::size_t index) -> void {
    active_scene_ = scene_.scenes.empty() ? 0U : std::min(index, scene_.scenes.size() - 1U);
    update();
}

auto CadViewport::set_scene_lighting(bool enabled) -> void {
    scene_lighting_ = enabled;
    update();
}

auto CadViewport::set_debug_overlay(bool enabled) -> void {
    debug_overlay_ = enabled;
    update();
}

auto CadViewport::orientation_cube_rect() const -> QRect {
    return QRect{width() - 132, 20, 104, 104};
}

auto CadViewport::draw_hud(QPainter& painter) const -> void {
    painter.setRenderHint(QPainter::Antialiasing);
    const QRect cube = orientation_cube_rect();
    const QPoint center = cube.center();
    const auto tile = rounded_hexagon(QRectF{cube}.adjusted(1.0, 1.0, -1.0, -1.0), 8.0);
    painter.fillPath(tile, QColor{13, 24, 38, 232});
    painter.setPen(QPen{QColor{"#48627f"}, 1.0});
    painter.drawPath(tile);

    const QPolygon top{{center.x(), cube.top() + 16}, {cube.right() - 17, cube.top() + 36},
                       {center.x(), cube.top() + 55}, {cube.left() + 17, cube.top() + 36}};
    const QPolygon front{{cube.left() + 17, cube.top() + 36}, {center.x(), cube.top() + 55},
                         {center.x(), cube.bottom() - 16}, {cube.left() + 17, cube.bottom() - 36}};
    const QPolygon right{{center.x(), cube.top() + 55}, {cube.right() - 17, cube.top() + 36},
                         {cube.right() - 17, cube.bottom() - 36}, {center.x(), cube.bottom() - 16}};
    painter.setPen(QPen{QColor{"#75849b"}, 1.0});
    painter.setBrush(QColor{"#354154"});
    painter.drawPolygon(top);
    painter.setBrush(QColor{"#293548"});
    painter.drawPolygon(front);
    painter.setBrush(QColor{"#202b3d"});
    painter.drawPolygon(right);
    painter.setPen(QColor{"#dbeafe"});
    painter.drawText(top.boundingRect(), Qt::AlignCenter, QStringLiteral("Top"));
    painter.drawText(front.boundingRect(), Qt::AlignCenter, QStringLiteral("F"));
    painter.drawText(right.boundingRect(), Qt::AlignCenter, QStringLiteral("R"));

    if (!debug_overlay_)
        return;
    const auto triangle_count = scene_.indices.size() / 3U;
    const QString text = QStringLiteral("DEBUG\n%1 parts  %2 vertices  %3 triangles\nyaw %4°  pitch %5°  distance %6\n%7  %8")
                             .arg(static_cast<qulonglong>(scene_.parts.size()))
                             .arg(static_cast<qulonglong>(scene_.vertices.size()))
                             .arg(static_cast<qulonglong>(triangle_count))
                             .arg(yaw_degrees_, 0, 'f', 1)
                             .arg(pitch_degrees_, 0, 'f', 1)
                             .arg(distance_, 0, 'f', 2)
                             .arg(orthographic_ ? QStringLiteral("orthographic") : QStringLiteral("perspective"))
                             .arg([this] {
                                 switch (display_mode_) {
                                 case DisplayMode::solid: return QStringLiteral("solid");
                                 case DisplayMode::solid_with_mesh:
                                     return QStringLiteral("solid + mesh edges");
                                 case DisplayMode::cad_wireframe:
                                     return QStringLiteral("CAD wireframe");
                                 case DisplayMode::mesh_wireframe:
                                     return QStringLiteral("triangle mesh");
                                 }
                                 return QStringLiteral("solid");
                             }());
    const QRect panel{20, height() - 96, 390, 74};
    QPainterPath debug_tile;
    debug_tile.addRoundedRect(QRectF{panel}, 10.0, 10.0);
    painter.fillPath(debug_tile, QColor{5, 12, 22, 218});
    painter.setPen(QColor{"#67e8f9"});
    painter.drawText(panel.adjusted(12, 8, -8, -6), Qt::AlignLeft | Qt::AlignVCenter, text);
}

auto CadViewport::select_part(std::optional<std::size_t> index) -> void {
    if (index && *index >= scene_.parts.size())
        index.reset();
    if (selected_part_ == index)
        return;
    selected_part_ = index;
    update();
    if (selection_changed)
        selection_changed(selected_part_);
}

auto CadViewport::save_screenshot(const QString& path) -> bool {
    return grabFramebuffer().save(path, "PNG", 95);
}

auto CadViewport::initializeGL() -> void {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mesh_program_.addShaderFromSourceCode(QOpenGLShader::Vertex, vertex_shader);
    mesh_program_.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment_shader);
    mesh_program_.link();
    line_program_.addShaderFromSourceCode(QOpenGLShader::Vertex, line_vertex_shader);
    line_program_.addShaderFromSourceCode(QOpenGLShader::Fragment, line_fragment_shader);
    line_program_.link();
    vertex_array_.create();
    vertex_buffer_.create();
    index_buffer_.create();
    wire_index_buffer_.create();
    mesh_wire_index_buffer_.create();
    initialized_ = true;
    scene_dirty_ = true;
}

auto CadViewport::resizeGL(int width, int height) -> void {
    glViewport(0, 0, width, height);
}

auto CadViewport::update_matrices() -> void {
    const float aspect = static_cast<float>(std::max(1, width())) /
                         static_cast<float>(std::max(1, height()));
    projection_.setToIdentity();
    const float radius = scene_.empty() ? 10.0F : scene_.radius();
    if (orthographic_) {
        const float extent = std::max(radius * 1.15F, distance_ * 0.42F);
        projection_.ortho(-extent * aspect, extent * aspect, -extent, extent, 0.01F,
                          std::max(1000.0F, radius * 20.0F));
    } else {
        projection_.perspective(36.0F, aspect, std::max(0.01F, radius * 0.001F),
                                std::max(1000.0F, radius * 30.0F));
    }
    const float yaw = qDegreesToRadians(yaw_degrees_);
    const float pitch = qDegreesToRadians(pitch_degrees_);
    const QVector3D direction{std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                              std::cos(pitch) * std::cos(yaw)};
    const QVector3D eye = target_ + direction * distance_;
    view_.setToIdentity();
    view_.lookAt(eye, target_, QVector3D{0.0F, 1.0F, 0.0F});
}

auto CadViewport::upload_scene() -> void {
    scene_dirty_ = false;
    vertex_array_.bind();
    vertex_buffer_.bind();
    vertex_buffer_.setUsagePattern(QOpenGLBuffer::StaticDraw);
    vertex_buffer_.allocate(scene_.vertices.data(),
                            static_cast<int>(scene_.vertices.size() * sizeof(RenderVertex)));
    index_buffer_.bind();
    index_buffer_.setUsagePattern(QOpenGLBuffer::StaticDraw);
    index_buffer_.allocate(scene_.indices.data(),
                           static_cast<int>(scene_.indices.size() * sizeof(std::uint32_t)));
    wire_index_buffer_.bind();
    wire_index_buffer_.setUsagePattern(QOpenGLBuffer::StaticDraw);
    wire_index_buffer_.allocate(
        scene_.wire_indices.data(),
        static_cast<int>(scene_.wire_indices.size() * sizeof(std::uint32_t)));
    mesh_wire_index_buffer_.bind();
    mesh_wire_index_buffer_.setUsagePattern(QOpenGLBuffer::StaticDraw);
    mesh_wire_index_buffer_.allocate(
        scene_.mesh_wire_indices.data(),
        static_cast<int>(scene_.mesh_wire_indices.size() * sizeof(std::uint32_t)));
    mesh_program_.bind();
    mesh_program_.enableAttributeArray(0);
    mesh_program_.setAttributeBuffer(0, GL_FLOAT, offsetof(RenderVertex, position), 3,
                                     sizeof(RenderVertex));
    mesh_program_.enableAttributeArray(1);
    mesh_program_.setAttributeBuffer(1, GL_FLOAT, offsetof(RenderVertex, normal), 3,
                                     sizeof(RenderVertex));
    mesh_program_.enableAttributeArray(2);
    mesh_program_.setAttributeBuffer(2, GL_FLOAT, offsetof(RenderVertex, color), 4,
                                     sizeof(RenderVertex));
    mesh_program_.release();
    mesh_wire_index_buffer_.release();
    wire_index_buffer_.release();
    index_buffer_.release();
    vertex_buffer_.release();
    vertex_array_.release();
}

auto CadViewport::draw_grid(const QMatrix4x4& view_projection) -> void {
    const float radius = scene_.empty() ? 50.0F : std::max(50.0F, scene_.radius() * 2.0F);
    const float step = std::pow(10.0F, std::floor(std::log10(std::max(1.0F, radius / 10.0F))));
    std::vector<QVector3D> lines;
    for (float coordinate = -radius; coordinate <= radius + step * 0.5F; coordinate += step) {
        lines.insert(lines.end(), {{-radius, coordinate, 0.0F}, {radius, coordinate, 0.0F},
                                   {coordinate, -radius, 0.0F}, {coordinate, radius, 0.0F}});
    }
    QOpenGLBuffer buffer{QOpenGLBuffer::VertexBuffer};
    buffer.create();
    buffer.bind();
    buffer.allocate(lines.data(), static_cast<int>(lines.size() * sizeof(QVector3D)));
    line_program_.bind();
    line_program_.setUniformValue("mvp", view_projection);
    line_program_.setUniformValue("line_color", QVector4D{0.19F, 0.23F, 0.28F, 0.72F});
    line_program_.enableAttributeArray(0);
    line_program_.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(QVector3D));
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size()));
    line_program_.disableAttributeArray(0);
    line_program_.release();
    buffer.release();
    buffer.destroy();
}

auto CadViewport::draw_edges(bool mesh, const QVector4D& color) -> void {
    auto& buffer = mesh ? mesh_wire_index_buffer_ : wire_index_buffer_;
    buffer.bind();
    line_program_.bind();
    line_program_.setUniformValue("mvp", projection_ * view_);
    for (std::size_t index = 0; index < scene_.parts.size(); ++index) {
        const auto& part = scene_.parts[index];
        line_program_.setUniformValue(
            "line_color", selected_part_ == index ? QVector4D{0.0F, 0.75F, 1.0F, 1.0F} : color);
        const auto first = mesh ? part.first_index * 2U : part.first_wire_index;
        const auto count = mesh ? part.index_count * 2U : part.wire_index_count;
        const auto offset = reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(first) * sizeof(std::uint32_t));
        glDrawElements(GL_LINES, static_cast<GLsizei>(count), GL_UNSIGNED_INT, offset);
    }
    line_program_.release();
    buffer.release();
}

auto CadViewport::paintGL() -> void {
    // QPainter draws the native HUD after the OpenGL pass and may change fixed
    // function state. Restore depth state on every frame; relying on the state
    // set once in initializeGL lets later triangles overwrite nearer geometry,
    // which is visible as radial bands on revolved CAD solids.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glClearDepth(1.0);
    glDepthRange(0.0, 1.0);
    glDisable(GL_CULL_FACE);
    QVector4D background{0.035F, 0.047F, 0.063F, 1.0F};
    if (!scene_.scenes.empty()) {
        const auto& preset = scene_.scenes[active_scene_].background;
        if (preset == QStringLiteral("SKY_DAY"))
            background = QVector4D{0.20F, 0.31F, 0.43F, 1.0F};
        else if (preset == QStringLiteral("NIGHT"))
            background = QVector4D{0.008F, 0.012F, 0.028F, 1.0F};
        else if (preset == QStringLiteral("TRANSPARENT"))
            background = QVector4D{0.0F, 0.0F, 0.0F, 0.0F};
    }
    glClearColor(background.x(), background.y(), background.z(), background.w());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    update_matrices();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    draw_grid(projection_ * view_);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    if (scene_.empty()) {
        QPainter painter{this};
        painter.setPen(QColor{148, 163, 184});
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Compile an ICAD model to preview it"));
        draw_hud(painter);
        return;
    }
    if (scene_dirty_)
        upload_scene();
    vertex_array_.bind();
    const bool solid = display_mode_ == DisplayMode::solid ||
                       display_mode_ == DisplayMode::solid_with_mesh;
    if (solid) {
        if (display_mode_ == DisplayMode::solid_with_mesh) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0F, 1.0F);
        }
        index_buffer_.bind();
        mesh_program_.bind();
        mesh_program_.setUniformValue("mvp", projection_ * view_);
        mesh_program_.setUniformValue("model_view", view_);
        const auto* scene_info = scene_.scenes.empty() ? nullptr : &scene_.scenes[active_scene_];
        const int light_count = scene_info == nullptr || !scene_lighting_
                                    ? 0
                                    : std::min(4, static_cast<int>(scene_info->lights.size()));
        mesh_program_.setUniformValue("light_count", light_count);
        mesh_program_.setUniformValue("scene_radius", scene_.radius());
        for (int index = 0; index < light_count; ++index) {
            const auto& light = scene_info->lights[static_cast<std::size_t>(index)];
            const QByteArray suffix = QByteArray::number(index) + ']';
            mesh_program_.setUniformValue((QByteArray{"light_is_point["} + suffix).constData(),
                                          light.point ? 1 : 0);
            mesh_program_.setUniformValue((QByteArray{"light_position["} + suffix).constData(),
                                          view_.map(light.position));
            mesh_program_.setUniformValue((QByteArray{"light_color["} + suffix).constData(),
                                          light.color);
            mesh_program_.setUniformValue((QByteArray{"light_intensity["} + suffix).constData(),
                                          light.intensity);
        }
        for (std::size_t index = 0; index < scene_.parts.size(); ++index) {
            const auto& part = scene_.parts[index];
            mesh_program_.setUniformValue("selected", selected_part_ == index ? 1.0F : 0.0F);
            const auto offset = reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(part.first_index) * sizeof(std::uint32_t));
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(part.index_count), GL_UNSIGNED_INT,
                           offset);
        }
        mesh_program_.release();
        index_buffer_.release();
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    if (!solid || display_mode_ == DisplayMode::solid_with_mesh) {
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        const bool mesh = display_mode_ != DisplayMode::cad_wireframe;
        draw_edges(mesh, solid ? QVector4D{0.02F, 0.04F, 0.07F, 0.56F}
                               : QVector4D{0.68F, 0.76F, 0.88F, 0.92F});
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
    }
    vertex_array_.release();

    QPainter painter{this};
    draw_hud(painter);
}

auto CadViewport::mousePressEvent(QMouseEvent* event) -> void {
    if (event->button() == Qt::LeftButton && orientation_cube_rect().contains(event->position().toPoint())) {
        const QRect cube = orientation_cube_rect();
        if (event->position().y() < cube.top() + cube.height() / 2) {
            set_standard_view(StandardView::top);
        } else if (event->position().x() < cube.center().x()) {
            set_standard_view(StandardView::front);
        } else {
            set_standard_view(StandardView::right);
        }
        event->accept();
        return;
    }
    press_position_ = event->position().toPoint();
    last_position_ = press_position_;
    dragging_ = event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton;
    panning_ = event->button() == Qt::MiddleButton ||
               (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::ShiftModifier));
    event->accept();
}

auto CadViewport::mouseMoveEvent(QMouseEvent* event) -> void {
    if (!dragging_)
        return;
    const QPoint current = event->position().toPoint();
    const QPoint delta = current - last_position_;
    last_position_ = current;
    if (panning_) {
        const float scale = std::max(0.001F, distance_ * 0.0015F);
        QMatrix4x4 inverse = view_.inverted();
        const QVector3D right = inverse.mapVector(QVector3D{1.0F, 0.0F, 0.0F});
        const QVector3D up = inverse.mapVector(QVector3D{0.0F, 1.0F, 0.0F});
        target_ += (-right * static_cast<float>(delta.x()) + up * static_cast<float>(delta.y())) * scale;
    } else {
        yaw_degrees_ += static_cast<float>(delta.x()) * 0.35F;
        pitch_degrees_ = std::clamp(pitch_degrees_ - static_cast<float>(delta.y()) * 0.35F,
                                    -89.0F, 89.0F);
    }
    update();
}

auto CadViewport::mouseReleaseEvent(QMouseEvent* event) -> void {
    const QPoint released = event->position().toPoint();
    if (event->button() == Qt::LeftButton && (released - press_position_).manhattanLength() < 4)
        select_part(pick_part(released));
    dragging_ = false;
    panning_ = false;
    event->accept();
}

auto CadViewport::wheelEvent(QWheelEvent* event) -> void {
    const float turns = static_cast<float>(event->angleDelta().y()) / 120.0F;
    distance_ = std::clamp(distance_ * std::pow(0.84F, turns), 0.01F, 1.0e8F);
    update();
    event->accept();
}

auto CadViewport::pick_part(const QPoint& point) const -> std::optional<std::size_t> {
    if (scene_.empty())
        return std::nullopt;
    const QMatrix4x4 matrix = projection_ * view_;
    const auto project = [&](const QVector3D& source) {
        const QVector4D clip = matrix * QVector4D{source, 1.0F};
        if (qFuzzyIsNull(clip.w()))
            return QVector3D{};
        const QVector3D ndc = clip.toVector3DAffine();
        return QVector3D{(ndc.x() + 1.0F) * 0.5F * static_cast<float>(width()),
                         (1.0F - ndc.y()) * 0.5F * static_cast<float>(height()), ndc.z()};
    };
    const QPointF target{point};
    float closest = std::numeric_limits<float>::max();
    std::optional<std::size_t> hit;
    for (std::size_t part_index = 0; part_index < scene_.parts.size(); ++part_index) {
        const auto& part = scene_.parts[part_index];
        const std::size_t end = static_cast<std::size_t>(part.first_index + part.index_count);
        for (std::size_t triangle = part.first_index; triangle + 2 < end; triangle += 3) {
            const QVector3D first = project(scene_.vertices[scene_.indices[triangle]].position);
            const QVector3D second = project(scene_.vertices[scene_.indices[triangle + 1]].position);
            const QVector3D third = project(scene_.vertices[scene_.indices[triangle + 2]].position);
            if (!point_in_triangle(target, first.toPointF(), second.toPointF(), third.toPointF()))
                continue;
            const float depth = (first.z() + second.z() + third.z()) / 3.0F;
            if (depth < closest) {
                closest = depth;
                hit = part_index;
            }
        }
    }
    return hit;
}

} // namespace icad::desktop
