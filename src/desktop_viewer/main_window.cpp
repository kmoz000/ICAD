#include "main_window.hpp"

#include "icad_highlighter.hpp"
#include "scene_model.hpp"
#include "icad/json/value.hpp"

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileSystemModel>
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
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTabBar>
#include <QTreeView>
#include <QTreeWidget>
#include <QVariant>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iterator>
#include <ranges>
#include <string>
#include <utility>

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

MainWindow::MainWindow(std::filesystem::path source_path, QWidget* parent) : QMainWindow{parent} {
    const auto absolute_path = std::filesystem::absolute(std::move(source_path)).lexically_normal();
    auto session = std::make_shared<engine::Session>(absolute_path);
    if (!session->ready()) {
        error_ = QString::fromStdString(session->error());
        return;
    }
    build_ui();
    editor_->mode_changed = [this](QString mode) { editor_mode_->setText(std::move(mode)); };
    build_actions();
    apply_theme();
    setWindowIcon(QIcon{QStringLiteral(":/icad/icons/icad-256.png")});
    setUnifiedTitleAndToolBarOnMac(true);
    resize(1540, 960);
    resizeDocks({diagnostics_dock_}, {145}, Qt::Vertical);

    compile_timer_.setSingleShot(true);
    compile_timer_.setInterval(300);
    connect(&compile_timer_, &QTimer::timeout, this, [this] { begin_compile(); });
    connect(&compile_watcher_, &QFutureWatcher<CompileTaskResult>::finished, this,
            [this] { finish_compile(); });
    history_timer_.setSingleShot(true);
    history_timer_.setInterval(420);
    connect(&history_timer_, &QTimer::timeout, this, [this] { capture_history(); });
    connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
        if (restoring_history_)
            return;
        auto* document = active_document();
        if (document == nullptr)
            return;
        document->source = editor_->toPlainText();
        set_modified(document->source != document->saved_source);
        history_timer_.start();
        schedule_compile();
    });
    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this,
            [this] { update_cursor_position(); });
    connect(document_tabs_, &QTabBar::currentChanged, this,
            [this](int index) { switch_document(index); });
    connect(document_tabs_, &QTabBar::tabMoved, this, [this](int, int) {
        // Resolve the settled tab through its stable document id. QTabBar may
        // report intermediate positional indices while a tab is moving.
        const int current_tab = document_tabs_->currentIndex();
        if (document_index_for_tab(current_tab) < 0)
            return;
        active_document_index_ = -1;
        switch_document(current_tab);
    });
    connect(document_tabs_, &QTabBar::tabCloseRequested, this,
            [this](int index) { close_document(index); });
    connect(workspace_tree_, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        const QFileInfo entry = workspace_model_->fileInfo(index);
        if (entry.isFile() && entry.suffix().compare(QStringLiteral("icad"), Qt::CaseInsensitive) == 0)
            open_source(std::filesystem::path{entry.absoluteFilePath().toStdString()});
    });
    connect(diagnostics_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) { jump_to_diagnostic(item); });
    connect(diagnostics_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) { jump_to_diagnostic(item); });
    viewport_->selection_changed = [this](std::optional<std::size_t> index) {
        update_selection(index);
    };
    connect(model_tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* item) {
        if (item == nullptr) {
            viewport_->select_part(std::nullopt);
            return;
        }
        bool ok = false;
        const auto raw = item->data(0, Qt::UserRole).toULongLong(&ok);
        if (ok)
            viewport_->select_part(static_cast<std::size_t>(raw));
    });
    connect(model_tree_, &QTreeWidget::itemDoubleClicked, this,
            [this] { viewport_->fit_selected(); });
    connect(scenes_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            viewport_->set_active_scene(static_cast<std::size_t>(row));
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

    OpenDocument initial;
    initial.id = next_document_id_++;
    initial.path = absolute_path;
    initial.session = std::move(session);
    initial.source = QString::fromStdString(initial.session->source());
    initial.saved_source = initial.source;
    initial.edit_history.push_back(initial.source);
    initial.history_index = 0;
    documents_.push_back(std::move(initial));
    {
        const QSignalBlocker blocker{document_tabs_};
        const int tab = document_tabs_->addTab(QFileInfo{to_qstring(absolute_path)}.fileName());
        document_tabs_->setTabData(tab, QVariant::fromValue<qulonglong>(documents_.front().id));
    }
    switch_document(0);
    open_folder(absolute_path.parent_path());
    update_recent_files(absolute_path);
}

auto MainWindow::set_standard_view(StandardView view) -> void {
    viewport_->set_standard_view(view);
    viewport_->fit_all();
}

auto MainWindow::set_display_mode(DisplayMode mode) -> void {
    viewport_->set_display_mode(mode);
}

auto MainWindow::set_assembly_inspection(bool enabled) -> void {
    viewport_->set_assembly_inspection(enabled);
}

auto MainWindow::set_cutaway(bool enabled) -> void {
    viewport_->set_cutaway(enabled);
}

auto MainWindow::open_document(const std::filesystem::path& path) -> bool {
    return open_source(path);
}

auto MainWindow::open_workspace(const std::filesystem::path& path) -> bool {
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    if (!open_folder(absolute))
        return false;

    std::error_code error;
    auto entry = absolute / "main.icad";
    if (!std::filesystem::is_regular_file(entry, error)) {
        std::vector<std::filesystem::path> sources;
        error.clear();
        for (std::filesystem::recursive_directory_iterator iterator{
                 absolute, std::filesystem::directory_options::skip_permission_denied, error},
             end;
             iterator != end; iterator.increment(error)) {
            if (error) {
                error.clear();
                continue;
            }
            if (iterator->is_regular_file(error) &&
                QFileInfo{to_qstring(iterator->path())}.suffix().compare(
                    QStringLiteral("icad"), Qt::CaseInsensitive) == 0) {
                sources.push_back(iterator->path());
            }
        }
        std::ranges::sort(sources);
        if (sources.empty()) {
            QMessageBox::critical(this, QStringLiteral("Open folder failed"),
                                  QStringLiteral("The selected folder contains no ICAD sources."));
            return false;
        }
        entry = sources.front();
    }
    return open_source(entry);
}

MainWindow::~MainWindow() {
    compile_timer_.stop();
    disconnect(&compile_watcher_, nullptr, this, nullptr);
    compile_watcher_.cancel();
}

