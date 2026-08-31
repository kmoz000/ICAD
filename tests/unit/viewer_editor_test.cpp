#include "icad_editor.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QTextCursor>

#include <iostream>
#include <string_view>

namespace {

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

auto main(int argc, char** argv) -> int {
    QApplication application{argc, argv};
    icad::desktop::IcadEditor editor;
    editor.setPlainText(QStringLiteral("PROJECT turbine\nBODY compressor\nEND"));
    editor.refresh_completions();
    if (!editor.has_completion(QStringLiteral("PROJECT")) ||
        !editor.has_completion(QStringLiteral("compressor")))
        return fail("ICAD keywords and document identifiers were not offered as completions");

    editor.moveCursor(QTextCursor::Start);
    editor.toggle_line_comment();
    if (!editor.toPlainText().startsWith(QStringLiteral("# PROJECT turbine")))
        return fail("toggle comment did not comment the active line");
    editor.toggle_line_comment();
    if (!editor.toPlainText().startsWith(QStringLiteral("PROJECT turbine")))
        return fail("toggle comment did not restore the active line");

    editor.duplicate_line();
    if (editor.toPlainText().count(QStringLiteral("PROJECT turbine")) != 2)
        return fail("duplicate line did not create a second exact line");
    editor.move_line(1);
    if (editor.toPlainText() !=
        QStringLiteral("PROJECT turbine\nBODY compressor\nPROJECT turbine\nEND"))
        return fail("move line did not move the duplicated line down by one row");

    editor.set_vim_mode(true);
    QKeyEvent insert{QEvent::KeyPress, Qt::Key_I, Qt::NoModifier, QStringLiteral("i")};
    QApplication::sendEvent(&editor, &insert);
    if (!editor.vim_insert_mode())
        return fail("Vim i command did not enter insert mode");
    QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
    QApplication::sendEvent(&editor, &escape);
    if (editor.vim_insert_mode())
        return fail("Vim escape did not return to normal mode");
    return 0;
}
