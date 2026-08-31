#include "icad_editor.hpp"

#include <QAbstractItemView>
#include <QCompleter>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStringListModel>
#include <QTextBlock>
#include <QTimer>

#include <algorithm>

namespace icad::desktop {
namespace {

[[nodiscard]] auto language_words() -> QStringList {
    return {
        QStringLiteral("REQUIRES"), QStringLiteral("CAPABILITY"), QStringLiteral("PROJECT"),
        QStringLiteral("UNITS"), QStringLiteral("PARAMETER"), QStringLiteral("ANGLE"),
        QStringLiteral("TOLERANCE"), QStringLiteral("POINT3"), QStringLiteral("VECTOR"),
        QStringLiteral("POSE"), QStringLiteral("INSTANCE"), QStringLiteral("MATERIAL"),
        QStringLiteral("PRESET"), QStringLiteral("BASE_COLOR"), QStringLiteral("METALLIC"),
        QStringLiteral("ROUGHNESS"), QStringLiteral("TEXTURE_SCALE"), QStringLiteral("UV_MODE"),
        QStringLiteral("PROFILE"), QStringLiteral("START"), QStringLiteral("POINT"),
        QStringLiteral("LINE"), QStringLiteral("ARC"), QStringLiteral("CIRCLE"),
        QStringLiteral("CLOSE"), QStringLiteral("BODY"), QStringLiteral("SKETCH"),
        QStringLiteral("SHAPE"), QStringLiteral("REGION"), QStringLiteral("OUTER"),
        QStringLiteral("HOLES"), QStringLiteral("SOLVE"), QStringLiteral("PAD"),
        QStringLiteral("POCKET"), QStringLiteral("FACE"), QStringLiteral("FEATURE"),
        QStringLiteral("TYPE"), QStringLiteral("OPERATION"), QStringLiteral("SELECT"),
        QStringLiteral("DIRECTION"), QStringLiteral("COUNT"), QStringLiteral("PLANE"),
        QStringLiteral("NORMAL"), QStringLiteral("WIDTH"), QStringLiteral("DEPTH"),
        QStringLiteral("HEIGHT"), QStringLiteral("RADIUS"), QStringLiteral("RADIUS1"),
        QStringLiteral("RADIUS2"), QStringLiteral("ORIGIN_X"), QStringLiteral("ORIGIN_Y"),
        QStringLiteral("ORIGIN_Z"), QStringLiteral("ROTATION_X"), QStringLiteral("ROTATION_Y"),
        QStringLiteral("ROTATION_Z"), QStringLiteral("BOX"), QStringLiteral("CYLINDER"),
        QStringLiteral("CONE"), QStringLiteral("SPHERE"), QStringLiteral("EXTRUDE"),
        QStringLiteral("REVOLVE"), QStringLiteral("SWEEP"), QStringLiteral("LOFT"),
        QStringLiteral("FREEFORM"), QStringLiteral("FILLET"), QStringLiteral("CHAMFER"),
        QStringLiteral("LINEAR_PATTERN"), QStringLiteral("MIRROR"), QStringLiteral("NEW"),
        QStringLiteral("UNION"), QStringLiteral("CUT"), QStringLiteral("INTERSECT"),
        QStringLiteral("INTERFACE"), QStringLiteral("CONNECT"), QStringLiteral("METHOD"),
        QStringLiteral("STANDARD"), QStringLiteral("FASTENER"), QStringLiteral("FIT"),
        QStringLiteral("CLEARANCE"), QStringLiteral("AUTO"), QStringLiteral("JOINT"),
        QStringLiteral("FIXED"), QStringLiteral("REVOLUTE"), QStringLiteral("PRISMATIC"),
        QStringLiteral("VALUE"), QStringLiteral("LIMIT"), QStringLiteral("CONSTRAINT"),
        QStringLiteral("MATE"), QStringLiteral("SCENE"), QStringLiteral("DURATION"),
        QStringLiteral("FPS"), QStringLiteral("BACKGROUND"), QStringLiteral("LIGHT"),
        QStringLiteral("EVENT"), QStringLiteral("TRACK"), QStringLiteral("KEYFRAME"),
        QStringLiteral("END"), QStringLiteral("ALUMINUM"), QStringLiteral("TITANIUM"),
        QStringLiteral("STRUCTURAL_STEEL"), QStringLiteral("CHROME"), QStringLiteral("COPPER"),
        QStringLiteral("BRASS"), QStringLiteral("CARBON_FIBER")};
}

} // namespace

IcadEditor::IcadEditor(QWidget* parent) : QPlainTextEdit{parent} {
    completion_model_ = new QStringListModel{language_words(), this};
    completer_ = new QCompleter{completion_model_, this};
    completer_->setWidget(this);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    completer_->setCompletionMode(QCompleter::PopupCompletion);
    completer_->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
    connect(completer_, qOverload<const QString&>(&QCompleter::activated), this,
            [this](const QString& value) { insert_completion(value); });

    completion_refresh_timer_ = new QTimer{this};
    completion_refresh_timer_->setSingleShot(true);
    completion_refresh_timer_->setInterval(220);
    connect(completion_refresh_timer_, &QTimer::timeout, this,
            [this] { refresh_completions(); });
    connect(this, &QPlainTextEdit::textChanged, completion_refresh_timer_,
            qOverload<>(&QTimer::start));
}

auto IcadEditor::set_vim_mode(bool enabled) -> void {
    vim_mode_ = enabled;
    vim_insert_mode_ = !enabled;
    vim_pending_delete_ = false;
    vim_pending_goto_ = false;
    update_mode_label();
}

auto IcadEditor::update_mode_label() -> void {
    if (!mode_changed)
        return;
    if (!vim_mode_)
        mode_changed(QStringLiteral("ICAD"));
    else
        mode_changed(vim_insert_mode_ ? QStringLiteral("VIM INSERT")
                                      : QStringLiteral("VIM NORMAL"));
}

auto IcadEditor::refresh_completions() -> void {
    QStringList values = language_words();
    static const QRegularExpression identifier{QStringLiteral(R"(\b[A-Za-z_][A-Za-z0-9_]*\b)")};
    auto matches = identifier.globalMatch(toPlainText());
    while (matches.hasNext())
        values.push_back(matches.next().captured());
    values.removeDuplicates();
    std::ranges::sort(values, [](const QString& first, const QString& second) {
        return QString::compare(first, second, Qt::CaseInsensitive) < 0;
    });
    completion_model_->setStringList(values);
}

auto IcadEditor::has_completion(const QString& value) const -> bool {
    return completion_model_->stringList().contains(value, Qt::CaseInsensitive);
}

auto IcadEditor::completion_prefix() const -> QString {
    auto cursor = textCursor();
    cursor.select(QTextCursor::WordUnderCursor);
    return cursor.selectedText();
}

auto IcadEditor::insert_completion(const QString& completion) -> void {
    auto cursor = textCursor();
    cursor.select(QTextCursor::WordUnderCursor);
    cursor.insertText(completion);
    setTextCursor(cursor);
}

auto IcadEditor::show_completion(bool forced) -> void {
    const QString prefix = completion_prefix();
    if (!forced && prefix.size() < 2) {
        completer_->popup()->hide();
        return;
    }
    completer_->setCompletionPrefix(prefix);
    if (completer_->completionCount() == 0) {
        completer_->popup()->hide();
        return;
    }
    auto rectangle = cursorRect();
    rectangle.setWidth(completer_->popup()->sizeHintForColumn(0) +
                       completer_->popup()->verticalScrollBar()->sizeHint().width() + 18);
    completer_->complete(rectangle);
}

auto IcadEditor::trigger_completion() -> void {
    refresh_completions();
    show_completion(true);
}

auto IcadEditor::toggle_line_comment() -> void {
    auto cursor = textCursor();
    const int anchor = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    QTextBlock first = document()->findBlock(anchor);
    QTextBlock last = document()->findBlock(std::max(anchor, end - 1));
    bool all_commented = true;
    for (auto block = first; block.isValid(); block = block.next()) {
        if (!block.text().trimmed().startsWith(QLatin1Char('#')))
            all_commented = false;
        if (block == last)
            break;
    }
    cursor.beginEditBlock();
    for (auto block = first; block.isValid(); block = block.next()) {
        QTextCursor line{block};
        line.movePosition(QTextCursor::StartOfBlock);
        if (all_commented) {
            const QString text = block.text();
            const qsizetype marker = text.indexOf(QLatin1Char('#'));
            if (marker >= 0) {
                line.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                                  static_cast<int>(marker));
                line.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor,
                                  marker + 1 < text.size() && text[marker + 1] == QLatin1Char(' ')
                                      ? 2
                                      : 1);
                line.removeSelectedText();
            }
        } else {
            line.insertText(QStringLiteral("# "));
        }
        if (block == last)
            break;
    }
    cursor.endEditBlock();
}

