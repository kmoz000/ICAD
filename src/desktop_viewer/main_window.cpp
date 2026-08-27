#include "main_window.hpp"

#include "icad_highlighter.hpp"
#include "scene_model.hpp"

#include <QAction>
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
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <chrono>
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
    build_actions();
    apply_theme();
    setWindowIcon(QIcon{QStringLiteral(":/icad/icons/icad-256.png")});
    setUnifiedTitleAndToolBarOnMac(true);
    resize(1540, 960);
    resizeDocks({diagnostics_dock_}, {145}, Qt::Vertical);

    compile_timer_.setSingleShot(true);
    compile_timer_.setInterval(110);
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
    connect(document_tabs_, &QTabBar::currentChanged, this,
            [this](int index) { switch_document(index); });
    connect(document_tabs_, &QTabBar::tabCloseRequested, this,
            [this](int index) { close_document(index); });
    connect(workspace_tree_, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        const QFileInfo entry = workspace_model_->fileInfo(index);
        if (entry.isFile() && entry.suffix().compare(QStringLiteral("icad"), Qt::CaseInsensitive) == 0)
            open_source(std::filesystem::path{entry.absoluteFilePath().toStdString()});
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
        document_tabs_->addTab(QFileInfo{to_qstring(absolute_path)}.fileName());
    }
    switch_document(0);
    open_folder(absolute_path.parent_path());
    update_recent_files(absolute_path);
}

auto MainWindow::set_standard_view(StandardView view) -> void {
    viewport_->set_standard_view(view);
    viewport_->fit_all();
}

auto MainWindow::open_document(const std::filesystem::path& path) -> bool {
    return open_source(path);
}

auto MainWindow::open_workspace(const std::filesystem::path& path) -> bool {
    return open_folder(path);
}

MainWindow::~MainWindow() {
    compile_timer_.stop();
    if (compile_watcher_.isRunning())
        compile_watcher_.waitForFinished();
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
    diagnostics_->setAlternatingRowColors(true);
    diagnostics_dock_ = new QDockWidget{QStringLiteral("Diagnostics"), this};
    diagnostics_dock_->setObjectName(QStringLiteral("diagnosticsDock"));
    diagnostics_dock_->setWidget(diagnostics_);
    addDockWidget(Qt::BottomDockWidgetArea, diagnostics_dock_);

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
    metrics_ = new QLabel{this};
    statusBar()->addWidget(compile_state_);
    statusBar()->addPermanentWidget(metrics_);
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
            [this] { close_document(active_document_index_); });
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
    view_menu->addAction(workspace_dock_->toggleViewAction());
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
        QTabBar#documentTabs QToolButton { background:transparent; border:0; border-radius:4px; padding:0; margin:0 5px 0 2px; min-width:16px; max-width:16px; min-height:16px; max-height:16px; qproperty-iconSize:10px 10px; }
        QTabBar#documentTabs QToolButton:hover { background:#55565c; }
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
        open_folder(std::filesystem::path{selected.toStdString()});
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
    for (std::size_t index = 0; index < documents_.size(); ++index) {
        if (documents_[index].path == absolute) {
            document_tabs_->setCurrentIndex(static_cast<int>(index));
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
    document_tabs_->addTab(QFileInfo{to_qstring(absolute)}.fileName());
    document_tabs_->setCurrentIndex(static_cast<int>(documents_.size()) - 1);
    if (workspace_root_.empty() || !path_is_within(absolute, workspace_root_))
        open_folder(absolute.parent_path());
    update_recent_files(absolute);
    return true;
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

auto MainWindow::switch_document(int index) -> void {
    if (index < 0 || index >= static_cast<int>(documents_.size()) ||
        index == active_document_index_)
        return;
    compile_timer_.stop();
    history_timer_.stop();
    set_scene_playing(false);
    active_document_index_ = index;
    auto& document = documents_[static_cast<std::size_t>(index)];
    restoring_history_ = true;
    editor_->setPlainText(document.source);
    restoring_history_ = false;
    viewport_->clear_scene();
    diagnostics_->clear();
    model_tree_->clear();
    scenes_->clear();
    properties_->setText(QStringLiteral("No component selected"));
    metrics_->clear();
    {
        const QSignalBlocker blocker{document_tabs_};
        document_tabs_->setCurrentIndex(index);
    }
    update_document_chrome();
    update_history_actions();
    pending_source_ = document.source;
    compile_pending_ = true;
    begin_compile();
    editor_->setFocus();
}

auto MainWindow::close_document(int index) -> void {
    if (index < 0 || index >= static_cast<int>(documents_.size()) ||
        !confirm_close_document(index))
        return;
    const bool was_active = index == active_document_index_;
    documents_.erase(documents_.begin() + index);
    {
        const QSignalBlocker blocker{document_tabs_};
        document_tabs_->removeTab(index);
    }
    if (documents_.empty()) {
        active_document_index_ = -1;
        close();
        return;
    }
    if (index < active_document_index_)
        --active_document_index_;
    if (was_active) {
        active_document_index_ = -1;
        switch_document(std::min(index, static_cast<int>(documents_.size()) - 1));
    } else {
        const QSignalBlocker blocker{document_tabs_};
        document_tabs_->setCurrentIndex(active_document_index_);
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
    for (std::size_t index = 0; index < documents_.size(); ++index) {
        const auto& candidate = documents_[index];
        const QString marker = candidate.modified ? QStringLiteral(" •") : QString{};
        document_tabs_->setTabText(static_cast<int>(index),
                                   QFileInfo{to_qstring(candidate.path)}.fileName() + marker);
        document_tabs_->setTabToolTip(static_cast<int>(index), to_qstring(candidate.path));
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
        return CompileTaskResult{document_id, source, session->preview(source.toStdString())};
    }));
}

auto MainWindow::finish_compile() -> void {
    const auto task = compile_watcher_.result();
    const auto* document = active_document();
    const bool belongs_to_active_document =
        document != nullptr && document->id == task.document_id && document->source == task.source;
    if (!belongs_to_active_document) {
        if (compile_pending_ || (document != nullptr && pending_source_ != document->source)) {
            pending_source_ = document == nullptr ? QString{} : document->source;
            compile_pending_ = false;
            begin_compile();
        }
        return;
    }
    const auto& result = task.preview;
    bool scene_ready = false;
    update_diagnostics(result);
    if (result.success) {
        if (!result.model_json.empty()) {
            auto parsed = parse_render_scene(result.model_json);
            if (parsed.ok()) {
                viewport_->set_scene(std::move(parsed.scene));
                rebuild_model_tree();
                rebuild_scene_panel();
                scene_ready = true;
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
    if (!pending_snapshot_path_.isEmpty()) {
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
    if (compile_pending_ || pending_source_ != document->source) {
        pending_source_ = document->source;
        compile_pending_ = false;
        begin_compile();
    }
}

auto MainWindow::request_snapshot(QString path, std::function<void(bool)> completion) -> void {
    pending_snapshot_path_ = std::move(path);
    snapshot_completion_ = std::move(completion);
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
        diagnostics_->addItem(QStringLiteral("No syntax, topology, assembly, or manufacturing diagnostics."));
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
