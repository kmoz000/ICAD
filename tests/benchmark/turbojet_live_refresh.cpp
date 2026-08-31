#include "icad/viewer/live_session.hpp"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {

auto fail(std::string_view message) -> int {
    std::cerr << "turbojet live-refresh benchmark failed: " << message << '\n';
    return 1;
}

template <typename Function>
auto elapsed_ms(Function&& function) {
    const auto start = std::chrono::steady_clock::now();
    auto result = function();
    const auto end = std::chrono::steady_clock::now();
    const double elapsed =
        std::chrono::duration<double, std::milli>(end - start).count();
    return std::pair{std::move(result), elapsed};
}

} // namespace

auto main(int argc, char** argv) -> int {
    if (argc != 2)
        return fail("expected the bench turbojet source path");

    icad::viewer::LiveSession session{std::filesystem::path{argv[1]}};
    if (!session.ready())
        return fail(session.error());

    auto [initial, initial_wall_ms] =
        elapsed_ms([&] { return session.preview(session.source()); });
    if (!initial.success || initial.bodies != 358 || initial.recomputed_bodies < 100 ||
        initial_wall_ms > 5000.0)
        return fail("initial high-detail preview exceeded the 5 s budget or was incomplete");
    const auto liner = std::ranges::find(initial.body_timings, "combustor_inner_liner",
                                         &icad::compiler::IncrementalBodyTiming::body);
    if (liner == initial.body_timings.end() || liner->triangles > 50000)
        return fail("combustor inner liner exceeded its preview complexity budget");

    auto [unchanged, unchanged_wall_ms] =
        elapsed_ms([&] { return session.preview(session.source()); });
    if (!unchanged.success || !unchanged.unchanged || unchanged.recomputed_bodies != 0 ||
        unchanged_wall_ms > 100.0)
        return fail("unchanged preview did not return from the cache within 100 ms");

    std::string edited = session.source();
    constexpr std::string_view disk_marker = "BODY compressor_rotor_disk_1";
    const auto disk = edited.find(disk_marker);
    const auto radius = edited.find("RADIUS 80 mm", disk);
    if (disk == std::string::npos || radius == std::string::npos)
        return fail("could not locate the deterministic rotor-disk edit marker");
    edited.replace(radius, std::string_view{"RADIUS 80 mm"}.size(), "RADIUS 80.1 mm");

    auto [refreshed, refreshed_wall_ms] =
        elapsed_ms([&] { return session.preview(edited); });
    if (!refreshed.success || refreshed.recomputed_bodies != 1 ||
        refreshed.reused_bodies + refreshed.recomputed_bodies < 100 ||
        refreshed_wall_ms > 2000.0)
        return fail("single-part edit missed incremental reuse or exceeded the 2 s budget");

    std::cout << "TURBOJET_LIVE_REFRESH initial_ms=" << initial_wall_ms
              << " unchanged_ms=" << unchanged_wall_ms
              << " edit_ms=" << refreshed_wall_ms
              << " compile_ms=" << initial.compile_ms
              << " analysis_ms=" << initial.analysis_ms
              << " validation_ms=" << initial.validation_ms
              << " serialization_ms=" << initial.serialization_ms
              << " frontend_ms=" << initial.frontend_ms
              << " fingerprint_ms=" << initial.fingerprint_ms
              << " geometry_ms=" << initial.geometry_ms
              << " merge_ms=" << initial.merge_ms
              << " bodies=" << initial.bodies
              << " reused_after_edit=" << refreshed.reused_bodies
              << " recomputed_after_edit=" << refreshed.recomputed_bodies << '\n';
    auto timings = initial.body_timings;
    std::ranges::sort(timings, std::greater{}, &icad::compiler::IncrementalBodyTiming::milliseconds);
    for (std::size_t index = 0; index < std::min<std::size_t>(12, timings.size()); ++index) {
        std::cout << "TURBOJET_BODY_TIMING body=" << timings[index].body
                  << " ms=" << timings[index].milliseconds
                  << " triangles=" << timings[index].triangles << '\n';
    }
    return 0;
}
