#include "main_window.hpp"

#include "icad_highlighter.hpp"
#include "scene_model.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFuture>
#include <QHeaderView>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <chrono>
#include <string>

namespace icad::desktop {
namespace {

[[nodiscard]] auto to_qstring(const std::filesystem::path& path) -> QString {
    return QString::fromStdString(path.string());
}

[[nodiscard]] auto severity_color(compiler::DiagnosticSeverity severity) -> QColor {
    switch (severity) {
    case compiler::DiagnosticSeverity::error:
        return QColor{"#fb7185"};
    case compiler::DiagnosticSeverity::warning:
        return QColor{"#fbbf24"};
    case compiler::DiagnosticSeverity::note:
        return QColor{"#60a5fa"};
    }
    return QColor{"#cbd5e1"};
}

} // namespace

MainWindow::MainWindow(std::filesystem::path source_path, QWidget* parent)
    : QMainWindow{parent}, source_path_{std::filesystem::absolute(std::move(source_path))} {
    session_ = std::make_unique<engine::Session>(source_path_);
    if (!session_->ready()) {
        error_ = QString::fromStdString(session_->error());
        session_.reset();
        return;
    }
    build_ui();
    build_actions();
    apply_theme();
    setWindowTitle(QStringLiteral("ICAD Studio — %1").arg(QFileInfo{to_qstring(source_path_)}.fileName()));
    setWindowIcon(QIcon{QStringLiteral(":/icad/icons/icad-256.png")});
    resize(1540, 960);

    compile_timer_.setSingleShot(true);
    compile_timer_.setInterval(110);
    connect(&compile_timer_, &QTimer::timeout, this, [this] { begin_compile(); });
    connect(&compile_watcher_, &QFutureWatcher<engine::PreviewResult>::finished, this,
            [this] { finish_compile(); });
    history_timer_.setSingleShot(true);
    history_timer_.setInterval(420);
    connect(&history_timer_, &QTimer::timeout, this, [this] { capture_history(); });
    connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
        if (restoring_history_)
            return;
        set_modified(editor_->toPlainText() != saved_source_);
        history_timer_.start();
        schedule_compile();
    });
    connect(diagnostics_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const int line = item->data(Qt::UserRole).toInt();
        if (line <= 0)
            return;
        auto cursor = editor_->textCursor();
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, line - 1);
        editor_->setTextCursor(cursor);
        editor_->setFocus();
    });
    viewport_->selection_changed = [this](std::optional<std::size_t> index) {
        update_selection(index);
    };
    connect(model_tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        const auto selected = model_tree_->selectedItems();
        if (selected.empty()) {
            viewport_->select_part(std::nullopt);
            return;
        }
        bool ok = false;
        const auto raw = selected.front()->data(0, Qt::UserRole).toULongLong(&ok);
        if (ok)
            viewport_->select_part(static_cast<std::size_t>(raw));
    });
    connect(scenes_, &QListWidget::itemActivated, this,
            [this] { set_scene_playing(!scene_playing_); });

    scene_timer_.setInterval(16);
    connect(&scene_timer_, &QTimer::timeout, this, [this] {
        const int next = timeline_->value() + 1;
        if (next > timeline_->maximum()) {
            timeline_->setValue(0);
        } else {
            timeline_->setValue(next);
        }
    });

    saved_source_ = QString::fromStdString(session_->source());
    editor_->setPlainText(saved_source_);
    reset_history(saved_source_);
    update_recent_files(source_path_);
    set_modified(false);
    begin_compile();
}

MainWindow::~MainWindow() {
    compile_timer_.stop();
    if (compile_watcher_.isRunning())
        compile_watcher_.waitForFinished();
}

