#include "icad/viewer/live_session.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view source =
    "PROJECT live_viewer\nUNITS mm\nBODY part\nFEATURE shape\nTYPE BOX\nWIDTH 10 mm\n"
    "DEPTH 20 mm\nHEIGHT 30 mm\nEND\nEND\n";

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

auto main() -> int {
    const auto root = std::filesystem::current_path() / "live-viewer-test-output";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const auto path = root / "live.icad";
    {
        std::ofstream output{path, std::ios::binary};
        output << source;
    }

    icad::viewer::LiveSession session{path};
    if (!session.ready() || session.source() != source)
        return fail("live session did not load .icad source");
    const auto first = session.preview(session.source());
    if (!first.success || first.bodies != 1 || first.revision != 1 ||
        first.recomputed_bodies != 1 || first.model_json.find("\"project\":\"live_viewer\"") ==
                                             std::string::npos ||
        first.model_json.find("\"parts\":[") == std::string::npos)
        return fail("valid source did not produce an in-memory live model");
    const auto unchanged = session.preview(session.source());
    if (!unchanged.success || !unchanged.unchanged || unchanged.recomputed_bodies != 0 ||
        unchanged.revision != first.revision)
        return fail("unchanged source did not reuse the live preview");
    const auto invalid = session.preview(std::string{source} + "$\n");
    if (invalid.success || invalid.diagnostics.empty() ||
        invalid.diagnostics.front().code != "ICAD-L0001")
        return fail("invalid edit did not preserve the last valid preview and diagnostics");

    std::string changed{source};
    changed.replace(changed.find("WIDTH 10 mm"), std::string_view{"WIDTH 10 mm"}.size(),
                    "WIDTH 12 mm");
    const auto refreshed = session.preview(changed);
    if (!refreshed.success || refreshed.recomputed_bodies != 1 || refreshed.revision != 2)
        return fail("changed body did not refresh incrementally");
    const auto saved = session.save(changed);
    std::ifstream input{path, std::ios::binary};
    const std::string persisted{std::istreambuf_iterator<char>{input},
                                std::istreambuf_iterator<char>{}};
    if (!saved.success || persisted != changed)
        return fail("live editor save did not persist source atomically");
    const auto package = session.export_package(changed, root / "exported");
    if (!package.success || package.artifacts != 13 || package.components != 1 ||
        package.solids != 1 || !std::filesystem::exists(root / "exported/live.step") ||
        !std::filesystem::exists(root / "exported/live.scene.json") ||
        std::filesystem::exists(root / "exported/live.html"))
        return fail("live viewer did not export the complete artifact package");
    return 0;
}
