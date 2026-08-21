#include "icad/document/source.hpp"

#include <algorithm>
#include <barrier>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr std::string_view initial_source = R"ICAD(PROJECT DurablePart
UNITS mm
PARAMETER width 10 mm # agent-controlled width
PARAMETER depth 20 mm
MATERIAL finish ALUMINUM
BODY part
MATERIAL finish
FEATURE block
TYPE BOX
WIDTH width
DEPTH depth
HEIGHT 30 mm
END
END
)ICAD";

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

auto main() -> int {
    const auto workspace = std::filesystem::current_path() / "source-document-workspace";
    const auto outside = std::filesystem::current_path() / "source-document-outside";
    std::filesystem::remove_all(workspace);
    std::filesystem::remove_all(outside);
    std::filesystem::create_directories(workspace);
    std::filesystem::create_directories(outside);

    const auto created = icad::document::write_source(
        workspace, "designs/part.icad", std::string{initial_source},
        icad::document::absent_revision);
    if (!created.ok() || created.previous_revision != "absent" ||
        created.revision.size() != 16) {
        return fail("validated project creation failed");
    }
    const auto snapshot = icad::document::read_source(workspace, "designs/part.icad");
    if (!snapshot.ok() || snapshot.source != initial_source ||
        snapshot.revision != created.revision) {
        return fail("project snapshot is inconsistent");
    }

    const auto stale = icad::document::write_source(
        workspace, "designs/part.icad", std::string{initial_source}, "0000000000000000");
    if (stale.status != icad::document::SourceStatus::conflict ||
        icad::document::read_source(workspace, "designs/part.icad").revision != created.revision) {
        return fail("stale project update was not rejected");
    }
    const auto invalid = icad::document::write_source(
        workspace, "designs/part.icad", "PROJECT broken\n$", created.revision);
    if (invalid.status != icad::document::SourceStatus::invalid_source ||
        invalid.diagnostics.empty() ||
        icad::document::read_source(workspace, "designs/part.icad").revision != created.revision) {
        return fail("invalid project update was not rejected atomically");
    }

    const auto edited = icad::document::set_parameter(
        workspace, "designs/part.icad", "width", 25.0, "mm", created.revision);
    if (!edited.ok() || edited.revision == created.revision) {
        return fail("parameter transaction failed");
    }
    const auto edited_snapshot =
        icad::document::read_source(workspace, "designs/part.icad");
    if (!edited_snapshot.source.contains(
            "PARAMETER width 25 mm # agent-controlled width")) {
        return fail("parameter transaction did not preserve the source comment");
    }
    const auto history =
        icad::document::source_history(workspace, "designs/part.icad");
    if (!history.ok() || history.revisions.size() != 2 ||
        !std::ranges::contains(history.revisions, created.revision) ||
        !std::ranges::contains(history.revisions, edited.revision)) {
        return fail("persistent revision history is incomplete");
    }
    const auto restored = icad::document::restore_source(
        workspace, "designs/part.icad", created.revision, edited.revision);
    if (!restored.ok() || restored.revision != created.revision ||
        icad::document::read_source(workspace, "designs/part.icad").source != initial_source) {
        return fail("revision restore failed");
    }

    std::string concurrent_a{initial_source};
    std::string concurrent_b{initial_source};
    concurrent_a.replace(concurrent_a.find("PARAMETER width 10 mm"),
                         std::string_view{"PARAMETER width 10 mm"}.size(),
                         "PARAMETER width 31 mm");
    concurrent_b.replace(concurrent_b.find("PARAMETER width 10 mm"),
                         std::string_view{"PARAMETER width 10 mm"}.size(),
                         "PARAMETER width 32 mm");
    std::barrier start{3};
    icad::document::SourceChange concurrent_result_a;
    icad::document::SourceChange concurrent_result_b;
    std::thread writer_a{[&] {
        start.arrive_and_wait();
        concurrent_result_a = icad::document::write_source(
            workspace, "designs/part.icad", concurrent_a, created.revision);
    }};
    std::thread writer_b{[&] {
        start.arrive_and_wait();
        concurrent_result_b = icad::document::write_source(
            workspace, "designs/part.icad", concurrent_b, created.revision);
    }};
    start.arrive_and_wait();
    writer_a.join();
    writer_b.join();
    const auto success_count = static_cast<int>(concurrent_result_a.ok()) +
                               static_cast<int>(concurrent_result_b.ok());
    const auto conflict_count =
        static_cast<int>(concurrent_result_a.status ==
                         icad::document::SourceStatus::conflict) +
        static_cast<int>(concurrent_result_b.status ==
                         icad::document::SourceStatus::conflict);
    if (success_count != 1 || conflict_count != 1) {
        return fail("concurrent optimistic commits did not select exactly one writer");
    }
    const auto concurrent_snapshot =
        icad::document::read_source(workspace, "designs/part.icad");
    const auto injection = icad::document::set_parameter(
        workspace, "designs/part.icad", "width\nPARAMETER injected", 99.0, "mm",
        concurrent_snapshot.revision);
    if (injection.status != icad::document::SourceStatus::invalid_source ||
        icad::document::read_source(workspace, "designs/part.icad").revision !=
            concurrent_snapshot.revision) {
        return fail("parameter token injection was not rejected");
    }

    const auto batch_created = icad::document::write_source(
        workspace, "designs/batch.icad", std::string{initial_source},
        icad::document::absent_revision);
    const std::vector<icad::document::ParameterEdit> batch_edits{
        {"width", 42.0, "mm"}, {"depth", 27.0, "mm"}};
    const auto batch = icad::document::set_parameters(
        workspace, "designs/batch.icad", batch_edits, batch_created.revision);
    const auto batch_snapshot = icad::document::read_source(workspace, "designs/batch.icad");
    if (!batch.ok() || !batch_snapshot.source.contains("PARAMETER width 42 mm") ||
        !batch_snapshot.source.contains("PARAMETER depth 27 mm")) {
        return fail("atomic batch parameter transaction failed");
    }
    const auto failed_batch = icad::document::set_parameters(
        workspace, "designs/batch.icad", {{"missing", 1.0, "mm"}}, batch.revision);
    if (failed_batch.status != icad::document::SourceStatus::invalid_source ||
        icad::document::read_source(workspace, "designs/batch.icad").revision != batch.revision) {
        return fail("invalid batch parameter transaction was not atomic");
    }

    const auto traversal = icad::document::write_source(
        workspace, "../escape.icad", std::string{initial_source},
        icad::document::absent_revision);
    if (traversal.status != icad::document::SourceStatus::path_error ||
        std::filesystem::exists(workspace.parent_path() / "escape.icad")) {
        return fail("project write escaped through traversal");
    }
    std::error_code symlink_error;
    std::filesystem::create_directory_symlink(outside, workspace / "linked", symlink_error);
    if (!symlink_error) {
        const auto symlink_escape = icad::document::write_source(
            workspace, "linked/escape.icad", std::string{initial_source},
            icad::document::absent_revision);
        if (symlink_escape.status != icad::document::SourceStatus::path_error ||
            std::filesystem::exists(outside / "escape.icad")) {
            return fail("project write escaped through a symlink");
        }
    }
    for (const auto& entry :
         std::filesystem::directory_iterator(workspace / "designs")) {
        if (entry.path().filename().string().contains(".icad-tmp-")) {
            return fail("atomic project update left a temporary file");
        }
        if (entry.path().filename().string().contains(".icad-lock")) {
            return fail("atomic project update left a commit lock");
        }
    }
    return 0;
}
