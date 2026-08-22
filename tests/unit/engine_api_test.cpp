#include "icad/engine/c_api.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr std::string_view source =
    "PROJECT engine_api\nUNITS mm\nBODY plate\n"
    "SKETCH base ON PLANE XY\nCIRCLE 0 mm 0 mm 8 mm\nEND\n"
    "PAD solid FROM base DEPTH 3 mm NEW\nEND\n";

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

auto main() -> int {
    const auto path = std::filesystem::temp_directory_path() / "icad-engine-api-test.icad";
    {
        std::ofstream output{path};
        output << source;
    }
    auto* session = icad_engine_session_create(path.string().c_str());
    if (session == nullptr || icad_engine_session_ready(session) != 1)
        return fail("native C engine session did not initialize");

    std::vector<std::thread> workers;
    std::vector<std::string> payloads(4);
    for (std::size_t index = 0; index < payloads.size(); ++index) {
        workers.emplace_back([&, index] {
            char* payload =
                icad_engine_session_preview_json(session, source.data(), source.size());
            if (payload != nullptr) {
                payloads[index] = payload;
                icad_engine_string_free(payload);
            }
        });
    }
    for (auto& worker : workers)
        worker.join();
    bool received_model = false;
    for (const auto& payload : payloads) {
        if (!payload.contains("\"success\":true")) {
            icad_engine_session_destroy(session);
            return fail("thread-safe native preview failed");
        }
        received_model = received_model || payload.contains("\"model\":");
    }
    if (!received_model) {
        icad_engine_session_destroy(session);
        return fail("native preview did not return an initial model payload");
    }
    char* path_value = icad_engine_session_source_path(session);
    const bool path_ok = path_value != nullptr && std::string_view{path_value}.ends_with(".icad");
    icad_engine_string_free(path_value);
    icad_engine_session_destroy(session);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return path_ok ? 0 : fail("native engine did not expose the source path");
}
