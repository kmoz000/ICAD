#include "main_window.hpp"
#include "scene_model.hpp"

#include "icad/engine/session.hpp"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QIcon>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QSurfaceFormat>
#include <QTabBar>
#include <QThread>
#include <QTimer>
#include <QTextCursor>
#include <QTreeView>
#include <QTreeWidget>

#include <filesystem>
#include <iostream>
#include <algorithm>
#include <memory>
#include <ranges>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

auto configure_parser(QCommandLineParser& parser, QCommandLineOption& self_test_option,
                      QCommandLineOption& window_test_option,
                      QCommandLineOption& view_option,
                      QCommandLineOption& display_option,
                      QCommandLineOption& assembly_inspection_option,
                      QCommandLineOption& cutaway_option,
                      QCommandLineOption& snapshot_option,
                      QCommandLineOption& studio_snapshot_option) -> void {
    parser.setApplicationDescription(
        QStringLiteral("Native Qt/OpenGL IDE and live viewer for agentic ICAD models."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(self_test_option);
    parser.addOption(window_test_option);
    parser.addOption(view_option);
    parser.addOption(display_option);
    parser.addOption(assembly_inspection_option);
    parser.addOption(cutaway_option);
    parser.addOption(snapshot_option);
    parser.addOption(studio_snapshot_option);
    parser.addPositionalArgument(
        QStringLiteral("source.icad|workspace"),
        QStringLiteral("One or more ICAD source files, or one workspace folder, to edit and preview."),
        QStringLiteral("[source.icad…|workspace]"));
}

[[nodiscard]] auto workspace_entry_source(const std::filesystem::path& directory)
    -> std::optional<QString> {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(directory).lexically_normal();
    const auto main_source = absolute / "main.icad";
    if (std::filesystem::is_regular_file(main_source, error))
        return QFileInfo{QString::fromStdString(main_source.string())}.absoluteFilePath();

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
            QFileInfo{QString::fromStdString(iterator->path().string())}.suffix().compare(
                QStringLiteral("icad"), Qt::CaseInsensitive) == 0) {
            sources.push_back(iterator->path());
        }
    }
    std::ranges::sort(sources);
    if (sources.empty())
        return std::nullopt;
    return QFileInfo{QString::fromStdString(sources.front().string())}.absoluteFilePath();
}

[[nodiscard]] auto source_argument(const QString& argument) -> std::optional<QString> {
    const QFileInfo info{argument};
    if (info.isDir())
        return workspace_entry_source(std::filesystem::path{info.absoluteFilePath().toStdString()});
    if (info.isFile() &&
        info.suffix().compare(QStringLiteral("icad"), Qt::CaseInsensitive) == 0) {
        return info.absoluteFilePath();
    }
    return std::nullopt;
}

[[nodiscard]] auto standard_view(QString name) -> std::optional<icad::desktop::StandardView> {
    name = name.toLower();
    if (name == QStringLiteral("isometric")) return icad::desktop::StandardView::isometric;
    if (name == QStringLiteral("front")) return icad::desktop::StandardView::front;
    if (name == QStringLiteral("back")) return icad::desktop::StandardView::back;
    if (name == QStringLiteral("left")) return icad::desktop::StandardView::left;
    if (name == QStringLiteral("right")) return icad::desktop::StandardView::right;
    if (name == QStringLiteral("top")) return icad::desktop::StandardView::top;
    if (name == QStringLiteral("bottom")) return icad::desktop::StandardView::bottom;
    return std::nullopt;
}

[[nodiscard]] auto display_mode(QString name) -> std::optional<icad::desktop::DisplayMode> {
    name = name.toLower();
    if (name == QStringLiteral("solid")) return icad::desktop::DisplayMode::solid;
    if (name == QStringLiteral("solid-mesh")) return icad::desktop::DisplayMode::solid_with_mesh;
    if (name == QStringLiteral("cad-wire")) return icad::desktop::DisplayMode::cad_wireframe;
    if (name == QStringLiteral("mesh-wire")) return icad::desktop::DisplayMode::mesh_wireframe;
    return std::nullopt;
}

