#pragma once

#include "cad_viewport.hpp"
#include "icad_editor.hpp"

#include "icad/engine/session.hpp"

#include <QFutureWatcher>
#include <QMainWindow>
#include <QTimer>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <cstdint>
#include <vector>

class QAction;
class QCheckBox;
class QDockWidget;
class QFileSystemModel;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QSlider;
class QTabBar;
class QTreeView;
class QTreeWidget;

namespace icad::desktop {

class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(std::filesystem::path source_path, QWidget* parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] auto ready() const noexcept -> bool { return !documents_.empty(); }
    [[nodiscard]] auto error() const -> QString { return error_; }
    [[nodiscard]] auto document_count() const noexcept -> std::size_t { return documents_.size(); }
    [[nodiscard]] auto workspace_root() const noexcept -> const std::filesystem::path& {
        return workspace_root_;
    }
    auto open_document(const std::filesystem::path& path) -> bool;
    auto open_workspace(const std::filesystem::path& path) -> bool;
    auto set_standard_view(StandardView view) -> void;
    auto set_display_mode(DisplayMode mode) -> void;
    auto set_assembly_inspection(bool enabled) -> void;
    auto set_cutaway(bool enabled) -> void;
    auto request_snapshot(QString path, std::function<void(bool)> completion) -> void;

  protected:
    auto closeEvent(QCloseEvent* event) -> void override;

  private:
    struct DocumentPreview {
        QString source;
        engine::PreviewResult result;
        std::optional<RenderScene> scene;
        QString scene_error;
    };

    struct OpenDocument {
        std::uint64_t id{};
        std::filesystem::path path;
        std::shared_ptr<engine::Session> session;
        QString source;
        QString saved_source;
        std::vector<QString> edit_history;
        std::optional<DocumentPreview> preview;
        int history_index{-1};
        bool modified{false};
    };

    struct CompileTaskResult {
        std::uint64_t document_id{};
        QString source;
        engine::PreviewResult preview;
        std::optional<RenderScene> scene;
        QString scene_error;
    };

    auto build_ui() -> void;
    auto build_actions() -> void;
    auto apply_theme() -> void;
    auto open_source_dialog() -> void;
    auto open_folder_dialog() -> void;
    auto open_folder(const std::filesystem::path& path) -> bool;
    auto open_source(const std::filesystem::path& path) -> bool;
    auto switch_document(int tab_index) -> void;
    auto close_document(int tab_index) -> void;
    [[nodiscard]] auto document_index_for_id(std::uint64_t id) const -> int;
    [[nodiscard]] auto document_index_for_tab(int tab_index) const -> int;
    [[nodiscard]] auto tab_index_for_document_id(std::uint64_t id) const -> int;
    [[nodiscard]] auto confirm_close_document(int index) -> bool;
    [[nodiscard]] auto save_document(int index) -> bool;
    [[nodiscard]] auto active_document() -> OpenDocument*;
    [[nodiscard]] auto active_document() const -> const OpenDocument*;
    [[nodiscard]] auto active_source_path() const -> std::filesystem::path;
    auto update_document_chrome() -> void;
    auto update_recent_files(const std::filesystem::path& path) -> void;
    auto rebuild_recent_menu() -> void;
    [[nodiscard]] auto confirm_all_changes() -> bool;
    auto schedule_compile() -> void;
    auto begin_compile() -> void;
    auto finish_compile() -> void;
    auto clear_preview_panels() -> void;
    auto apply_preview(const DocumentPreview& preview) -> void;
    auto restore_document_preview(const OpenDocument& document) -> void;
    auto update_diagnostics(const engine::PreviewResult& result) -> void;
    auto update_evidence(const engine::PreviewResult& result) -> void;
    auto jump_to_diagnostic(QListWidgetItem* item) -> void;
    auto update_cursor_position() -> void;
    auto rebuild_model_tree() -> void;
    auto rebuild_scene_panel() -> void;
    auto update_selection(std::optional<std::size_t> index) -> void;
    auto save_source() -> bool;
    auto export_package() -> void;
    auto export_screenshot() -> void;
    auto set_modified(bool modified) -> void;
    auto reset_history(const QString& source) -> void;
    auto capture_history() -> void;
    auto step_history(int direction) -> void;
    auto update_history_actions() -> void;
    auto set_scene_playing(bool enabled) -> void;
    [[nodiscard]] static auto path_is_within(const std::filesystem::path& path,
                                             const std::filesystem::path& directory) -> bool;

    std::vector<OpenDocument> documents_;
    std::uint64_t next_document_id_{1};
    int active_document_index_{-1};
    QString error_;
    std::filesystem::path workspace_root_;
    IcadEditor* editor_{};
    CadViewport* viewport_{};
    QTabBar* document_tabs_{};
    QFileSystemModel* workspace_model_{};
    QTreeView* workspace_tree_{};
    QListWidget* diagnostics_{};
    QListWidget* evidence_{};
    QTreeWidget* model_tree_{};
    QListWidget* scenes_{};
    QSlider* timeline_{};
    QLabel* properties_{};
    QLabel* compile_state_{};
    QLabel* editor_mode_{};
    QLabel* cursor_position_{};
    QLabel* metrics_{};
    QDockWidget* diagnostics_dock_{};
    QDockWidget* evidence_dock_{};
    QDockWidget* workspace_dock_{};
    QMenu* recent_menu_{};
    QAction* history_back_action_{};
    QAction* history_next_action_{};
    QAction* scene_play_action_{};
    QTimer compile_timer_;
    QTimer scene_timer_;
    QTimer history_timer_;
    QFutureWatcher<CompileTaskResult> compile_watcher_;
    QString pending_source_;
    QString pending_snapshot_path_;
    std::function<void(bool)> snapshot_completion_;
    bool compile_pending_{false};
    bool scene_playing_{false};
    bool restoring_history_{false};
};

} // namespace icad::desktop