auto MainWindow::build_ui() -> void {
    auto* splitter = new QSplitter{Qt::Horizontal, this};
    editor_ = new QPlainTextEdit{splitter};
    editor_->setObjectName(QStringLiteral("sourceEditor"));
    editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor_->setTabStopDistance(32.0);
    editor_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new IcadHighlighter{editor_->document()};
    viewport_ = new CadViewport{splitter};
    splitter->addWidget(editor_);
    splitter->addWidget(viewport_);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 5);
    splitter->setSizes({520, 1020});
    setCentralWidget(splitter);

    diagnostics_ = new QListWidget{this};
    diagnostics_->setAlternatingRowColors(true);
    diagnostics_dock_ = new QDockWidget{QStringLiteral("Diagnostics"), this};
    diagnostics_dock_->setObjectName(QStringLiteral("diagnosticsDock"));
    diagnostics_dock_->setWidget(diagnostics_);
    addDockWidget(Qt::BottomDockWidgetArea, diagnostics_dock_);

    model_tree_ = new QTreeWidget{this};
    model_tree_->setHeaderLabels({QStringLiteral("Component"), QStringLiteral("Material")});
    model_tree_->header()->setStretchLastSection(false);
    model_tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    model_tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    auto* model_dock = new QDockWidget{QStringLiteral("Model"), this};
    model_dock->setObjectName(QStringLiteral("modelDock"));
    model_dock->setWidget(model_tree_);
    addDockWidget(Qt::RightDockWidgetArea, model_dock);

    auto* scene_panel = new QWidget{this};
    auto* scene_layout = new QVBoxLayout{scene_panel};
    scenes_ = new QListWidget{scene_panel};
    timeline_ = new QSlider{Qt::Horizontal, scene_panel};
    timeline_->setRange(0, 1000);
    scene_layout->addWidget(scenes_);
    scene_layout->addWidget(new QLabel{QStringLiteral("Scene timeline"), scene_panel});
    scene_layout->addWidget(timeline_);
    auto* scene_dock = new QDockWidget{QStringLiteral("Scenes"), this};
    scene_dock->setObjectName(QStringLiteral("sceneDock"));
    scene_dock->setWidget(scene_panel);
    addDockWidget(Qt::RightDockWidgetArea, scene_dock);

    properties_ = new QLabel{QStringLiteral("No component selected"), this};
    properties_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    properties_->setWordWrap(true);
    properties_->setMargin(10);
    auto* properties_dock = new QDockWidget{QStringLiteral("Properties"), this};
    properties_dock->setObjectName(QStringLiteral("propertiesDock"));
    properties_dock->setWidget(properties_);
    addDockWidget(Qt::RightDockWidgetArea, properties_dock);
    tabifyDockWidget(scene_dock, properties_dock);
    scene_dock->raise();

    compile_state_ = new QLabel{QStringLiteral("Ready"), this};
    metrics_ = new QLabel{this};
    statusBar()->addWidget(compile_state_);
    statusBar()->addPermanentWidget(metrics_);
}

