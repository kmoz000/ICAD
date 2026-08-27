#include "icad/engine/session.hpp"

#include "icad/viewer/live_session.hpp"

#include <mutex>
#include <utility>

namespace icad::engine {

class Session::Impl {
  public:
    explicit Impl(std::filesystem::path path) : live{std::move(path)} {}

    mutable std::mutex mutex;
    viewer::LiveSession live;
};

namespace {

[[nodiscard]] auto copy(viewer::PreviewResult source) -> PreviewResult {
    return {source.success,
            std::move(source.message),
            std::move(source.model_json),
            source.revision,
            source.bodies,
            source.materials,
            source.scenes,
            source.keyframes,
            source.reused_bodies,
            source.recomputed_bodies,
            source.parallel_workers,
            source.milliseconds,
            source.unchanged,
            std::move(source.diagnostics)};
}

[[nodiscard]] auto copy(viewer::PackageResult source) -> PackageResult {
    return {source.success,
            std::move(source.message),
            std::move(source.directory),
            source.artifacts,
            source.components,
            source.solids,
            source.milliseconds,
            std::move(source.diagnostics)};
}

} // namespace

Session::Session(std::filesystem::path source_path)
    : impl_{new Impl{std::move(source_path)}} {}
Session::Session(Session&& other) noexcept : impl_{std::exchange(other.impl_, nullptr)} {}
auto Session::operator=(Session&& other) noexcept -> Session& {
    if (this != &other) {
        delete impl_;
        impl_ = std::exchange(other.impl_, nullptr);
    }
    return *this;
}
Session::~Session() { delete impl_; }

auto Session::ready() const -> bool {
    const std::lock_guard lock{impl_->mutex};
    return impl_->live.ready();
}

auto Session::error() const -> std::string {
    const std::lock_guard lock{impl_->mutex};
    return impl_->live.error();
}

auto Session::source() const -> std::string {
    const std::lock_guard lock{impl_->mutex};
    return impl_->live.source();
}

auto Session::source_path() const -> std::filesystem::path {
    const std::lock_guard lock{impl_->mutex};
    return impl_->live.source_path();
}

auto Session::default_export_directory() const -> std::filesystem::path {
    const std::lock_guard lock{impl_->mutex};
    return impl_->live.default_export_directory();
}

auto Session::preview(std::string_view source) -> PreviewResult {
    const std::lock_guard lock{impl_->mutex};
    return copy(impl_->live.preview(source));
}

auto Session::save(std::string_view source) -> SaveResult {
    const std::lock_guard lock{impl_->mutex};
    auto result = impl_->live.save(source);
    return {result.success, std::move(result.message)};
}

auto Session::export_package(std::string_view source,
                             const std::filesystem::path& directory) -> PackageResult {
    const std::lock_guard lock{impl_->mutex};
    return copy(impl_->live.export_package(source, directory));
}

} // namespace icad::engine