auto MainWindow::build_ui() -> void {
    auto* central = new QWidget{this};
    auto* central_layout = new QVBoxLayout{central};
    central_layout->setContentsMargins(0, 0, 0, 0);
    central_layout->setSpacing(0);
    document_tabs_ = new QTabBar{central};
    document_tabs_->setObjectName(QStringLiteral("documentTabs"));
    document_tabs_->setDocumentMode(true);
    document_tabs_->setDrawBase(false);
    document_tabs_->setExpanding(false);
    document_tabs_->setMovable(true);
    document_tabs_->setTabsClosable(true);
    document_tabs_->setUsesScrollButtons(true);
    document_tabs_->setFixedHeight(40);
    document_tabs_->setSelectionBehaviorOnRemove(QTabBar::SelectPreviousTab);
    document_tabs_->setElideMode(Qt::ElideRight);
    central_layout->addWidget(document_tabs_);

    auto* splitter = new QSplitter{Qt::Horizontal, central};
    editor_ = new IcadEditor{splitter};
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
    central_layout->addWidget(splitter, 1);
    setCentralWidget(central);

    workspace_model_ = new QFileSystemModel{this};
    workspace_model_->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    workspace_model_->setNameFilters({QStringLiteral("*.icad")});
    workspace_model_->setNameFilterDisables(false);
    workspace_tree_ = new QTreeView{this};
    workspace_tree_->setObjectName(QStringLiteral("workspaceTree"));
    workspace_tree_->setModel(workspace_model_);
    workspace_tree_->setHeaderHidden(true);
    workspace_tree_->setAnimated(true);
    workspace_tree_->setExpandsOnDoubleClick(true);
    workspace_tree_->setIndentation(16);
    workspace_tree_->setUniformRowHeights(true);
    workspace_tree_->setTextElideMode(Qt::ElideMiddle);
    for (int column = 1; column < workspace_model_->columnCount(); ++column)
        workspace_tree_->hideColumn(column);
    workspace_dock_ = new QDockWidget{QStringLiteral("Workspace"), this};
    workspace_dock_->setObjectName(QStringLiteral("workspaceDock"));
    workspace_dock_->setWidget(workspace_tree_);
    workspace_dock_->setMinimumWidth(210);
    addDockWidget(Qt::LeftDockWidgetArea, workspace_dock_);

    diagnostics_ = new QListWidget{this};
    diagnostics_->setObjectName(QStringLiteral("diagnosticsList"));
    diagnostics_->setAlternatingRowColors(true);
    diagnostics_dock_ = new QDockWidget{QStringLiteral("Diagnostics"), this};
    diagnostics_dock_->setObjectName(QStringLiteral("diagnosticsDock"));
    diagnostics_dock_->setWidget(diagnostics_);
    addDockWidget(Qt::BottomDockWidgetArea, diagnostics_dock_);

    evidence_ = new QListWidget{this};
    evidence_->setObjectName(QStringLiteral("evidenceList"));
    evidence_->setAlternatingRowColors(true);
    evidence_dock_ = new QDockWidget{QStringLiteral("Engineering Evidence"), this};
    evidence_dock_->setObjectName(QStringLiteral("evidenceDock"));
    evidence_dock_->setWidget(evidence_);
    addDockWidget(Qt::BottomDockWidgetArea, evidence_dock_);
    tabifyDockWidget(diagnostics_dock_, evidence_dock_);
    diagnostics_dock_->raise();

    model_tree_ = new QTreeWidget{this};
    model_tree_->setObjectName(QStringLiteral("modelTree"));
    model_tree_->setHeaderLabels({QStringLiteral("Component"), QStringLiteral("Material")});
    model_tree_->setRootIsDecorated(false);
    model_tree_->setAlternatingRowColors(true);
    model_tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    model_tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    model_tree_->setUniformRowHeights(true);
    model_tree_->setAllColumnsShowFocus(true);
    model_tree_->header()->setMinimumSectionSize(90);
    model_tree_->header()->setStretchLastSection(false);
    model_tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    model_tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    auto* model_dock = new QDockWidget{QStringLiteral("Model"), this};
    model_dock->setObjectName(QStringLiteral("modelDock"));
    model_dock->setWidget(model_tree_);
    model_dock->setMinimumWidth(300);
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
    compile_state_->setObjectName(QStringLiteral("compileState"));
    editor_mode_ = new QLabel{QStringLiteral("ICAD"), this};
    editor_mode_->setObjectName(QStringLiteral("editorMode"));
    cursor_position_ = new QLabel{QStringLiteral("Ln 1, Col 1"), this};
    cursor_position_->setObjectName(QStringLiteral("cursorPosition"));
    cursor_position_->setMinimumWidth(112);
    cursor_position_->setToolTip(QStringLiteral("1-based editor cursor position"));
    metrics_ = new QLabel{this};
    statusBar()->addWidget(compile_state_);
    statusBar()->addPermanentWidget(editor_mode_);
    statusBar()->addPermanentWidget(cursor_position_);
    statusBar()->addPermanentWidget(metrics_);
    update_cursor_position();
}