auto IcadEditor::duplicate_line() -> void {
    auto cursor = textCursor();
    cursor.beginEditBlock();
    const QString line = cursor.block().text();
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.insertText(QStringLiteral("\n") + line);
    cursor.endEditBlock();
    setTextCursor(cursor);
}

auto IcadEditor::move_line(int direction) -> void {
    if (direction == 0)
        return;
    auto cursor = textCursor();
    const int original_column = cursor.positionInBlock();
    const QTextBlock current = cursor.block();
    const QTextBlock target = direction < 0 ? current.previous() : current.next();
    if (!target.isValid())
        return;
    QStringList lines = toPlainText().split(QLatin1Char('\n'));
    const int current_number = current.blockNumber();
    const int target_number = target.blockNumber();
    lines.swapItemsAt(current_number, target_number);
    {
        const QSignalBlocker blocker{this};
        setPlainText(lines.join(QLatin1Char('\n')));
    }
    auto moved = textCursor();
    moved.movePosition(QTextCursor::Start);
    moved.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, target_number);
    moved.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, original_column);
    setTextCursor(moved);
    document()->setModified(true);
    emit textChanged();
}

auto IcadEditor::handle_vim_normal_key(QKeyEvent* event) -> bool {
    if (!vim_mode_ || vim_insert_mode_ || event->modifiers() != Qt::NoModifier)
        return false;
    auto cursor = textCursor();
    const int key = event->key();
    if (vim_pending_delete_) {
        vim_pending_delete_ = false;
        if (key == Qt::Key_D) {
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            if (!cursor.block().next().isValid())
                cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 1);
            else
                cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
            cursor.removeSelectedText();
            return true;
        }
    }
    if (vim_pending_goto_) {
        vim_pending_goto_ = false;
        if (key == Qt::Key_G) {
            cursor.movePosition(QTextCursor::Start);
            setTextCursor(cursor);
            return true;
        }
    }
    switch (key) {
    case Qt::Key_I:
        vim_insert_mode_ = true;
        update_mode_label();
        return true;
    case Qt::Key_A:
        cursor.movePosition(QTextCursor::Right);
        setTextCursor(cursor);
        vim_insert_mode_ = true;
        update_mode_label();
        return true;
    case Qt::Key_H: cursor.movePosition(QTextCursor::Left); break;
    case Qt::Key_J: cursor.movePosition(QTextCursor::Down); break;
    case Qt::Key_K: cursor.movePosition(QTextCursor::Up); break;
    case Qt::Key_L: cursor.movePosition(QTextCursor::Right); break;
    case Qt::Key_0: cursor.movePosition(QTextCursor::StartOfBlock); break;
    case Qt::Key_Dollar: cursor.movePosition(QTextCursor::EndOfBlock); break;
    case Qt::Key_W: cursor.movePosition(QTextCursor::NextWord); break;
    case Qt::Key_B: cursor.movePosition(QTextCursor::PreviousWord); break;
    case Qt::Key_G:
        if (event->text() == QStringLiteral("G"))
            cursor.movePosition(QTextCursor::End);
        else
            vim_pending_goto_ = true;
        break;
    case Qt::Key_D:
        vim_pending_delete_ = true;
        return true;
    case Qt::Key_X:
        cursor.deleteChar();
        return true;
    case Qt::Key_O:
        if (event->text() == QStringLiteral("O")) {
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.insertText(QStringLiteral("\n"));
            cursor.movePosition(QTextCursor::Up);
        } else {
            cursor.movePosition(QTextCursor::EndOfBlock);
            cursor.insertText(QStringLiteral("\n"));
        }
        setTextCursor(cursor);
        vim_insert_mode_ = true;
        update_mode_label();
        return true;
    default: return true;
    }
    setTextCursor(cursor);
    return true;
}

auto IcadEditor::keyPressEvent(QKeyEvent* event) -> void {
    if (completer_->popup()->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            event->ignore();
            return;
        default: break;
        }
    }
    if (event->key() == Qt::Key_Space &&
        event->modifiers().testFlag(Qt::ControlModifier)) {
        trigger_completion();
        return;
    }
    if (event->key() == Qt::Key_Escape && vim_mode_) {
        completer_->popup()->hide();
        vim_insert_mode_ = false;
        vim_pending_delete_ = false;
        vim_pending_goto_ = false;
        auto cursor = textCursor();
        cursor.clearSelection();
        setTextCursor(cursor);
        update_mode_label();
        return;
    }
    if (handle_vim_normal_key(event))
        return;
    QPlainTextEdit::keyPressEvent(event);
    if (!event->text().isEmpty() && !event->text().front().isSpace())
        show_completion(false);
    else if (event->key() != Qt::Key_Backspace)
        completer_->popup()->hide();
}

} // namespace icad::desktop