auto configure_application_metadata() -> void {
    QCoreApplication::setApplicationName(QStringLiteral("ICAD Studio"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ICAD_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("ICAD"));
}

auto self_test(const QString& source) -> int {
    icad::engine::Session session{std::filesystem::path{source.toStdString()}};
    if (!session.ready()) {
        std::cerr << "QT_VIEWER_SELF_TEST failed: " << session.error() << '\n';
        return 2;
    }
    const auto preview = session.preview(session.source());
    if (!preview.success) {
        std::cerr << "QT_VIEWER_SELF_TEST failed: " << preview.message << '\n';
        return 3;
    }
    const auto scene = icad::desktop::parse_render_scene(preview.model_json);
    if (!scene.ok()) {
        std::cerr << "QT_VIEWER_SELF_TEST failed: " << scene.error.toStdString() << '\n';
        return 4;
    }
    std::cout << "QT_VIEWER_SELF_TEST passed parts=" << scene.scene.parts.size()
              << " triangles=" << scene.scene.indices.size() / 3U
              << " mesh_edges=" << scene.scene.mesh_wire_indices.size() / 2U
              << " scenes=" << scene.scene.scenes.size()
              << " lights="
              << (scene.scene.scenes.empty() ? 0U : scene.scene.scenes.front().lights.size())
              << '\n';
    return 0;
}

} // namespace

