#include "icad/compiler/compiler.hpp"
#include "icad/viewer/live_session.hpp"

#include <filesystem>
#include <fstream>
#include <chrono>
#include <iostream>
#include <string_view>

namespace {

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

auto write(const std::filesystem::path& path, std::string_view source) -> bool {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    return static_cast<bool>(output << source);
}

} // namespace

auto main() -> int {
    const auto root = std::filesystem::current_path() / "importer-test-output";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / "parts");
    const auto main_path = root / "assembly.icad";
    const auto body_path = root / "parts" / "bracket.icad";
    if (!write(body_path,
               "BODY bracket\nFEATURE shape\nTYPE BOX\nWIDTH 10 mm\nDEPTH 20 mm\n"
               "HEIGHT 30 mm\nEND\nEND\n"))
        return fail("could not create imported module fixture");

    constexpr std::string_view source =
        "PROJECT modular\nUNITS mm\nIMPORT \"parts/bracket.icad\"\n";
    if (!write(main_path, source))
        return fail("could not create modular main source fixture");
    const icad::compiler::CompileOptions options{
        .build_topology = true,
        .imports = {.source_path = main_path, .project_root = root}};
    const auto imported = icad::compiler::compile(source, options);
    if (!imported.ok() || imported.imported_files.size() != 1 ||
        imported.ir_project->bodies.size() != 1 ||
        imported.ir_project->bodies.front().name != "bracket")
        return fail("IMPORT did not compose the ICAD body module");

    icad::viewer::LiveSession session{main_path};
    const auto first_preview = session.preview(source);
    const auto old_time = std::filesystem::last_write_time(body_path);
    if (!write(body_path,
               "BODY bracket\nFEATURE shape\nTYPE BOX\nWIDTH 12 mm\nDEPTH 20 mm\n"
               "HEIGHT 30 mm\nEND\nEND\n"))
        return fail("could not update imported module fixture");
    std::filesystem::last_write_time(body_path, old_time + std::chrono::seconds{1});
    const auto updated_preview = session.preview(source);
    if (!first_preview.success || !updated_preview.success || updated_preview.unchanged ||
        updated_preview.revision != first_preview.revision + 1)
        return fail("live preview did not invalidate a changed imported module");

    const auto injected = icad::compiler::compile(
        "PROJECT modular\nUNITS mm\nINJECT parts/bracket.icad\n", options);
    if (!injected.ok() || injected.ir_project->bodies.size() != 1)
        return fail("INJECT alias did not compose the ICAD body module");

    const auto escaped = icad::compiler::compile(
        "PROJECT modular\nUNITS mm\nIMPORT \"../outside.icad\"\n", options);
    if (escaped.ok() || escaped.diagnostics.empty() ||
        escaped.diagnostics.front().code != "ICAD-I0003")
        return fail("project-root escape was not rejected");

    if (!write(root / "cycle.icad", "IMPORT \"cycle.icad\"\n"))
        return fail("could not create cyclic module fixture");
    const auto cycle = icad::compiler::compile(
        "PROJECT modular\nUNITS mm\nIMPORT \"cycle.icad\"\n", options);
    if (cycle.ok() || cycle.diagnostics.empty() ||
        cycle.diagnostics.front().code != "ICAD-I0005")
        return fail("cyclic import was not rejected");

    std::filesystem::remove_all(root, ignored);
    return 0;
}
