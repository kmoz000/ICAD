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
#include <QFileInfo>
#include <QFileDialog>
#include <QIcon>
#include <QHeaderView>
#include <QMessageBox>
#include <QPixmap>
#include <QSurfaceFormat>
#include <QTabBar>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>

#include <filesystem>
#include <iostream>
#include <algorithm>
#include <ranges>
#include <optional>
#include <span>
#include <string_view>

namespace {

auto configure_parser(QCommandLineParser& parser, QCommandLineOption& self_test_option,
                      QCommandLineOption& window_test_option,
                      QCommandLineOption& view_option,
                      QCommandLineOption& snapshot_option,
                      QCommandLineOption& studio_snapshot_option) -> void {
    parser.setApplicationDescription(
        QStringLiteral("Native Qt/OpenGL IDE and live viewer for agentic ICAD models."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(self_test_option);
    parser.addOption(window_test_option);
    parser.addOption(view_option);
    parser.addOption(snapshot_option);
    parser.addOption(studio_snapshot_option);
    parser.addPositionalArgument(QStringLiteral("source.icad"),
                                 QStringLiteral("One or more ICAD source files to edit and preview."),
                                 QStringLiteral("[source.icad…]"));
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
              << " scenes=" << scene.scene.scenes.size() << '\n';
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
        configure_parser(parser, self_test_option, window_test_option, view_option, snapshot_option,
                         studio_snapshot_option);
        parser.process(application);
        const auto positional = parser.positionalArguments();
        if (!requested_self_test)
            return 0;
        if (positional.size() != 1)
            parser.showHelp(2);
        return self_test(QFileInfo{positional.front()}.absoluteFilePath());
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
    configure_parser(parser, self_test_option, window_test_option, view_option, snapshot_option,
                     studio_snapshot_option);
    parser.process(application);
    const auto initial_view = standard_view(parser.value(view_option));
    if (!initial_view) {
        std::cerr << "icad-viewer: --view expects isometric, front, back, left, right, top, or bottom\n";
        return 2;
    }
    const auto positional = parser.positionalArguments();
    QString source;
    if (positional.empty()) {
        source = QFileDialog::getOpenFileName(nullptr, QStringLiteral("Open ICAD source"), QString{},
                                              QStringLiteral("ICAD source (*.icad)"));
        if (source.isEmpty())
            return 0;
    } else {
        source = QFileInfo{positional.front()}.absoluteFilePath();
    }
    for (const QString& candidate : positional) {
        if (QFileInfo{candidate}.suffix().compare(QStringLiteral("icad"), Qt::CaseInsensitive) != 0) {
            std::cerr << "icad-viewer: expected .icad source files\n";
            return 2;
        }
    }
    icad::desktop::MainWindow window{std::filesystem::path{source.toStdString()}};
    if (!window.ready()) {
        QMessageBox::critical(nullptr, QStringLiteral("ICAD Studio"), window.error());
        return 2;
    }
    for (qsizetype index = 1; index < positional.size(); ++index) {
        if (!window.open_document(
                std::filesystem::path{QFileInfo{positional[index]}.absoluteFilePath().toStdString()}))
            return 2;
    }
    window.set_standard_view(*initial_view);
    if (parser.isSet(window_test_option)) {
        const auto* tabs = window.findChild<QTabBar*>(QStringLiteral("documentTabs"));
        const auto* workspace = window.findChild<QTreeView*>(QStringLiteral("workspaceTree"));
        const auto* model = window.findChild<QTreeWidget*>(QStringLiteral("modelTree"));
        const bool has_open_folder = std::ranges::any_of(
            window.findChildren<QAction*>(), [](const QAction* action) {
                return action->text().remove(QLatin1Char('&')) == QStringLiteral("Open Folder…");
            });
        const int expected_tabs = std::max(1, static_cast<int>(positional.size()));
        if (tabs == nullptr || tabs->count() != expected_tabs ||
            tabs->height() > 40 || !tabs->isMovable() || tabs->elideMode() != Qt::ElideRight ||
            window.document_count() != static_cast<std::size_t>(expected_tabs) || workspace == nullptr ||
            workspace->model() == nullptr || window.workspace_root().empty() || model == nullptr ||
            model->selectionBehavior() != QAbstractItemView::SelectRows ||
            model->header()->sectionResizeMode(0) != QHeaderView::Stretch || !has_open_folder) {
            std::cerr << "QT_VIEWER_WINDOW_TEST failed: workspace shell is incomplete\n";
            return 3;
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
        QTimer::singleShot(1200, &application, [&window, &application, output] {
            const bool saved = window.grab().save(output, "PNG");
            if (saved)
                std::cout << "ICAD_STUDIO_SNAPSHOT " << output.toStdString() << '\n';
            else
                std::cerr << "icad-viewer: could not write Studio snapshot "
                          << output.toStdString() << '\n';
            application.exit(saved ? 0 : 3);
        });
    }
    return QApplication::exec();
}
