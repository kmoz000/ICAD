#include "scene_model.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

constexpr std::string_view coplanar_quad = R"JSON({
  "format":"ICAD_SCENE",
  "project":"wireframe_quad",
  "materials":[],
  "scenes":[{
    "name":"inspection","duration":1,"fps":30,"background":"SKY_DAY",
    "lights":[{"name":"key","kind":"POINT","color":[1,0.8,0.6],"intensity":3,
                "positionMm":[2,3,4]}]
  }],
  "parts":[{
    "name":"plate","body":"plate","material":"",
    "vertices":[[0,0,0],[1,0,0],[1,1,0],[0,1,0]],
    "triangles":[[0,1,2],[0,2,3]]
  }]
})JSON";

constexpr std::string_view creased_pair = R"JSON({
  "format":"ICAD_SCENE",
  "project":"wireframe_crease",
  "materials":[],
  "scenes":[],
  "parts":[{
    "name":"fold","body":"fold","material":"",
    "vertices":[[0,0,0],[1,0,0],[0,1,0],[0,0,1]],
    "triangles":[[0,1,2],[0,3,1]]
  }]
})JSON";

constexpr std::string_view smooth_pair = R"JSON({
  "format":"ICAD_SCENE",
  "project":"smooth_normals",
  "materials":[],
  "scenes":[],
  "parts":[{
    "name":"bend","body":"bend","material":"",
    "vertices":[[0,0,0],[1,0,0],[0,1,0],[0,0.9396926,0.3420201]],
    "triangles":[[0,1,2],[0,1,3]]
  }]
})JSON";

} // namespace

auto main() -> int {
    const auto quad = icad::desktop::parse_render_scene(coplanar_quad);
    if (!quad.ok() || quad.scene.parts.size() != 1 || quad.scene.indices.size() != 6 ||
        quad.scene.wire_indices.size() != 8 || quad.scene.mesh_wire_indices.size() != 12 ||
        quad.scene.scenes.size() != 1 || quad.scene.scenes[0].background != "SKY_DAY" ||
        quad.scene.scenes[0].lights.size() != 1 || !quad.scene.scenes[0].lights[0].point ||
        quad.scene.scenes[0].lights[0].position != QVector3D{2.0F, 3.0F, 4.0F} ||
        QVector3D::dotProduct(quad.scene.vertices[0].normal,
                              quad.scene.vertices[3].normal) < 0.999F) {
        return fail("CAD wireframe did not suppress a coplanar tessellation diagonal");
    }
    const auto crease = icad::desktop::parse_render_scene(creased_pair);
    if (!crease.ok() || crease.scene.parts.size() != 1 || crease.scene.indices.size() != 6 ||
        crease.scene.wire_indices.size() != 10 ||
        std::abs(QVector3D::dotProduct(crease.scene.vertices[0].normal,
                                      crease.scene.vertices[3].normal)) > 0.001F) {
        return fail("CAD renderer did not preserve a physical crease edge and split normal");
    }
    const auto smooth = icad::desktop::parse_render_scene(smooth_pair);
    if (!smooth.ok() || smooth.scene.wire_indices.size() != 8 ||
        QVector3D::dotProduct(smooth.scene.vertices[0].normal,
                              smooth.scene.vertices[3].normal) < 0.999F) {
        return fail("CAD renderer did not share angle-weighted normals across a smooth bend");
    }
    return 0;
}