auto main(int argc, char** argv) -> int {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    const bool requested_self_test = std::ranges::any_of(
        std::span{argv, static_cast<std::size_t>(argc)}, [](const char* argument) {
            return std::string_view{argument} == "--self-test";
        });
    const bool requested_core_only = requested_self_test || std::ranges::any_of(
        std::span{argv, static_cast<std::size_t>(argc)}, [](const char* argument) {
            const std::string_view option{argument};
            return option == "--help" || option == "-h" || option == "--version" ||
                   option == "-v";
        });
    QCommandLineOption self_test_option{QStringLiteral("self-test"),
                                        QStringLiteral("Compile and validate a scene without opening a window.")};
    QCommandLineOption window_test_option{
        QStringLiteral("window-test"),
        QStringLiteral("Construct and validate the native editor window without entering the event loop.")};
    QCommandLineOption view_option{QStringLiteral("view"),
                                   QStringLiteral("Initial standard 3D view."),
                                   QStringLiteral("side"), QStringLiteral("isometric")};
    QCommandLineOption display_option{
        QStringLiteral("display"),
        QStringLiteral("Initial viewport shading: solid, solid-mesh, cad-wire, or mesh-wire."),
        QStringLiteral("mode"), QStringLiteral("solid")};
    QCommandLineOption assembly_inspection_option{
        QStringLiteral("assembly-inspection"),
        QStringLiteral("Render casing shells translucently and overlay joint axes and fit datums.")};
    QCommandLineOption cutaway_option{
        QStringLiteral("cutaway"),
        QStringLiteral("Render casing shells translucently without assembly annotation overlays.")};
    QCommandLineOption snapshot_option{
        QStringLiteral("snapshot"),
        QStringLiteral("Render the compiled viewport to a PNG and exit."),
        QStringLiteral("output.png")};
    QCommandLineOption studio_snapshot_option{
        QStringLiteral("studio-snapshot"),
        QStringLiteral("Capture the complete rendered ICAD Studio window to a PNG and exit."),
        QStringLiteral("output.png")};
    if (requested_core_only) {
        QCoreApplication application{argc, argv};
        configure_application_metadata();
        QCommandLineParser parser;
        configure_parser(parser, self_test_option, window_test_option, view_option, display_option,
                         assembly_inspection_option, cutaway_option, snapshot_option, studio_snapshot_option);
        parser.process(application);
        const auto positional = parser.positionalArguments();
        if (!requested_self_test)
            return 0;
        if (positional.size() != 1)
            parser.showHelp(2);
        const auto resolved = source_argument(positional.front());
        if (!resolved) {
            std::cerr << "icad-viewer: expected an ICAD source or workspace folder\n";
            return 2;
        }
        return self_test(*resolved);
    }

    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication application{argc, argv};
    configure_application_metadata();
    QApplication::setApplicationDisplayName(QStringLiteral("ICAD Studio"));
    QApplication::setWindowIcon(QIcon{QStringLiteral(":/icad/icons/icad-256.png")});

    QCommandLineParser parser;
    configure_parser(parser, self_test_option, window_test_option, view_option, display_option,
                     assembly_inspection_option, cutaway_option, snapshot_option, studio_snapshot_option);
    parser.process(application);
    const auto initial_view = standard_view(parser.value(view_option));
    if (!initial_view) {
        std::cerr << "icad-viewer: --view expects isometric, front, back, left, right, top, or bottom\n";
        return 2;
    }
    const auto initial_display = display_mode(parser.value(display_option));
    if (!initial_display) {
        std::cerr << "icad-viewer: --display expects solid, solid-mesh, cad-wire, or mesh-wire\n";
        return 2;
    }
    const auto positional = parser.positionalArguments();
    QStringList sources;
    std::optional<std::filesystem::path> workspace;
    QString source;
    if (positional.empty()) {
        source = QFileDialog::getOpenFileName(nullptr, QStringLiteral("Open ICAD source"), QString{},
                                              QStringLiteral("ICAD source (*.icad)"));
        if (source.isEmpty())
            return 0;
        sources.push_back(QFileInfo{source}.absoluteFilePath());
    } else {
        for (const QString& candidate : positional) {
            const QFileInfo info{candidate};
            if (info.isDir()) {
                if (positional.size() != 1) {
                    std::cerr << "icad-viewer: a workspace folder cannot be combined with source files\n";
                    return 2;
                }
                workspace = std::filesystem::path{info.absoluteFilePath().toStdString()};
            }
            const auto resolved = source_argument(candidate);
            if (!resolved) {
                std::cerr << "icad-viewer: expected an ICAD source or workspace folder\n";
                return 2;
            }
            sources.push_back(*resolved);
        }
        source = sources.front();
    }
    icad::desktop::MainWindow window{std::filesystem::path{source.toStdString()}};
    if (!window.ready()) {
        QMessageBox::critical(nullptr, QStringLiteral("ICAD Studio"), window.error());
        return 2;
    }
    if (workspace && !window.open_workspace(*workspace))
        return 2;
    for (qsizetype index = 1; index < sources.size(); ++index) {
        if (!window.open_document(
                std::filesystem::path{sources[index].toStdString()}))
            return 2;
    }
    window.set_standard_view(*initial_view);
    window.set_display_mode(*initial_display);
    window.set_assembly_inspection(parser.isSet(assembly_inspection_option));
    window.set_cutaway(parser.isSet(cutaway_option));
    if (parser.isSet(window_test_option)) {
        window.show();
        auto* tabs = window.findChild<QTabBar*>(QStringLiteral("documentTabs"));
        const auto* workspace_tree = window.findChild<QTreeView*>(QStringLiteral("workspaceTree"));
        const auto* model = window.findChild<QTreeWidget*>(QStringLiteral("modelTree"));
        auto* editor = window.findChild<QPlainTextEdit*>(QStringLiteral("sourceEditor"));
        const auto* compile_state = window.findChild<QLabel*>(QStringLiteral("compileState"));
        const auto* cursor_position =
            window.findChild<QLabel*>(QStringLiteral("cursorPosition"));
        auto* diagnostics =
            window.findChild<QListWidget*>(QStringLiteral("diagnosticsList"));
        const auto* evidence =
            window.findChild<QListWidget*>(QStringLiteral("evidenceList"));
        const bool has_open_folder = std::ranges::any_of(
            window.findChildren<QAction*>(), [](const QAction* action) {
                return action->text().remove(QLatin1Char('&')) == QStringLiteral("Open Folder…");
            });
        const bool has_frame_selected = std::ranges::any_of(
            window.findChildren<QAction*>(), [](const QAction* action) {
                return action->text().remove(QLatin1Char('&')) == QStringLiteral("Frame selected");
            });
        const bool has_mesh_mode = std::ranges::any_of(
            window.findChildren<QAction*>(), [](const QAction* action) {
                return action->text().remove(QLatin1Char('&')) == QStringLiteral("Triangle mesh");
            });
        const bool has_scene_lighting = std::ranges::any_of(
            window.findChildren<QAction*>(), [](const QAction* action) {
                return action->text().remove(QLatin1Char('&')) ==
                       QStringLiteral("Use scene lighting");
            });
        const int expected_tabs = std::max(1, static_cast<int>(sources.size()));
        if (tabs == nullptr || tabs->count() != expected_tabs ||
            tabs->height() > 40 || !tabs->isMovable() || tabs->elideMode() != Qt::ElideRight ||
            window.document_count() != static_cast<std::size_t>(expected_tabs) ||
            workspace_tree == nullptr || workspace_tree->model() == nullptr ||
            window.workspace_root().empty() || model == nullptr ||
            editor == nullptr || compile_state == nullptr || cursor_position == nullptr ||
            diagnostics == nullptr || evidence == nullptr ||
            model->selectionBehavior() != QAbstractItemView::SelectRows ||
            model->header()->sectionResizeMode(0) != QHeaderView::Stretch || !has_open_folder ||
            !has_frame_selected || !has_mesh_mode || !has_scene_lighting) {
            std::cerr << "QT_VIEWER_WINDOW_TEST failed: workspace shell is incomplete\n";
            return 3;
        }
        const auto wait_for_preview = [&application, model, compile_state] {
            QElapsedTimer elapsed;
            elapsed.start();
            while (elapsed.elapsed() < 60000) {
                application.processEvents(QEventLoop::AllEvents, 20);
                if (model->topLevelItemCount() > 0 &&
                    compile_state->text().startsWith(QStringLiteral("Preview"))) {
                    return true;
                }
                QThread::msleep(2);
            }
            return false;
        };
        if (!wait_for_preview()) {
            std::cerr << "QT_VIEWER_WINDOW_TEST failed: active document did not compile automatically"
                      << " title=" << window.windowTitle().toStdString()
                      << " parts=" << model->topLevelItemCount()
                      << " state=" << compile_state->text().toStdString()
                      << " current_tab=" << tabs->currentIndex() << '\n';
            return 4;
        }
        if (expected_tabs > 1) {
            tabs->setCurrentIndex(0);
            if (!wait_for_preview()) {
                std::cerr << "QT_VIEWER_WINDOW_TEST failed: switched document did not compile automatically\n";
                return 5;
            }
            tabs->setCurrentIndex(expected_tabs - 1);
            if (model->topLevelItemCount() == 0 ||
                !compile_state->text().startsWith(QStringLiteral("Preview"))) {
                std::cerr << "QT_VIEWER_WINDOW_TEST failed: cached preview was not restored immediately"
                          << " tab=" << tabs->currentIndex()
                          << " title=" << window.windowTitle().toStdString()
                          << " parts=" << model->topLevelItemCount()
                          << " state=" << compile_state->text().toStdString() << '\n';
                return 6;
            }
            const QString moved_path = tabs->tabToolTip(0);
            tabs->moveTab(0, expected_tabs - 1);
            tabs->setCurrentIndex(expected_tabs - 1);
            application.processEvents(QEventLoop::AllEvents, 20);
            QFile source_file{moved_path};
            QString expected_source;
            const bool source_opened = source_file.open(QIODevice::ReadOnly);
            if (source_opened) {
                expected_source = QString::fromUtf8(source_file.readAll());
                expected_source.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
                expected_source.replace(QLatin1Char('\r'), QLatin1Char('\n'));
            }
            if (!source_opened || editor->toPlainText() != expected_source ||
                !window.windowTitle().contains(QFileInfo{moved_path}.fileName()) ||
                model->topLevelItemCount() == 0) {
                std::cerr << "QT_VIEWER_WINDOW_TEST failed: moved tab lost its document identity"
                          << " source_opened=" << source_opened
                          << " source_matches=" << (editor->toPlainText() == expected_source)
                          << " title=" << window.windowTitle().toStdString()
                          << " expected_file=" << QFileInfo{moved_path}.fileName().toStdString()
                          << " parts=" << model->topLevelItemCount()
                          << " state=" << compile_state->text().toStdString()
                          << " current_tab=" << tabs->currentIndex() << '\n';
                return 7;
            }
        }
        editor->moveCursor(QTextCursor::End);
        application.processEvents(QEventLoop::AllEvents, 20);
        if (!cursor_position->text().startsWith(QStringLiteral("Ln ")) ||
            !cursor_position->text().contains(QStringLiteral(", Col "))) {
            std::cerr << "QT_VIEWER_WINDOW_TEST failed: cursor line and column are absent\n";
            return 8;
        }
        const int valid_part_count = model->topLevelItemCount();
        editor->insertPlainText(QStringLiteral("\n$\n"));
        QElapsedTimer diagnostic_wait;
        diagnostic_wait.start();
        while (diagnostic_wait.elapsed() < 5000 &&
               !compile_state->text().startsWith(QStringLiteral("Compile failed"))) {
            application.processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(2);
        }
        if (!compile_state->text().contains(QStringLiteral("Ln ")) ||
            !compile_state->text().contains(QStringLiteral(", Col ")) ||
            diagnostics->count() == 0 ||
            !diagnostics->item(0)->text().contains(QStringLiteral("Ln ")) ||
            !diagnostics->item(0)->text().contains(QStringLiteral(", Col ")) ||
            model->topLevelItemCount() != valid_part_count) {
            std::cerr << "QT_VIEWER_WINDOW_TEST failed: precise diagnostics or last-valid preview are missing"
                      << " state=" << compile_state->text().toStdString()
                      << " diagnostics=" << diagnostics->count()
                      << " parts=" << model->topLevelItemCount() << '\n';
            return 9;
        }
        std::cout << "QT_VIEWER_WINDOW_TEST passed\n";
        return 0;
    }
    if (parser.isSet(snapshot_option) && parser.isSet(studio_snapshot_option)) {
        std::cerr << "icad-viewer: --snapshot and --studio-snapshot cannot be combined\n";
        return 2;
    }
    if (parser.isSet(snapshot_option)) {
        const QString output = QFileInfo{parser.value(snapshot_option)}.absoluteFilePath();
        if (QFileInfo{output}.suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) != 0) {
            std::cerr << "icad-viewer: --snapshot expects a .png output path\n";
            return 2;
        }
        if (!QDir{}.mkpath(QFileInfo{output}.absolutePath())) {
            std::cerr << "icad-viewer: could not create the snapshot output directory\n";
            return 2;
        }
        window.request_snapshot(output, [&application, output](bool saved) {
            if (saved)
                std::cout << "ICAD_VIEWER_SNAPSHOT " << output.toStdString() << '\n';
            else
                std::cerr << "icad-viewer: could not write snapshot " << output.toStdString()
                          << '\n';
            application.exit(saved ? 0 : 3);
        });
    }
    window.show();
    if (parser.isSet(studio_snapshot_option)) {
        const QString output = QFileInfo{parser.value(studio_snapshot_option)}.absoluteFilePath();
        if (QFileInfo{output}.suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) != 0) {
            std::cerr << "icad-viewer: --studio-snapshot expects a .png output path\n";
            return 2;
        }
        if (!QDir{}.mkpath(QFileInfo{output}.absolutePath())) {
            std::cerr << "icad-viewer: could not create the Studio snapshot output directory\n";
            return 2;
        }
        auto elapsed = std::make_shared<QElapsedTimer>();
        elapsed->start();
        auto* capture_timer = new QTimer{&window};
        capture_timer->setInterval(100);
        QObject::connect(capture_timer, &QTimer::timeout, &window,
                         [&window, &application, output, elapsed, capture_timer] {
            const auto* state = window.findChild<QLabel*>(QStringLiteral("compileState"));
            const QString text = state == nullptr ? QString{} : state->text();
            const bool progress = text == QStringLiteral("Ready") ||
                                  text.contains(QStringLiteral("Compiling")) ||
                                  text.contains(QStringLiteral("Updating"));
            if (progress && elapsed->elapsed() < 60000)
                return;
            capture_timer->stop();
            QTimer::singleShot(180, &window, [&window, &application, output] {
                const bool saved = window.grab().save(output, "PNG");
                if (saved)
                    std::cout << "ICAD_STUDIO_SNAPSHOT " << output.toStdString() << '\n';
                else
                    std::cerr << "icad-viewer: could not write Studio snapshot "
                              << output.toStdString() << '\n';
                application.exit(saved ? 0 : 3);
            });
        });
        capture_timer->start();
    }
    return QApplication::exec();
}