auto MainWindow::build_actions() -> void {
    auto* file_menu = menuBar()->addMenu(QStringLiteral("&File"));
    auto* open = file_menu->addAction(QStringLiteral("&Open ICAD File…"));
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, [this] { open_source_dialog(); });
    recent_menu_ = file_menu->addMenu(QStringLiteral("Open &Recent"));
    rebuild_recent_menu();
    file_menu->addSeparator();
    auto* save = file_menu->addAction(QStringLiteral("&Save"));
    save->setShortcut(QKeySequence::Save);
    connect(save, &QAction::triggered, this, [this] { save_source(); });
    file_menu->addSeparator();
    auto* export_action = file_menu->addAction(QStringLiteral("Export manufacturing package…"));
    export_action->setShortcut(QKeySequence{QStringLiteral("Ctrl+Shift+E")});
    connect(export_action, &QAction::triggered, this, [this] { export_package(); });
    auto* screenshot = file_menu->addAction(QStringLiteral("Export viewport image…"));
    connect(screenshot, &QAction::triggered, this, [this] { export_screenshot(); });
    file_menu->addSeparator();
    auto* close = file_menu->addAction(QStringLiteral("Close Project"), QKeySequence::Close,
                                       this, &QWidget::close);
    close->setMenuRole(QAction::NoRole);
    auto* quit = file_menu->addAction(QStringLiteral("Quit ICAD Studio"), QKeySequence::Quit,
                                      this, &QWidget::close);
    quit->setMenuRole(QAction::QuitRole);

    auto* edit_menu = menuBar()->addMenu(QStringLiteral("&Edit"));
    history_back_action_ = edit_menu->addAction(QStringLiteral("Back one operation"));
    history_back_action_->setShortcuts({QKeySequence::Undo,
                                        QKeySequence{QStringLiteral("Alt+Left")}});
    connect(history_back_action_, &QAction::triggered, this, [this] { step_history(-1); });
    history_next_action_ = edit_menu->addAction(QStringLiteral("Next operation"));
    history_next_action_->setShortcuts({QKeySequence::Redo,
                                        QKeySequence{QStringLiteral("Alt+Right")}});
    connect(history_next_action_, &QAction::triggered, this, [this] { step_history(1); });
    edit_menu->addSeparator();
    edit_menu->addAction(QStringLiteral("Cut"), QKeySequence::Cut, editor_, &QPlainTextEdit::cut);
    edit_menu->addAction(QStringLiteral("Copy"), QKeySequence::Copy, editor_, &QPlainTextEdit::copy);
    edit_menu->addAction(QStringLiteral("Paste"), QKeySequence::Paste, editor_, &QPlainTextEdit::paste);
    edit_menu->addAction(QStringLiteral("Select All"), QKeySequence::SelectAll,
                         editor_, &QPlainTextEdit::selectAll);
    update_history_actions();

    auto* build_menu = menuBar()->addMenu(QStringLiteral("&Build"));
    auto* compile = build_menu->addAction(QStringLiteral("Compile now"));
    compile->setShortcut(QKeySequence{QStringLiteral("Ctrl+B")});
    connect(compile, &QAction::triggered, this, [this] { begin_compile(); });

    auto* view_menu = menuBar()->addMenu(QStringLiteral("&View"));
    auto* fit = view_menu->addAction(QStringLiteral("Fit all"));
    fit->setShortcut(QKeySequence{QStringLiteral("F")});
    connect(fit, &QAction::triggered, viewport_, &CadViewport::fit_all);
    auto* projection = view_menu->addAction(QStringLiteral("Orthographic projection"));
    projection->setCheckable(true);
    connect(projection, &QAction::toggled, viewport_, &CadViewport::set_orthographic);
    auto* wireframe = view_menu->addAction(QStringLiteral("Wireframe"));
    wireframe->setCheckable(true);
    connect(wireframe, &QAction::toggled, viewport_, &CadViewport::set_wireframe);
    auto* debug = view_menu->addAction(QStringLiteral("Debug overlay"));
    debug->setShortcut(QKeySequence{QStringLiteral("Ctrl+Shift+D")});
    debug->setCheckable(true);
    connect(debug, &QAction::toggled, viewport_, &CadViewport::set_debug_overlay);
    view_menu->addSeparator();
    view_menu->addAction(diagnostics_dock_->toggleViewAction());
    auto* standard_views = view_menu->addMenu(QStringLiteral("Standard View"));
    const auto add_view = [this, standard_views](QString label, StandardView view) {
        auto* action = standard_views->addAction(std::move(label));
        connect(action, &QAction::triggered, this, [this, view] { viewport_->set_standard_view(view); });
    };
    add_view(QStringLiteral("ISO"), StandardView::isometric);
    add_view(QStringLiteral("Front"), StandardView::front);
    add_view(QStringLiteral("Right"), StandardView::right);
    add_view(QStringLiteral("Top"), StandardView::top);

    auto* scene_menu = menuBar()->addMenu(QStringLiteral("&Scene"));
    scene_play_action_ = scene_menu->addAction(QStringLiteral("Play Scene"));
    scene_play_action_->setShortcut(QKeySequence{Qt::Key_Space});
    scene_play_action_->setCheckable(true);
    connect(scene_play_action_, &QAction::toggled, this,
            [this](bool enabled) { set_scene_playing(enabled); });
}

