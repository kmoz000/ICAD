#include "icad/viewer/live_session.hpp"
#include "icad/cad/model.hpp"
#include "icad/compiler/compiler.hpp"
#include "icad/scene/exporter.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

auto main(int argc, char** argv) -> int {
    if (argc != 2)
        return fail("expected robotic-arm .icad source path");
    icad::viewer::LiveSession session{std::filesystem::path{argv[1]}};
    if (!session.ready())
        return fail(session.error());

    const auto initial = session.preview(session.source());
    if (!initial.success || initial.bodies != 10 || initial.recomputed_bodies != 10)
        return fail("initial large-project preview did not compile all components");
    const auto unchanged = session.preview(session.source());
    if (!unchanged.success || !unchanged.unchanged || unchanged.recomputed_bodies != 0)
        return fail("unchanged large-project preview was not reused");

    std::string edited = session.source();
    constexpr std::string_view original = "PARAMETER jaw_depth 14 mm";
    const auto position = edited.find(original);
    if (position == std::string::npos)
        return fail("robotic-arm refresh parameter is missing");
    edited.replace(position, original.size(), "PARAMETER jaw_depth 15 mm");
    const auto refreshed = session.preview(edited);
    if (!refreshed.success || refreshed.recomputed_bodies == 0 ||
        refreshed.reused_bodies == 0 ||
        refreshed.recomputed_bodies + refreshed.reused_bodies != 10)
        return fail("large-project edit did not selectively reuse body meshes");
    if (refreshed.model_json.empty() || initial.parallel_workers < 1)
        return fail("large-project refresh did not return a renderable parallel model");
    const auto full_compilation = icad::compiler::compile(edited);
    if (!full_compilation.ok())
        return fail("clean comparison compilation failed");
    const auto full_model = icad::cad::build_model(*full_compilation.ir_project);
    const auto full_json =
        icad::scene::web_model_json(*full_compilation.ir_project, full_model);
    if (refreshed.model_json != full_json)
        return fail("incremental preview diverged from a clean delivery-model rebuild");

    std::cout << "LIVE_REFRESH initial_ms=" << initial.milliseconds
              << " unchanged_ms=" << unchanged.milliseconds
              << " edited_ms=" << refreshed.milliseconds
              << " recomputed=" << refreshed.recomputed_bodies
              << " reused=" << refreshed.reused_bodies
              << " workers=" << initial.parallel_workers << '\n';
    return 0;
}
