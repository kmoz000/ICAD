#include "main_window.hpp"
#include "scene_model.hpp"

#include "icad/engine/session.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFileDialog>
#include <QIcon>
#include <QMessageBox>
#include <QSurfaceFormat>

#include <filesystem>
#include <iostream>
#include <algorithm>
#include <ranges>
#include <span>
#include <string_view>

namespace {

auto configure_parser(QCommandLineParser& parser, QCommandLineOption& self_test_option) -> void {
    parser.setApplicationDescription(
        QStringLiteral("Native Qt/OpenGL IDE and live viewer for agentic ICAD models."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(self_test_option);
    parser.addPositionalArgument(QStringLiteral("source.icad"),
                                 QStringLiteral("ICAD source file to edit and preview."));
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
    if (requested_core_only) {
        QCoreApplication application{argc, argv};
        configure_application_metadata();
        QCommandLineParser parser;
        configure_parser(parser, self_test_option);
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
    configure_parser(parser, self_test_option);
    parser.process(application);
    const auto positional = parser.positionalArguments();
    if (positional.size() > 1)
        parser.showHelp(2);
    QString source;
    if (positional.empty()) {
        source = QFileDialog::getOpenFileName(nullptr, QStringLiteral("Open ICAD source"), QString{},
                                              QStringLiteral("ICAD source (*.icad)"));
        if (source.isEmpty())
            return 0;
    } else {
        source = QFileInfo{positional.front()}.absoluteFilePath();
    }
    if (QFileInfo{source}.suffix().compare(QStringLiteral("icad"), Qt::CaseInsensitive) != 0) {
        std::cerr << "icad-viewer: expected an .icad source file\n";
        return 2;
    }
    icad::desktop::MainWindow window{std::filesystem::path{source.toStdString()}};
    if (!window.ready()) {
        QMessageBox::critical(nullptr, QStringLiteral("ICAD Studio"), window.error());
        return 2;
    }
    window.show();
    return QApplication::exec();
}
