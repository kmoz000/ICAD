#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <vector>

namespace icad::desktop {

class IcadHighlighter final : public QSyntaxHighlighter {
  public:
    explicit IcadHighlighter(QTextDocument* document);

  protected:
    auto highlightBlock(const QString& text) -> void override;

  private:
    struct Rule {
        QRegularExpression expression;
        QTextCharFormat format;
        int capture{0};
    };
    std::vector<Rule> rules_;
};

} // namespace icad::desktop
