#include "icad/compiler/compiler.hpp"
#include "icad/materials/library.hpp"
#include "icad/scene/exporter.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view source = R"ICAD(
PROJECT scene_test
UNITS mm
POINT3 lamp_position 200 mm 150 mm 300 mm
MATERIAL shell
PRESET CARBON_FIBER
BASE_COLOR 0.04 0.05 0.06 1
METALLIC 0.3
ROUGHNESS 0.22
TEXTURE_SCALE 12 mm
UV_MODE BOX
END
BODY product
MATERIAL shell
FEATURE case
TYPE BOX
WIDTH 100 mm
DEPTH 60 mm
HEIGHT 20 mm
END
END
SCENE turntable
DURATION 4 s
FPS 30
BACKGROUND STUDIO
LOOP 3
LIGHT key POINT COLOR 1 0.92 0.8 INTENSITY 4 AT lamp_position
EVENT 2 s midpoint
TRACK orbit CAMERA main
EASING EASE_IN_OUT
KEYFRAME 0 s POSITION 0 mm 0 mm 0 mm ROTATION 0 deg 0 deg 0 deg
KEYFRAME 4 s POSITION 0 mm 0 mm 0 mm ROTATION 0 deg 0 deg 360 deg
END
TRACK reveal VISIBILITY product
EASING STEP
KEYFRAME 0 s VISIBLE OFF
KEYFRAME 1 s VISIBLE ON
END
END
)ICAD";

[[nodiscard]] auto contains(const std::string& text, std::string_view value) -> bool {
    return text.find(value) != std::string::npos;
}

} // namespace

auto main() -> int {
    if (icad::materials::all().size() != 26) {
        std::cerr << "predefined material catalog size changed unexpectedly\n";
        return 1;
    }
    const auto result = icad::compiler::compile(source);
    if (!result.ok() || result.ir_project->materials.size() != 1 ||
        result.ir_project->scenes.size() != 1 ||
        result.ir_project->materials.front().texture_scale_mm != 12.0 ||
        result.ir_project->materials.front().roughness != 0.22 ||
        result.ir_project->scenes.front().loop_count != 3 ||
        result.ir_project->scenes.front().lights.size() != 1 ||
        result.ir_project->scenes.front().events.size() != 1 ||
        result.ir_project->scenes.front().tracks.size() != 2 ||
        result.ir_project->scenes.front().tracks.front().keyframes.size() != 2) {
        std::cerr << "material and scene source did not lower to canonical IR\n";
        return 1;
    }
    const auto base = std::filesystem::current_path() / "scene-test-output" / "turntable";
    const auto exported = icad::scene::export_web_bundle(*result.ir_project, base);
    if (!exported.success || exported.materials != 1 || exported.scenes != 1 ||
        exported.tracks != 2 || exported.keyframes != 4) {
        std::cerr << "web scene bundle export failed\n";
        return 1;
    }
    std::ifstream scene{base.string() + ".scene.json", std::ios::binary};
    const std::string content{std::istreambuf_iterator<char>{scene},
                              std::istreambuf_iterator<char>{}};
    std::ifstream library{base.parent_path() / "icad-viewer.js", std::ios::binary};
    const std::string library_content{std::istreambuf_iterator<char>{library},
                                      std::istreambuf_iterator<char>{}};
    if (!contains(content, "\"preset\":\"CARBON_FIBER\"") ||
        !contains(content, "data:image/bmp;base64,") ||
        !contains(content, "\"textureScaleMm\":12") ||
        !contains(content, "\"uvMode\":\"BOX\"") ||
        !contains(content, "\"loopCount\":3") ||
        !contains(content, "\"easing\":\"EASE_IN_OUT\"") ||
        !contains(content, "\"visible\":false") ||
        !contains(content, "\"name\":\"midpoint\"") ||
        !contains(content, "\"name\":\"turntable\"") ||
        !contains(library_content, "let playing = false") ||
        !contains(library_content, "icad-view-cube") ||
        !contains(library_content, "icad-component-menu") ||
        !contains(library_content, "pointInTriangle") ||
        !std::filesystem::exists(base.string() + ".html") ||
        !std::filesystem::exists(base.string() + ".viewer.js") ||
        !std::filesystem::exists(base.parent_path() / "icad-viewer.js")) {
        std::cerr << "scene bundle is missing embedded texture, animation, or viewer files\n";
        return 1;
    }

    const auto bad_material = icad::compiler::compile(
        "PROJECT bad\nUNITS mm\nMATERIAL finish UNOBTAINIUM\n");
    if (bad_material.ok() || bad_material.diagnostics.front().code != "ICAD-S0011") {
        std::cerr << "unknown material preset was accepted\n";
        return 1;
    }
    const auto bad_timeline = icad::compiler::compile(
        "PROJECT bad_scene\nUNITS mm\nSCENE demo\nDURATION 1 s\nFPS 30\nBACKGROUND STUDIO\n"
        "TRACK orbit CAMERA main\n"
        "KEYFRAME 1 s POSITION 0 mm 0 mm 0 mm ROTATION 0 deg 0 deg 0 deg\n"
        "KEYFRAME 0 s POSITION 0 mm 0 mm 0 mm ROTATION 0 deg 0 deg 0 deg\n"
        "END\nEND\n");
    if (bad_timeline.ok()) {
        std::cerr << "non-increasing animation timeline was accepted\n";
        return 1;
    }
    return 0;
}
