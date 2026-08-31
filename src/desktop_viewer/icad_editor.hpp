#pragma once

#include <QPlainTextEdit>

#include <functional>

class QCompleter;
class QStringListModel;
class QTimer;

namespace icad::desktop {

class IcadEditor final : public QPlainTextEdit {
  public:
    explicit IcadEditor(QWidget* parent = nullptr);

    auto set_vim_mode(bool enabled) -> void;
    [[nodiscard]] auto vim_mode() const noexcept -> bool { return vim_mode_; }
    [[nodiscard]] auto vim_insert_mode() const noexcept -> bool { return vim_insert_mode_; }
    auto trigger_completion() -> void;
    auto refresh_completions() -> void;
    [[nodiscard]] auto has_completion(const QString& value) const -> bool;
    auto toggle_line_comment() -> void;
    auto duplicate_line() -> void;
    auto move_line(int direction) -> void;

    std::function<void(QString)> mode_changed;

  protected:
    auto keyPressEvent(QKeyEvent* event) -> void override;

  private:
    [[nodiscard]] auto completion_prefix() const -> QString;
    auto show_completion(bool forced) -> void;
    auto insert_completion(const QString& completion) -> void;
    auto update_mode_label() -> void;
    auto handle_vim_normal_key(QKeyEvent* event) -> bool;

    QCompleter* completer_{};
    QStringListModel* completion_model_{};
    QTimer* completion_refresh_timer_{};
    bool vim_mode_{false};
    bool vim_insert_mode_{true};
    bool vim_pending_delete_{false};
    bool vim_pending_goto_{false};
};

} // namespace icad::desktop