auto MainWindow::build_actions() -> void {
    auto* file_menu = menuBar()->addMenu(QStringLiteral("&File"));
    auto* open = file_menu->addAction(QStringLiteral("&Open ICAD File…"));
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, [this] { open_source_dialog(); });
    auto* open_folder_action = file_menu->addAction(QStringLiteral("Open &Folder…"));
    open_folder_action->setShortcut(QKeySequence{QStringLiteral("Ctrl+Shift+O")});
    connect(open_folder_action, &QAction::triggered, this, [this] { open_folder_dialog(); });
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
    auto* close = file_menu->addAction(QStringLiteral("Close Tab"), QKeySequence::Close);
    connect(close, &QAction::triggered, this,
            [this] { close_document(document_tabs_->currentIndex()); });
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
    edit_menu->addSeparator();
    auto* complete = edit_menu->addAction(QStringLiteral("Trigger Suggestions"));
    complete->setShortcut(QKeySequence{QStringLiteral("Ctrl+Space")});
    connect(complete, &QAction::triggered, editor_, &IcadEditor::trigger_completion);
    auto* comment = edit_menu->addAction(QStringLiteral("Toggle Line Comment"));
    comment->setShortcut(QKeySequence{QStringLiteral("Ctrl+/")});
    connect(comment, &QAction::triggered, editor_, &IcadEditor::toggle_line_comment);
    auto* duplicate = edit_menu->addAction(QStringLiteral("Duplicate Line"));
    duplicate->setShortcut(QKeySequence{QStringLiteral("Shift+Alt+Down")});
    connect(duplicate, &QAction::triggered, editor_, &IcadEditor::duplicate_line);
    auto* move_up = edit_menu->addAction(QStringLiteral("Move Line Up"));
    move_up->setShortcut(QKeySequence{QStringLiteral("Alt+Up")});
    connect(move_up, &QAction::triggered, this, [this] { editor_->move_line(-1); });
    auto* move_down = edit_menu->addAction(QStringLiteral("Move Line Down"));
    move_down->setShortcut(QKeySequence{QStringLiteral("Alt+Down")});
    connect(move_down, &QAction::triggered, this, [this] { editor_->move_line(1); });
    auto* vim_mode = edit_menu->addAction(QStringLiteral("Vim Keybindings"));
    vim_mode->setShortcut(QKeySequence{QStringLiteral("Ctrl+Alt+V")});
    vim_mode->setCheckable(true);
    vim_mode->setChecked(QSettings{}.value(QStringLiteral("vimMode"), false).toBool());
    connect(vim_mode, &QAction::toggled, this, [this](bool enabled) {
        editor_->set_vim_mode(enabled);
        QSettings{}.setValue(QStringLiteral("vimMode"), enabled);
    });
    editor_->set_vim_mode(vim_mode->isChecked());
    update_history_actions();

    auto* build_menu = menuBar()->addMenu(QStringLiteral("&Build"));
    auto* compile = build_menu->addAction(QStringLiteral("Compile now"));
    compile->setShortcut(QKeySequence{QStringLiteral("Ctrl+B")});
    connect(compile, &QAction::triggered, this, [this] { begin_compile(); });

    auto* view_menu = menuBar()->addMenu(QStringLiteral("&View"));
    auto* display_modes = view_menu->addMenu(QStringLiteral("Viewport shading"));
    auto* display_group = new QActionGroup{display_modes};
    display_group->setExclusive(true);
    const auto add_display_mode = [this, display_modes, display_group](
                                      QString label, DisplayMode mode, bool selected = false) {
        auto* action = display_modes->addAction(std::move(label));
        action->setCheckable(true);
        action->setChecked(selected);
        display_group->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, mode] { viewport_->set_display_mode(mode); });
    };
    add_display_mode(QStringLiteral("Solid"), DisplayMode::solid, true);
    add_display_mode(QStringLiteral("Solid with mesh edges"), DisplayMode::solid_with_mesh);
    add_display_mode(QStringLiteral("CAD wireframe"), DisplayMode::cad_wireframe);
    add_display_mode(QStringLiteral("Triangle mesh"), DisplayMode::mesh_wireframe);
    auto* debug = view_menu->addAction(QStringLiteral("Debug overlay"));
    debug->setShortcut(QKeySequence{QStringLiteral("Ctrl+Shift+D")});
    debug->setCheckable(true);
    connect(debug, &QAction::toggled, viewport_, &CadViewport::set_debug_overlay);
    auto* assembly_inspection =
        view_menu->addAction(QStringLiteral("Assembly inspection / X-ray"));
    assembly_inspection->setShortcut(QKeySequence{QStringLiteral("Ctrl+Shift+A")});
    assembly_inspection->setCheckable(true);
    connect(assembly_inspection, &QAction::toggled, viewport_,
            &CadViewport::set_assembly_inspection);
    auto* cutaway = view_menu->addAction(QStringLiteral("Clean cutaway"));
    cutaway->setShortcut(QKeySequence{QStringLiteral("Ctrl+Shift+X")});
    cutaway->setCheckable(true);
    connect(cutaway, &QAction::toggled, viewport_, &CadViewport::set_cutaway);
    view_menu->addSeparator();
    view_menu->addAction(workspace_dock_->toggleViewAction());
    view_menu->addAction(diagnostics_dock_->toggleViewAction());
    view_menu->addAction(evidence_dock_->toggleViewAction());

    auto* camera_menu = menuBar()->addMenu(QStringLiteral("&Camera"));
    auto* fit = camera_menu->addAction(QStringLiteral("Frame all"));
    fit->setShortcut(QKeySequence{QStringLiteral("F")});
    connect(fit, &QAction::triggered, viewport_, &CadViewport::fit_all);
    auto* fit_selected = camera_menu->addAction(QStringLiteral("Frame selected"));
    fit_selected->setShortcut(QKeySequence{QStringLiteral("Shift+F")});
    connect(fit_selected, &QAction::triggered, viewport_, &CadViewport::fit_selected);
    auto* projection = camera_menu->addAction(QStringLiteral("Orthographic projection"));
    projection->setCheckable(true);
    connect(projection, &QAction::toggled, viewport_, &CadViewport::set_orthographic);
    auto* standard_views = camera_menu->addMenu(QStringLiteral("Standard View"));
    const auto add_view = [this, standard_views](QString label, StandardView view) {
        auto* action = standard_views->addAction(std::move(label));
        connect(action, &QAction::triggered, this, [this, view] { viewport_->set_standard_view(view); });
    };
    add_view(QStringLiteral("ISO"), StandardView::isometric);
    add_view(QStringLiteral("Front"), StandardView::front);
    add_view(QStringLiteral("Right"), StandardView::right);
    add_view(QStringLiteral("Top"), StandardView::top);

    auto* scene_menu = menuBar()->addMenu(QStringLiteral("&Scene"));
    auto* scene_lighting = scene_menu->addAction(QStringLiteral("Use scene lighting"));
    scene_lighting->setCheckable(true);
    scene_lighting->setChecked(true);
    connect(scene_lighting, &QAction::toggled, viewport_, &CadViewport::set_scene_lighting);
    scene_menu->addSeparator();
    scene_play_action_ = scene_menu->addAction(QStringLiteral("Play Scene"));
    scene_play_action_->setShortcut(QKeySequence{Qt::Key_Space});
    scene_play_action_->setCheckable(true);
    connect(scene_play_action_, &QAction::toggled, this,
            [this](bool enabled) { set_scene_playing(enabled); });
}

