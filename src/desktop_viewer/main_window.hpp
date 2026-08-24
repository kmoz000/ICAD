#pragma once

#include "cad_viewport.hpp"

#include "icad/engine/session.hpp"

#include <QFutureWatcher>
#include <QMainWindow>
#include <QTimer>

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

class QAction;
class QCheckBox;
class QDockWidget;
class QLabel;
class QListWidget;
class QMenu;
class QPlainTextEdit;
class QSlider;
class QTreeWidget;

namespace icad::desktop {

class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(std::filesystem::path source_path, QWidget* parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] auto ready() const noexcept -> bool { return session_ != nullptr; }
    [[nodiscard]] auto error() const -> QString { return error_; }

  protected:
    auto closeEvent(QCloseEvent* event) -> void override;

  private:
    auto build_ui() -> void;
    auto build_actions() -> void;
    auto apply_theme() -> void;
    auto open_source_dialog() -> void;
    auto open_source(const std::filesystem::path& path) -> bool;
    auto update_recent_files(const std::filesystem::path& path) -> void;
    auto rebuild_recent_menu() -> void;
    [[nodiscard]] auto confirm_abandon_changes() -> bool;
    auto schedule_compile() -> void;
    auto begin_compile() -> void;
    auto finish_compile() -> void;
    auto update_diagnostics(const engine::PreviewResult& result) -> void;
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

    std::unique_ptr<engine::Session> session_;
    QString error_;
    std::filesystem::path source_path_;
    QPlainTextEdit* editor_{};
    CadViewport* viewport_{};
    QListWidget* diagnostics_{};
    QTreeWidget* model_tree_{};
    QListWidget* scenes_{};
    QSlider* timeline_{};
    QLabel* properties_{};
    QLabel* compile_state_{};
    QLabel* metrics_{};
    QDockWidget* diagnostics_dock_{};
    QMenu* recent_menu_{};
    QAction* history_back_action_{};
    QAction* history_next_action_{};
    QAction* scene_play_action_{};
    QTimer compile_timer_;
    QTimer scene_timer_;
    QTimer history_timer_;
    QFutureWatcher<engine::PreviewResult> compile_watcher_;
    QString pending_source_;
    QString saved_source_;
    std::vector<QString> edit_history_;
    int history_index_{-1};
    bool compile_pending_{false};
    bool modified_{false};
    bool scene_playing_{false};
    bool restoring_history_{false};
};

} // namespace icad::desktop
