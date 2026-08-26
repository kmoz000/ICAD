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

constexpr std::string_view connection_source = R"ICAD(
PROJECT engine_connection
UNITS mm
TOLERANCE LINEAR 0.01 mm ANGULAR 0.1 deg
POINT3 cover_origin 0 mm 0 mm 10 mm
POINT3 seat 5 mm 5 mm 10 mm
VECTOR up 0 0 1
VECTOR down 0 0 -1
MATERIAL alloy ALUMINUM
BODY base
MATERIAL alloy
FEATURE stock
TYPE BOX
WIDTH 10 mm
DEPTH 10 mm
HEIGHT 10 mm
END
END
INSTANCE cover OF base AT cover_origin ROTATION 0 deg 0 deg 0 deg
MATE seated FACE base Z_MAX cover Z_MIN OFFSET 0 mm
INTERFACE base_flange BODY base AT seat AXIS up TYPE FLANGE SIZE 10 mm
INTERFACE cover_flange BODY cover AT seat AXIS down TYPE FLANGE SIZE 10 mm
CONNECT mount base_flange cover_flange METHOD BOLTED STANDARD ISO_4762 FASTENER M6 CLEARANCE 0.01 mm AUTO
)ICAD";

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

    const auto connection_path =
        std::filesystem::temp_directory_path() / "icad-engine-connection-test.icad";
    {
        std::ofstream output{connection_path};
        output << connection_source;
    }
    auto* connection_session = icad_engine_session_create(connection_path.string().c_str());
    if (connection_session == nullptr || icad_engine_session_ready(connection_session) != 1)
        return fail("native connection session did not initialize");
    char* valid_value = icad_engine_session_preview_json(
        connection_session, connection_source.data(), connection_source.size());
    const std::string valid_payload = valid_value == nullptr ? std::string{} : valid_value;
    icad_engine_string_free(valid_value);
    if (!valid_payload.contains("\"success\":true") ||
        !valid_payload.contains("\"model\":")) {
        icad_engine_session_destroy(connection_session);
        return fail("native engine bridge did not return the valid seated preview");
    }
    std::string resized{connection_source};
    const auto height = resized.find("HEIGHT 10 mm");
    resized.replace(height, std::string_view{"HEIGHT 10 mm"}.size(), "HEIGHT 100 mm");
    char* rejected_value =
        icad_engine_session_preview_json(connection_session, resized.data(), resized.size());
    const std::string rejected_payload =
        rejected_value == nullptr ? std::string{} : rejected_value;
    icad_engine_string_free(rejected_value);
    if (!rejected_payload.contains("\"success\":false") ||
        (!rejected_payload.contains("ICAD-M0013") &&
         !rejected_payload.contains("ICAD-V0001")) ||
        rejected_payload.contains("\"model\":")) {
        icad_engine_session_destroy(connection_session);
        return fail("native engine bridge exposed stale resized assembly geometry");
    }
    char* restored_value = icad_engine_session_preview_json(
        connection_session, connection_source.data(), connection_source.size());
    const std::string restored_payload =
        restored_value == nullptr ? std::string{} : restored_value;
    icad_engine_string_free(restored_value);
    icad_engine_session_destroy(connection_session);
    if (!restored_payload.contains("\"success\":true") ||
        !restored_payload.contains("\"unchanged\":true"))
        return fail("native engine bridge did not preserve the last valid assembly preview");

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(connection_path, ignored);
    return path_ok ? 0 : fail("native engine did not expose the source path");
}