auto MainWindow::apply_theme() -> void {
    if (auto* fusion = QStyleFactory::create(QStringLiteral("Fusion")); fusion != nullptr)
        QApplication::setStyle(fusion);
    editor_->setAttribute(Qt::WA_MacShowFocusRect, false);
    workspace_tree_->setAttribute(Qt::WA_MacShowFocusRect, false);
    setStyleSheet(QStringLiteral(R"css(
        QMainWindow, QMenuBar, QMenu, QStatusBar, QDockWidget { background:#17181a; color:#f2f2f7; }
        QMenuBar { border:0; padding:3px 7px; spacing:3px; }
        QMenuBar::item { border:0; border-radius:8px; padding:6px 10px; background:transparent; }
        QMenuBar::item:selected, QMenuBar::item:pressed { background:#333438; }
        QMenu { background:#28292d; border:1px solid #45464b; border-radius:12px; padding:7px; }
        QMenu::item { border:0; border-radius:7px; padding:7px 30px 7px 12px; }
        QMenu::item:selected { background:#0a84ff; color:#ffffff; }
        QMenu::separator { height:1px; background:#45464b; margin:6px 8px; }
        QToolButton, QPushButton { background:#34353a; color:#f2f2f7; border:0; outline:0; border-radius:9px; padding:6px 10px; }
        QToolButton:hover, QPushButton:hover { background:#45464b; }
        QToolButton:pressed, QToolButton:checked, QPushButton:pressed { background:#0a84ff; }
        QDockWidget { border:0; }
        QDockWidget::title { background:#202124; padding:8px 10px; font-weight:600; }
        QPlainTextEdit#sourceEditor { background:#07101f; color:#dbeafe; border:0; selection-background-color:#185fa8; padding:9px; }
        QListWidget, QTreeWidget, QTreeView, QLabel { background:#1c1d20; color:#e5e5ea; border:0; outline:0; }
        QListWidget::item, QTreeWidget::item, QTreeView::item { border:0; border-radius:6px; padding:3px; }
        QListWidget::item:hover, QTreeWidget::item:hover, QTreeView::item:hover { background:#2d2e32; }
        QListWidget::item:selected, QTreeWidget::item:selected, QTreeView::item:selected { background:#0a84ff; color:#ffffff; }
        QTreeView#workspaceTree { background:#202124; padding:5px; }
        QHeaderView::section { background:#25262a; color:#d1d1d6; border:0; padding:6px; }
        QTabBar#documentTabs { background:#202124; border:0; padding:3px 6px 0 6px; }
        QTabBar#documentTabs::tab { background:#292a2e; color:#aeb0b6; border:0; border-radius:7px 7px 0 0; min-width:104px; max-width:210px; height:31px; padding:0 10px; margin-right:3px; }
        QTabBar#documentTabs::tab:hover { background:#35363b; color:#f2f2f7; }
        QTabBar#documentTabs::tab:selected { background:#3a3b40; color:#ffffff; border-bottom:2px solid #0a84ff; }
        QTabBar#documentTabs::close-button { background:transparent; border:0; border-radius:4px; width:16px; height:16px; margin:0 4px 0 2px; }
        QTabBar#documentTabs::close-button:hover { background:#55565c; }
        QTabBar#documentTabs QToolButton#ScrollLeftButton,
        QTabBar#documentTabs QToolButton#ScrollRightButton { background:#292a2e; border:0; border-radius:4px; padding:0; margin:3px 1px; min-width:22px; max-width:22px; min-height:30px; max-height:30px; }
        QTabBar#documentTabs QToolButton#ScrollLeftButton:hover,
        QTabBar#documentTabs QToolButton#ScrollRightButton:hover { background:#45464b; }
        QStatusBar { background:#202124; border-top:1px solid #36373b; }
        QSlider::groove:horizontal { background:#45464b; height:5px; border-radius:3px; }
        QSlider::handle:horizontal { background:#0a84ff; width:14px; margin:-5px 0; border-radius:7px; }
        QScrollBar:vertical { background:transparent; width:7px; margin:2px; }
        QScrollBar::handle:vertical { background:#55565c; min-height:30px; border-radius:3px; }
        QScrollBar::handle:vertical:hover { background:#74757c; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { height:0; background:transparent; }
        QScrollBar:horizontal { background:transparent; height:7px; margin:2px; }
        QScrollBar::handle:horizontal { background:#55565c; min-width:30px; border-radius:3px; }
        QScrollBar::handle:horizontal:hover { background:#74757c; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { width:0; background:transparent; }
        QSplitter::handle { background:#34353a; width:1px; height:1px; }
    )css"));
}

auto MainWindow::open_source_dialog() -> void {
    const auto current_path = active_source_path();
    const auto start_directory = current_path.empty() ? workspace_root_ : current_path.parent_path();
    const QString selected = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open ICAD source"), to_qstring(start_directory),
        QStringLiteral("ICAD source (*.icad)"));
    if (!selected.isEmpty())
        open_source(std::filesystem::path{selected.toStdString()});
}

auto MainWindow::open_folder_dialog() -> void {
    const QString selected = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Open ICAD workspace"), to_qstring(workspace_root_),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!selected.isEmpty())
        open_workspace(std::filesystem::path{selected.toStdString()});
}

auto MainWindow::open_folder(const std::filesystem::path& path) -> bool {
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_directory(absolute, error)) {
        QMessageBox::critical(this, QStringLiteral("Open folder failed"),
                              QStringLiteral("The selected workspace folder is not available."));
        return false;
    }
    workspace_root_ = absolute;
    const QModelIndex root = workspace_model_->setRootPath(to_qstring(workspace_root_));
    workspace_tree_->setRootIndex(root);
    workspace_tree_->setCurrentIndex(root);
    workspace_tree_->expandToDepth(1);
    workspace_dock_->setWindowTitle(
        QStringLiteral("Workspace — %1").arg(QFileInfo{to_qstring(workspace_root_)}.fileName()));
    QSettings{}.setValue(QStringLiteral("workspaceFolder"), to_qstring(workspace_root_));
    workspace_dock_->show();
    statusBar()->showMessage(QStringLiteral("Workspace opened: %1").arg(to_qstring(workspace_root_)),
                             2500);
    return true;
}

auto MainWindow::open_source(const std::filesystem::path& path) -> bool {
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    for (const auto& document : documents_) {
        if (document.path == absolute) {
            document_tabs_->setCurrentIndex(tab_index_for_document_id(document.id));
            return true;
        }
    }
    auto next_session = std::make_shared<engine::Session>(absolute);
    if (!next_session->ready()) {
        QMessageBox::critical(this, QStringLiteral("Open failed"),
                              QString::fromStdString(next_session->error()));
        return false;
    }
    OpenDocument document;
    document.id = next_document_id_++;
    document.path = absolute;
    document.session = std::move(next_session);
    document.source = QString::fromStdString(document.session->source());
    document.saved_source = document.source;
    document.edit_history.push_back(document.source);
    document.history_index = 0;
    documents_.push_back(std::move(document));
    const auto& opened = documents_.back();
    const int tab = document_tabs_->addTab(QFileInfo{to_qstring(absolute)}.fileName());
    document_tabs_->setTabData(tab, QVariant::fromValue<qulonglong>(opened.id));
    document_tabs_->setCurrentIndex(tab);
    if (workspace_root_.empty() || !path_is_within(absolute, workspace_root_))
        open_folder(absolute.parent_path());
    update_recent_files(absolute);
    return true;
}

auto MainWindow::document_index_for_id(std::uint64_t id) const -> int {
    const auto found = std::ranges::find(documents_, id, &OpenDocument::id);
    return found == documents_.end()
               ? -1
               : static_cast<int>(std::distance(documents_.begin(), found));
}

auto MainWindow::document_index_for_tab(int tab_index) const -> int {
    if (tab_index < 0 || tab_index >= document_tabs_->count())
        return -1;
    bool ok = false;
    const auto id = document_tabs_->tabData(tab_index).toULongLong(&ok);
    return ok ? document_index_for_id(static_cast<std::uint64_t>(id)) : -1;
}

auto MainWindow::tab_index_for_document_id(std::uint64_t id) const -> int {
    for (int tab = 0; tab < document_tabs_->count(); ++tab) {
        bool ok = false;
        const auto candidate = document_tabs_->tabData(tab).toULongLong(&ok);
        if (ok && candidate == id)
            return tab;
    }
    return -1;
}

auto MainWindow::active_document() -> OpenDocument* {
    if (active_document_index_ < 0 ||
        active_document_index_ >= static_cast<int>(documents_.size()))
        return nullptr;
    return &documents_[static_cast<std::size_t>(active_document_index_)];
}

auto MainWindow::path_is_within(const std::filesystem::path& path,
                                const std::filesystem::path& directory) -> bool {
    if (directory.empty())
        return false;
    const auto relative = path.lexically_normal().lexically_relative(directory.lexically_normal());
    if (relative.empty())
        return path.lexically_normal() == directory.lexically_normal();
    const auto first = relative.begin();
    return first != relative.end() && *first != std::filesystem::path{".."};
}

auto MainWindow::active_document() const -> const OpenDocument* {
    if (active_document_index_ < 0 ||
        active_document_index_ >= static_cast<int>(documents_.size()))
        return nullptr;
    return &documents_[static_cast<std::size_t>(active_document_index_)];
}

auto MainWindow::active_source_path() const -> std::filesystem::path {
    const auto* document = active_document();
    return document == nullptr ? std::filesystem::path{} : document->path;
}

auto MainWindow::switch_document(int tab_index) -> void {
    const int document_index = document_index_for_tab(tab_index);
    if (document_index < 0 || document_index == active_document_index_)
        return;
    compile_timer_.stop();
    history_timer_.stop();
    set_scene_playing(false);
    active_document_index_ = document_index;
    auto& document = documents_[static_cast<std::size_t>(document_index)];
    restoring_history_ = true;
    editor_->setPlainText(document.source);
    restoring_history_ = false;
    {
        const QSignalBlocker blocker{document_tabs_};
        document_tabs_->setCurrentIndex(tab_index);
    }
    update_document_chrome();
    update_history_actions();
    restore_document_preview(document);
    pending_source_ = document.source;
    if (!document.preview || document.preview->source != document.source) {
        // Defer compilation through the live-edit timer. During startup several
        // positional files are opened synchronously; compiling the first one
        // immediately made the final active tab wait behind an invisible job
        // and left its viewport blank. Restarting the timer on each activation
        // makes the settled active tab compile first.
        schedule_compile();
    } else {
        compile_pending_ = false;
    }
    editor_->setFocus();
}

auto MainWindow::close_document(int tab_index) -> void {
    const int document_index = document_index_for_tab(tab_index);
    if (document_index < 0 || !confirm_close_document(document_index))
        return;
    const bool was_active = document_index == active_document_index_;
    const auto active_id = active_document() == nullptr ? 0U : active_document()->id;
    documents_.erase(documents_.begin() + document_index);
    {
        const QSignalBlocker blocker{document_tabs_};
        document_tabs_->removeTab(tab_index);
    }
    if (documents_.empty()) {
        active_document_index_ = -1;
        close();
        return;
    }
    if (was_active) {
        active_document_index_ = -1;
        const int next_tab = std::min(tab_index, document_tabs_->count() - 1);
        switch_document(next_tab);
    } else {
        active_document_index_ = document_index_for_id(active_id);
        const QSignalBlocker blocker{document_tabs_};
        document_tabs_->setCurrentIndex(tab_index_for_document_id(active_id));
    }
}

auto MainWindow::confirm_close_document(int index) -> bool {
    if (index < 0 || index >= static_cast<int>(documents_.size()))
        return false;
    const auto& document = documents_[static_cast<std::size_t>(index)];
    if (!document.modified)
        return true;
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Unsaved ICAD source"),
        QStringLiteral("Save changes to %1 before closing?")
            .arg(QFileInfo{to_qstring(document.path)}.fileName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    return answer != QMessageBox::Save || save_document(index);
}

auto MainWindow::save_document(int index) -> bool {
    if (index < 0 || index >= static_cast<int>(documents_.size()))
        return false;
    auto& document = documents_[static_cast<std::size_t>(index)];
    const auto result = document.session->save(document.source.toStdString());
    if (!result.success) {
        QMessageBox::critical(this, QStringLiteral("Save failed"),
                              QString::fromStdString(result.message));
        return false;
    }
    document.saved_source = document.source;
    document.modified = false;
    if (index == active_document_index_) {
        capture_history();
        update_document_chrome();
        statusBar()->showMessage(QStringLiteral("Source saved"), 2500);
    }
    return true;
}

auto MainWindow::update_document_chrome() -> void {
    const auto* document = active_document();
    if (document == nullptr)
        return;
    for (int tab = 0; tab < document_tabs_->count(); ++tab) {
        const int document_index = document_index_for_tab(tab);
        if (document_index < 0)
            continue;
        const auto& candidate = documents_[static_cast<std::size_t>(document_index)];
        const QString marker = candidate.modified ? QStringLiteral(" •") : QString{};
        document_tabs_->setTabText(tab,
                                   QFileInfo{to_qstring(candidate.path)}.fileName() + marker);
        document_tabs_->setTabToolTip(tab, to_qstring(candidate.path));
    }
    setWindowModified(document->modified);
    const QString suffix = document->modified ? QStringLiteral("[*]") : QString{};
    setWindowTitle(QStringLiteral("ICAD Studio — %1%2")
                       .arg(QFileInfo{to_qstring(document->path)}.fileName(), suffix));
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

auto MainWindow::confirm_all_changes() -> bool {
    for (int index = 0; index < static_cast<int>(documents_.size()); ++index) {
        if (!confirm_close_document(index))
            return false;
    }
    return true;
}

auto MainWindow::reset_history(const QString& source) -> void {
    auto* document = active_document();
    if (document == nullptr)
        return;
    document->edit_history.clear();
    document->edit_history.push_back(source);
    document->history_index = 0;
    update_history_actions();
}

auto MainWindow::capture_history() -> void {
    if (restoring_history_)
        return;
    auto* document = active_document();
    if (document == nullptr)
        return;
    const QString source = editor_->toPlainText();
    document->source = source;
    if (document->history_index >= 0 &&
        document->edit_history[static_cast<std::size_t>(document->history_index)] == source)
        return;
    if (document->history_index + 1 < static_cast<int>(document->edit_history.size()))
        document->edit_history.erase(document->edit_history.begin() + document->history_index + 1,
                                     document->edit_history.end());
    document->edit_history.push_back(source);
    // Keep one baseline plus 100 navigable edit operations.
    if (document->edit_history.size() > 101U)
        document->edit_history.erase(document->edit_history.begin());
    document->history_index = static_cast<int>(document->edit_history.size()) - 1;
    update_history_actions();
}

auto MainWindow::step_history(int direction) -> void {
    auto* document = active_document();
    if (document == nullptr)
        return;
    history_timer_.stop();
    capture_history();
    const int next = document->history_index + direction;
    if (next < 0 || next >= static_cast<int>(document->edit_history.size()))
        return;
    document->history_index = next;
    restoring_history_ = true;
    editor_->setPlainText(document->edit_history[static_cast<std::size_t>(document->history_index)]);
    restoring_history_ = false;
    document->source = editor_->toPlainText();
    set_modified(document->source != document->saved_source);
    schedule_compile();
    update_history_actions();
    statusBar()->showMessage(QStringLiteral("Operation history %1 / %2")
                                 .arg(document->history_index + 1)
                                 .arg(static_cast<int>(document->edit_history.size())),
                             1800);
}

auto MainWindow::update_history_actions() -> void {
    const auto* document = active_document();
    if (history_back_action_ != nullptr)
        history_back_action_->setEnabled(document != nullptr && document->history_index > 0);
    if (history_next_action_ != nullptr)
        history_next_action_->setEnabled(document != nullptr && document->history_index >= 0 &&
            document->history_index + 1 < static_cast<int>(document->edit_history.size()));
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
    auto* document = active_document();
    if (document == nullptr)
        return;
    document->source = editor_->toPlainText();
    pending_source_ = document->source;
    compile_pending_ = true;
    compile_timer_.start();
}

auto MainWindow::begin_compile() -> void {
    auto* document = active_document();
    if (document == nullptr)
        return;
    if (compile_watcher_.isRunning()) {
        pending_source_ = editor_->toPlainText();
        compile_pending_ = true;
        return;
    }
    compile_timer_.stop();
    const QString source = editor_->toPlainText();
    document->source = source;
    compile_pending_ = false;
    compile_state_->setText(QStringLiteral("Compiling…"));
    compile_state_->setStyleSheet(QStringLiteral("color:#fbbf24"));
    const auto session = document->session;
    const std::uint64_t document_id = document->id;
    compile_watcher_.setFuture(QtConcurrent::run([session, source, document_id] {
        CompileTaskResult task;
        task.document_id = document_id;
        task.source = source;
        task.preview = session->preview(source.toStdString());
        if (task.preview.success && !task.preview.model_json.empty()) {
            auto parsed = parse_render_scene(task.preview.model_json);
            if (parsed.ok())
                task.scene = std::move(parsed.scene);
            else
                task.scene_error = std::move(parsed.error);
        }
        return task;
    }));
}

auto MainWindow::clear_preview_panels() -> void {
    viewport_->clear_scene();
    diagnostics_->clear();
    evidence_->clear();
    model_tree_->clear();
    scenes_->clear();
    properties_->setText(QStringLiteral("No component selected"));
    metrics_->clear();
}

auto MainWindow::apply_preview(const DocumentPreview& preview) -> void {
    if (!preview.result.success) {
        if (preview.scene) {
            viewport_->set_scene(*preview.scene);
            rebuild_model_tree();
            rebuild_scene_panel();
        } else {
            clear_preview_panels();
        }
        update_diagnostics(preview.result);
        update_evidence(preview.result);
        QString failure = QString::fromStdString(preview.result.message);
        if (!preview.result.diagnostics.empty()) {
            const auto& diagnostic = preview.result.diagnostics.front();
            failure = QStringLiteral("Compile failed · %1 at Ln %2, Col %3")
                          .arg(QString::fromStdString(diagnostic.code))
                          .arg(static_cast<qulonglong>(diagnostic.location.line))
                          .arg(static_cast<qulonglong>(diagnostic.location.column));
        }
        compile_state_->setText(failure);
        compile_state_->setToolTip(QString::fromStdString(preview.result.message));
        compile_state_->setStyleSheet(QStringLiteral("color:#fb7185"));
        diagnostics_dock_->show();
        diagnostics_dock_->raise();
        return;
    }
    clear_preview_panels();
    update_diagnostics(preview.result);
    update_evidence(preview.result);
    compile_state_->setToolTip({});
    if (!preview.scene_error.isEmpty()) {
        auto* item = new QListWidgetItem{
            QStringLiteral("ICAD-R0001 · %1").arg(preview.scene_error), diagnostics_};
        item->setForeground(QColor{"#fb7185"});
        compile_state_->setText(preview.scene_error);
        compile_state_->setStyleSheet(QStringLiteral("color:#fb7185"));
        return;
    }
    if (!preview.scene) {
        compile_state_->setText(QStringLiteral("Compiled model contains no render scene"));
        compile_state_->setStyleSheet(QStringLiteral("color:#fb7185"));
        return;
    }
    viewport_->set_scene(*preview.scene);
    rebuild_model_tree();
    rebuild_scene_panel();
    if (!preview.result.engineering_valid) {
        compile_state_->setText(QStringLiteral("Preview ready · engineering issues"));
        compile_state_->setStyleSheet(QStringLiteral("color:#fbbf24"));
    } else if (preview.result.unchanged) {
        compile_state_->setText(QStringLiteral("Preview reused"));
        compile_state_->setStyleSheet(QStringLiteral("color:#4ade80"));
    } else {
        compile_state_->setText(QStringLiteral("Preview ready"));
        compile_state_->setStyleSheet(QStringLiteral("color:#4ade80"));
    }
    metrics_->setText(QStringLiteral("%1 bodies · %2 ms · %3 reused / %4 rebuilt · %5 workers")
                          .arg(static_cast<qulonglong>(preview.result.bodies))
                          .arg(preview.result.milliseconds, 0, 'f', 1)
                          .arg(static_cast<qulonglong>(preview.result.reused_bodies))
                          .arg(static_cast<qulonglong>(preview.result.recomputed_bodies))
                          .arg(static_cast<qulonglong>(preview.result.parallel_workers)));
}

auto MainWindow::restore_document_preview(const OpenDocument& document) -> void {
    if (!document.preview) {
        clear_preview_panels();
        compile_state_->setText(QStringLiteral("Compiling live preview…"));
        compile_state_->setStyleSheet(QStringLiteral("color:#fbbf24"));
        return;
    }
    apply_preview(*document.preview);
    if (document.preview->source != document.source) {
        compile_state_->setText(QStringLiteral("Updating live preview…"));
        compile_state_->setStyleSheet(QStringLiteral("color:#fbbf24"));
    }
}

auto MainWindow::finish_compile() -> void {
    auto task = compile_watcher_.result();
    const int completed_index = document_index_for_id(task.document_id);
    bool applied_to_active = false;
    bool scene_ready = false;
    if (completed_index >= 0) {
        auto& completed = documents_[static_cast<std::size_t>(completed_index)];
        if (completed.source == task.source) {
            DocumentPreview preview;
            preview.source = task.source;
            preview.result = std::move(task.preview);
            if (preview.result.unchanged && completed.preview) {
                preview.scene = std::move(completed.preview->scene);
                preview.scene_error = std::move(completed.preview->scene_error);
            } else if (!preview.result.success && completed.preview) {
                // Keep the last valid model available while the user repairs a
                // source diagnostic. The failed source and diagnostics remain
                // authoritative; only the viewport is deliberately retained.
                preview.scene = std::move(completed.preview->scene);
                preview.scene_error = std::move(completed.preview->scene_error);
            } else {
                preview.scene = std::move(task.scene);
                preview.scene_error = std::move(task.scene_error);
            }
            completed.preview = std::move(preview);
            if (completed_index == active_document_index_) {
                apply_preview(*completed.preview);
                applied_to_active = true;
                scene_ready = completed.preview->scene.has_value() &&
                              completed.preview->scene_error.isEmpty();
            }
        }
    }
    if (applied_to_active && !pending_snapshot_path_.isEmpty()) {
        const QString output_path = std::exchange(pending_snapshot_path_, QString{});
        auto completion = std::exchange(snapshot_completion_, {});
        QTimer::singleShot(180, this,
                           [this, output_path, scene_ready,
                            completion = std::move(completion)] {
            bool saved = false;
            if (scene_ready) {
                viewport_->update();
                viewport_->repaint();
                saved = viewport_->save_screenshot(output_path);
            }
            if (completion)
                completion(saved);
        });
    }
    const auto* document = active_document();
    const bool active_preview_is_current =
        document != nullptr && document->preview && document->preview->source == document->source;
    if (document != nullptr && (compile_pending_ || !active_preview_is_current)) {
        pending_source_ = document->source;
        compile_pending_ = false;
        compile_timer_.start();
    }
}

auto MainWindow::request_snapshot(QString path, std::function<void(bool)> completion) -> void {
    pending_snapshot_path_ = std::move(path);
    snapshot_completion_ = std::move(completion);
}

auto MainWindow::update_diagnostics(const engine::PreviewResult& result) -> void {
    diagnostics_->clear();
    for (const auto& diagnostic : result.diagnostics) {
        const QString description = QStringLiteral("%1 · Ln %2, Col %3 · %4")
                                        .arg(QString::fromStdString(diagnostic.code))
                                        .arg(static_cast<qulonglong>(diagnostic.location.line))
                                        .arg(static_cast<qulonglong>(diagnostic.location.column))
                                        .arg(QString::fromStdString(diagnostic.message));
        auto* item = new QListWidgetItem{description, diagnostics_};
        item->setForeground(severity_color(diagnostic.severity));
        item->setData(Qt::UserRole, static_cast<qulonglong>(diagnostic.location.line));
        item->setData(Qt::UserRole + 1,
                      static_cast<qulonglong>(diagnostic.location.column));
        item->setToolTip(QStringLiteral("Click to jump to Ln %1, Col %2")
                             .arg(static_cast<qulonglong>(diagnostic.location.line))
                             .arg(static_cast<qulonglong>(diagnostic.location.column)));
    }
    if (result.diagnostics.empty())
        diagnostics_->addItem(QStringLiteral("No syntax, topology, assembly, or manufacturing diagnostics."));
}

auto MainWindow::update_evidence(const engine::PreviewResult& result) -> void {
    evidence_->clear();
    if (result.evidence_json.empty()) {
        auto* item = new QListWidgetItem{
            QStringLiteral("No adjacent .evidence.json manifest; model remains unmanaged development geometry."),
            evidence_};
        item->setForeground(QColor{"#fbbf24"});
        return;
    }
    const auto parsed = json::parse(result.evidence_json);
    if (!parsed.ok()) {
        auto* item = new QListWidgetItem{QStringLiteral("Evidence result could not be parsed."), evidence_};
        item->setForeground(QColor{"#fb7185"});
        return;
    }
    const auto string_value = [&](std::string_view name) -> QString {
        const auto* value = parsed.value->find(name);
        const auto* text = value == nullptr ? nullptr : value->string();
        return text == nullptr ? QStringLiteral("unknown") : QString::fromStdString(*text);
    };
    const auto bool_value = [&](std::string_view name) -> bool {
        const auto* value = parsed.value->find(name);
        const auto* flag = value == nullptr ? nullptr : value->boolean();
        return flag != nullptr && *flag;
    };
    const auto number_value = [&](std::string_view name) -> qulonglong {
        const auto* value = parsed.value->find(name);
        const auto* number = value == nullptr ? nullptr : value->number();
        return number == nullptr ? 0U : static_cast<qulonglong>(*number);
    };
    evidence_->addItem(QStringLiteral("Lifecycle: %1").arg(string_value("lifecycleState")));
    evidence_->addItem(QStringLiteral("Basis: %1").arg(string_value("basis")));
    auto* status = new QListWidgetItem{
        bool_value("releaseReady") ? QStringLiteral("Ground-test release evidence accepted")
                                    : QStringLiteral("Ground-test release blocked"), evidence_};
    status->setForeground(bool_value("releaseReady") ? QColor{"#4ade80"} : QColor{"#fb7185"});
    evidence_->addItem(QStringLiteral("Open requirements: %1 · Open applicable paragraphs: %2 · Blocking hazards: %3")
                           .arg(number_value("openRequirements"))
                           .arg(number_value("openApplicableCompliance"))
                           .arg(number_value("blockingHazards")));
    evidence_dock_->raise();
    const auto* issues_value = parsed.value->find("issues");
    const auto* issues = issues_value == nullptr ? nullptr : issues_value->array();
    if (issues == nullptr || issues->empty()) {
        evidence_->addItem(QStringLiteral("Manifest integrity: valid"));
        return;
    }
    for (const auto& issue : *issues) {
        const auto* code_value = issue.find("code");
        const auto* message_value = issue.find("message");
        const auto* line_value = issue.find("line");
        const auto* column_value = issue.find("column");
        const auto* code = code_value == nullptr ? nullptr : code_value->string();
        const auto* message = message_value == nullptr ? nullptr : message_value->string();
        const auto line = line_value == nullptr || line_value->number() == nullptr
                              ? 1.0
                              : *line_value->number();
        const auto column = column_value == nullptr || column_value->number() == nullptr
                                ? 1.0
                                : *column_value->number();
        auto* item = new QListWidgetItem{
            QStringLiteral("%1 · Ln %2, Col %3 · %4")
                .arg(code == nullptr ? QStringLiteral("ICAD-E") : QString::fromStdString(*code))
                .arg(static_cast<qulonglong>(line))
                .arg(static_cast<qulonglong>(column))
                .arg(message == nullptr ? QStringLiteral("Evidence issue")
                                        : QString::fromStdString(*message)),
            evidence_};
        item->setForeground(QColor{"#fb7185"});
    }
}

auto MainWindow::jump_to_diagnostic(QListWidgetItem* item) -> void {
    if (item == nullptr)
        return;
    const int line = item->data(Qt::UserRole).toInt();
    const int column = item->data(Qt::UserRole + 1).toInt();
    if (line <= 0 || column <= 0)
        return;
    auto cursor = editor_->textCursor();
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, line - 1);
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, column - 1);
    editor_->setTextCursor(cursor);
    editor_->centerCursor();
    editor_->setFocus();
}

auto MainWindow::update_cursor_position() -> void {
    if (editor_ == nullptr || cursor_position_ == nullptr)
        return;
    const auto cursor = editor_->textCursor();
    cursor_position_->setText(QStringLiteral("Ln %1, Col %2")
                                  .arg(cursor.blockNumber() + 1)
                                  .arg(cursor.positionInBlock() + 1));
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
    model_tree_->header()->resizeSection(1,
        std::max(model_tree_->header()->sectionSizeHint(1), 90));
}

auto MainWindow::rebuild_scene_panel() -> void {
    scenes_->clear();
    for (const auto& scene : viewport_->scene().scenes) {
        auto* item = new QListWidgetItem{
            QStringLiteral("▶ %1 · %2s · %3 · %4L")
                .arg(scene.name)
                .arg(scene.duration_seconds, 0, 'f', 1)
                .arg(scene.background)
                .arg(static_cast<qulonglong>(scene.lights.size())),
            scenes_};
        item->setToolTip(QStringLiteral("%1 fps · %2 authored light%3")
                             .arg(scene.frames_per_second, 0, 'f', 0)
                             .arg(static_cast<qulonglong>(scene.lights.size()))
                             .arg(scene.lights.size() == 1 ? QString{} : QStringLiteral("s")));
    }
    if (!viewport_->scene().scenes.empty())
        scenes_->setCurrentRow(0);
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
    if (auto* item = model_tree_->topLevelItem(static_cast<int>(*index)); item != nullptr) {
        model_tree_->setCurrentItem(item);
        model_tree_->scrollToItem(item, QAbstractItemView::EnsureVisible);
    }
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
    auto* document = active_document();
    if (document == nullptr)
        return false;
    document->source = editor_->toPlainText();
    return save_document(active_document_index_);
}

auto MainWindow::export_package() -> void {
    auto* document = active_document();
    if (document == nullptr)
        return;
    document->source = editor_->toPlainText();
    const QString suggested = to_qstring(document->session->default_export_directory());
    const QString selected = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Export ICAD manufacturing package"), suggested,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (selected.isEmpty())
        return;
    compile_state_->setText(QStringLiteral("Exporting package…"));
    const auto result = document->session->export_package(
        document->source.toStdString(), std::filesystem::path{selected.toStdString()});
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
        QFileInfo{to_qstring(active_source_path())}.completeBaseName() + QStringLiteral(".png"),
        QStringLiteral("PNG image (*.png)"));
    if (!path.isEmpty() && !viewport_->save_screenshot(path))
        QMessageBox::critical(this, QStringLiteral("Screenshot failed"),
                              QStringLiteral("Could not write the selected PNG file."));
}

auto MainWindow::set_modified(bool modified) -> void {
    auto* document = active_document();
    if (document == nullptr)
        return;
    document->modified = modified;
    update_document_chrome();
}

auto MainWindow::closeEvent(QCloseEvent* event) -> void {
    if (confirm_all_changes()) {
        event->accept();
    } else {
        event->ignore();
    }
}

} // namespace icad::desktop