auto MainWindow::apply_theme() -> void {
    setStyleSheet(QStringLiteral(R"css(
        QMainWindow, QMenuBar, QMenu, QStatusBar, QDockWidget { background:#0b1220; color:#dbeafe; }
        QMenuBar { border:0; padding:2px 6px; spacing:3px; }
        QMenuBar::item { border:0; border-radius:7px; padding:6px 10px; background:transparent; }
        QMenuBar::item:selected, QMenuBar::item:pressed { background:#1b2a42; }
        QMenu { background:#101827; border:1px solid #2a3952; border-radius:10px; padding:7px; }
        QMenu::item { border:0; border-radius:6px; padding:7px 28px 7px 12px; }
        QMenu::item:selected { background:#233653; color:#f8fafc; }
        QMenu::separator { height:1px; background:#293750; margin:6px 8px; }
        QToolButton, QPushButton { background:#162237; color:#e2e8f0; border:0; outline:0; border-radius:7px; padding:6px 9px; }
        QToolButton:hover, QPushButton:hover { background:#21334f; }
        QToolButton:pressed, QToolButton:checked, QPushButton:pressed { background:#164e63; }
        QDockWidget::title { background:#111c2e; padding:7px; font-weight:600; }
        QPlainTextEdit#sourceEditor { background:#08101f; color:#dbeafe; border:0; selection-background-color:#164e63; padding:8px; }
        QListWidget, QTreeWidget, QLabel { background:#0d1728; color:#cbd5e1; border:0; }
        QListWidget::item:selected, QTreeWidget::item:selected { background:#164e63; color:#f8fafc; }
        QHeaderView::section { background:#152238; color:#cbd5e1; border:0; padding:5px; }
        QStatusBar { border-top:1px solid #26354d; }
        QSlider::groove:horizontal { background:#26354d; height:5px; border-radius:2px; }
        QSlider::handle:horizontal { background:#38bdf8; width:13px; margin:-5px 0; border-radius:6px; }
        QScrollBar:vertical { background:transparent; width:8px; margin:2px; }
        QScrollBar::handle:vertical { background:#41516a; min-height:28px; border-radius:4px; }
        QScrollBar::handle:vertical:hover { background:#60728e; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { height:0; background:transparent; }
        QScrollBar:horizontal { background:transparent; height:8px; margin:2px; }
        QScrollBar::handle:horizontal { background:#41516a; min-width:28px; border-radius:4px; }
        QScrollBar::handle:horizontal:hover { background:#60728e; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { width:0; background:transparent; }
        QSplitter::handle { background:#162237; width:1px; height:1px; }
    )css"));
}

auto MainWindow::open_source_dialog() -> void {
    const QString selected = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open ICAD source"), to_qstring(source_path_.parent_path()),
        QStringLiteral("ICAD source (*.icad)"));
    if (!selected.isEmpty())
        open_source(std::filesystem::path{selected.toStdString()});
}

auto MainWindow::open_source(const std::filesystem::path& path) -> bool {
    if (!confirm_abandon_changes())
        return false;
    const auto absolute = std::filesystem::absolute(path);
    auto next_session = std::make_unique<engine::Session>(absolute);
    if (!next_session->ready()) {
        QMessageBox::critical(this, QStringLiteral("Open failed"),
                              QString::fromStdString(next_session->error()));
        return false;
    }
    compile_timer_.stop();
    history_timer_.stop();
    if (compile_watcher_.isRunning())
        compile_watcher_.waitForFinished();
    session_ = std::move(next_session);
    source_path_ = absolute;
    saved_source_ = QString::fromStdString(session_->source());
    restoring_history_ = true;
    editor_->setPlainText(saved_source_);
    restoring_history_ = false;
    viewport_->clear_scene();
    diagnostics_->clear();
    model_tree_->clear();
    scenes_->clear();
    reset_history(saved_source_);
    update_recent_files(source_path_);
    set_modified(false);
    begin_compile();
    editor_->setFocus();
    return true;
}

auto MainWindow::update_recent_files(const std::filesystem::path& path) -> void {
    QSettings settings;
    QStringList recent = settings.value(QStringLiteral("recentFiles")).toStringList();
    const QString normalized = QFileInfo{to_qstring(path)}.absoluteFilePath();
    recent.removeAll(normalized);
    recent.prepend(normalized);
    while (recent.size() > 10)
        recent.removeLast();
    settings.setValue(QStringLiteral("recentFiles"), recent);
    rebuild_recent_menu();
}

auto MainWindow::rebuild_recent_menu() -> void {
    if (recent_menu_ == nullptr)
        return;
    recent_menu_->clear();
    QSettings settings;
    const QStringList recent = settings.value(QStringLiteral("recentFiles")).toStringList();
    int visible = 0;
    for (const QString& path : recent) {
        if (!QFileInfo::exists(path))
            continue;
        auto* action = recent_menu_->addAction(
            QStringLiteral("%1  —  %2").arg(QFileInfo{path}.fileName(), path));
        connect(action, &QAction::triggered, this,
                [this, path] { open_source(std::filesystem::path{path.toStdString()}); });
        ++visible;
    }
    if (visible == 0) {
        auto* empty = recent_menu_->addAction(QStringLiteral("No recent files"));
        empty->setEnabled(false);
    } else {
        recent_menu_->addSeparator();
        auto* clear = recent_menu_->addAction(QStringLiteral("Clear Recent Files"));
        connect(clear, &QAction::triggered, this, [this] {
            QSettings{}.remove(QStringLiteral("recentFiles"));
            rebuild_recent_menu();
        });
    }
}

auto MainWindow::confirm_abandon_changes() -> bool {
    if (!modified_)
        return true;
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Unsaved ICAD source"), QStringLiteral("Save changes before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    return answer != QMessageBox::Save || save_source();
}

auto MainWindow::reset_history(const QString& source) -> void {
    edit_history_.clear();
    edit_history_.push_back(source);
    history_index_ = 0;
    update_history_actions();
}

auto MainWindow::capture_history() -> void {
    if (restoring_history_)
        return;
    const QString source = editor_->toPlainText();
    if (history_index_ >= 0 && edit_history_[static_cast<std::size_t>(history_index_)] == source)
        return;
    if (history_index_ + 1 < static_cast<int>(edit_history_.size()))
        edit_history_.erase(edit_history_.begin() + history_index_ + 1, edit_history_.end());
    edit_history_.push_back(source);
    // Keep one baseline plus 100 navigable edit operations.
    if (edit_history_.size() > 101U)
        edit_history_.erase(edit_history_.begin());
    history_index_ = static_cast<int>(edit_history_.size()) - 1;
    update_history_actions();
}

auto MainWindow::step_history(int direction) -> void {
    history_timer_.stop();
    capture_history();
    const int next = history_index_ + direction;
    if (next < 0 || next >= static_cast<int>(edit_history_.size()))
        return;
    history_index_ = next;
    restoring_history_ = true;
    editor_->setPlainText(edit_history_[static_cast<std::size_t>(history_index_)]);
    restoring_history_ = false;
    set_modified(editor_->toPlainText() != saved_source_);
    schedule_compile();
    update_history_actions();
    statusBar()->showMessage(QStringLiteral("Operation history %1 / %2")
                                 .arg(history_index_ + 1)
                                 .arg(static_cast<int>(edit_history_.size())),
                             1800);
}

auto MainWindow::update_history_actions() -> void {
    if (history_back_action_ != nullptr)
        history_back_action_->setEnabled(history_index_ > 0);
    if (history_next_action_ != nullptr)
        history_next_action_->setEnabled(
            history_index_ >= 0 && history_index_ + 1 < static_cast<int>(edit_history_.size()));
}

auto MainWindow::set_scene_playing(bool enabled) -> void {
    scene_playing_ = enabled;
    if (scene_play_action_ != nullptr) {
        scene_play_action_->blockSignals(true);
        scene_play_action_->setChecked(enabled);
        scene_play_action_->setText(enabled ? QStringLiteral("Pause Scene")
                                            : QStringLiteral("Play Scene"));
        scene_play_action_->blockSignals(false);
    }
    if (enabled)
        scene_timer_.start();
    else
        scene_timer_.stop();
}

auto MainWindow::schedule_compile() -> void {
    pending_source_ = editor_->toPlainText();
    compile_pending_ = true;
    compile_timer_.start();
}

auto MainWindow::begin_compile() -> void {
    if (session_ == nullptr)
        return;
    if (compile_watcher_.isRunning()) {
        pending_source_ = editor_->toPlainText();
        compile_pending_ = true;
        return;
    }
    compile_timer_.stop();
    const QString source = editor_->toPlainText();
    compile_pending_ = false;
    compile_state_->setText(QStringLiteral("Compiling…"));
    compile_state_->setStyleSheet(QStringLiteral("color:#fbbf24"));
    engine::Session* const session = session_.get();
    compile_watcher_.setFuture(QtConcurrent::run([session, source] {
        return session->preview(source.toStdString());
    }));
}

auto MainWindow::finish_compile() -> void {
    const auto result = compile_watcher_.result();
    update_diagnostics(result);
    if (result.success) {
        if (!result.model_json.empty()) {
            auto parsed = parse_render_scene(result.model_json);
            if (parsed.ok()) {
                viewport_->set_scene(std::move(parsed.scene));
                rebuild_model_tree();
                rebuild_scene_panel();
            } else {
                compile_state_->setText(parsed.error);
                compile_state_->setStyleSheet(QStringLiteral("color:#fb7185"));
            }
        }
        if (result.success) {
            compile_state_->setText(result.unchanged ? QStringLiteral("Preview reused")
                                                     : QStringLiteral("Preview ready"));
            compile_state_->setStyleSheet(QStringLiteral("color:#4ade80"));
            metrics_->setText(QStringLiteral("%1 bodies · %2 ms · %3 reused / %4 rebuilt · %5 workers")
                                  .arg(static_cast<qulonglong>(result.bodies))
                                  .arg(result.milliseconds, 0, 'f', 1)
                                  .arg(static_cast<qulonglong>(result.reused_bodies))
                                  .arg(static_cast<qulonglong>(result.recomputed_bodies))
                                  .arg(static_cast<qulonglong>(result.parallel_workers)));
        }
    } else {
        compile_state_->setText(QString::fromStdString(result.message));
        compile_state_->setStyleSheet(QStringLiteral("color:#fb7185"));
    }
    if (compile_pending_ || pending_source_ != editor_->toPlainText()) {
        pending_source_ = editor_->toPlainText();
        compile_pending_ = false;
        begin_compile();
    }
}

auto MainWindow::update_diagnostics(const engine::PreviewResult& result) -> void {
    diagnostics_->clear();
    for (const auto& diagnostic : result.diagnostics) {
        const QString description = QStringLiteral("%1 · %2:%3 · %4")
                                        .arg(QString::fromStdString(diagnostic.code))
                                        .arg(static_cast<qulonglong>(diagnostic.location.line))
                                        .arg(static_cast<qulonglong>(diagnostic.location.column))
                                        .arg(QString::fromStdString(diagnostic.message));
        auto* item = new QListWidgetItem{description, diagnostics_};
        item->setForeground(severity_color(diagnostic.severity));
        item->setData(Qt::UserRole, static_cast<qulonglong>(diagnostic.location.line));
    }
    if (result.diagnostics.empty())
        diagnostics_->addItem(QStringLiteral("No compiler diagnostics."));
}

auto MainWindow::rebuild_model_tree() -> void {
    model_tree_->clear();
    const auto& parts = viewport_->scene().parts;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const auto& part = parts[index];
        auto* item = new QTreeWidgetItem{model_tree_, {part.name, part.material}};
        item->setToolTip(0, part.body.isEmpty() ? part.name : part.body);
        item->setData(0, Qt::UserRole, static_cast<qulonglong>(index));
    }
}

auto MainWindow::rebuild_scene_panel() -> void {
    scenes_->clear();
    for (const auto& scene : viewport_->scene().scenes) {
        scenes_->addItem(QStringLiteral("▶ %1  ·  %2 s @ %3 fps")
                             .arg(scene.name)
                             .arg(scene.duration_seconds, 0, 'f', 2)
                             .arg(scene.frames_per_second, 0, 'f', 0));
    }
    if (viewport_->scene().scenes.empty())
        scenes_->addItem(QStringLiteral("No programmable scenes"));
}

auto MainWindow::update_selection(std::optional<std::size_t> index) -> void {
    model_tree_->blockSignals(true);
    model_tree_->clearSelection();
    if (!index || *index >= viewport_->scene().parts.size()) {
        properties_->setText(QStringLiteral("No component selected"));
        model_tree_->blockSignals(false);
        return;
    }
    const auto& part = viewport_->scene().parts[*index];
    if (auto* item = model_tree_->topLevelItem(static_cast<int>(*index)); item != nullptr)
        item->setSelected(true);
    properties_->setText(QStringLiteral("<b>%1</b><br>Body: %2<br>Material: %3<br>Triangles: %4<br>Bounds: [%5, %6, %7] — [%8, %9, %10] mm")
                             .arg(part.name, part.body, part.material)
                             .arg(static_cast<qulonglong>(part.index_count / 3U))
                             .arg(part.minimum.x(), 0, 'f', 2)
                             .arg(part.minimum.y(), 0, 'f', 2)
                             .arg(part.minimum.z(), 0, 'f', 2)
                             .arg(part.maximum.x(), 0, 'f', 2)
                             .arg(part.maximum.y(), 0, 'f', 2)
                             .arg(part.maximum.z(), 0, 'f', 2));
    model_tree_->blockSignals(false);
}

auto MainWindow::save_source() -> bool {
    if (session_ == nullptr)
        return false;
    const auto result = session_->save(editor_->toPlainText().toStdString());
    if (!result.success) {
        QMessageBox::critical(this, QStringLiteral("Save failed"), QString::fromStdString(result.message));
        return false;
    }
    saved_source_ = editor_->toPlainText();
    capture_history();
    set_modified(false);
    statusBar()->showMessage(QStringLiteral("Source saved"), 2500);
    return true;
}

auto MainWindow::export_package() -> void {
    if (session_ == nullptr)
        return;
    const QString suggested = to_qstring(session_->default_export_directory());
    const QString selected = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Export ICAD manufacturing package"), suggested,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (selected.isEmpty())
        return;
    compile_state_->setText(QStringLiteral("Exporting package…"));
    const auto result = session_->export_package(editor_->toPlainText().toStdString(),
                                                  std::filesystem::path{selected.toStdString()});
    if (!result.success) {
        QMessageBox::critical(this, QStringLiteral("Export failed"), QString::fromStdString(result.message));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Exported %1 artifacts to %2")
                                 .arg(static_cast<qulonglong>(result.artifacts))
                                 .arg(selected),
                             7000);
    compile_state_->setText(QStringLiteral("Preview ready"));
}

auto MainWindow::export_screenshot() -> void {
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export viewport image"),
        QFileInfo{to_qstring(source_path_)}.completeBaseName() + QStringLiteral(".png"),
        QStringLiteral("PNG image (*.png)"));
    if (!path.isEmpty() && !viewport_->save_screenshot(path))
        QMessageBox::critical(this, QStringLiteral("Screenshot failed"),
                              QStringLiteral("Could not write the selected PNG file."));
}

auto MainWindow::set_modified(bool modified) -> void {
    modified_ = modified;
    setWindowModified(modified);
    const QString suffix = modified ? QStringLiteral("[*]") : QString{};
    setWindowTitle(QStringLiteral("ICAD Studio — %1%2")
                       .arg(QFileInfo{to_qstring(source_path_)}.fileName(), suffix));
}

auto MainWindow::closeEvent(QCloseEvent* event) -> void {
    if (confirm_abandon_changes()) {
        event->accept();
    } else {
        event->ignore();
    }
}

} // namespace icad::desktop
