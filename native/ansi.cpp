#include "ansi.h"
#include <QTextCursor>
#include <QStringList>
static QColor ansiColor(int index) {
    static const char *colors[] = {"#19222e", "#f07178", "#9bd49c", "#f0cb80", "#82aaff", "#c792ea", "#89ddff", "#d8e2ee", "#69798c", "#ff8b92", "#b2e3ac", "#ffe29b", "#a6c3ff", "#dab3ff", "#b2f0ff", "#ffffff"};
    if (index < 16) return QColor(colors[qBound(0, index, 15)]);
    if (index >= 232) return QColor(8 + (index - 232) * 10, 8 + (index - 232) * 10, 8 + (index - 232) * 10);
    index -= 16;
    auto component = [](int n) { return n == 0 ? 0 : 55 + n * 40; };
    return QColor(component(index / 36), component(index / 6 % 6), component(index % 6));
}
void AnsiRenderer::reset() { pending_.clear(); format_ = QTextCharFormat(); }
void AnsiRenderer::sgr(const QString &parameters) {
    const auto values = parameters.split(';');
    for (int i = 0; i < values.size(); ++i) {
        const int n = values[i].toInt();
        if (n == 0) format_ = QTextCharFormat();
        else if (n == 1) format_.setFontWeight(QFont::Bold);
        else if (n == 2) format_.setForeground(QColor("#7d899a"));
        else if (n == 3) format_.setFontItalic(true);
        else if (n == 4) format_.setFontUnderline(true);
        else if (n == 22) format_.setFontWeight(QFont::Normal);
        else if (n == 23) format_.setFontItalic(false);
        else if (n == 24) format_.setFontUnderline(false);
        else if (n == 39) format_.clearForeground();
        else if (n == 49) format_.clearBackground();
        else if ((n >= 30 && n <= 37) || (n >= 90 && n <= 97)) format_.setForeground(ansiColor(n >= 90 ? n - 90 + 8 : n - 30));
        else if ((n >= 40 && n <= 47) || (n >= 100 && n <= 107)) format_.setBackground(ansiColor(n >= 100 ? n - 100 + 8 : n - 40));
        else if ((n == 38 || n == 48) && i + 2 < values.size()) {
            QColor color;
            if (values[i + 1] == "5") { color = ansiColor(qBound(0, values[i + 2].toInt(), 255)); i += 2; }
            else if (values[i + 1] == "2" && i + 4 < values.size()) {
                color = QColor(qBound(0, values[i + 2].toInt(), 255), qBound(0, values[i + 3].toInt(), 255), qBound(0, values[i + 4].toInt(), 255)); i += 4;
            }
            if (color.isValid()) { if (n == 38) format_.setForeground(color); else format_.setBackground(color); }
        }
    }
}
void AnsiRenderer::append(QTextDocument *document, const QString &text) {
    pending_ += text;
    QTextCursor cursor(document);
    cursor.movePosition(QTextCursor::End);
    int i = 0;
    while (i < pending_.size()) {
        const auto c = pending_[i];
        if (c == QChar(27) || c == QChar(0x9b)) {
            const bool csi = c == QChar(0x9b);
            if (!csi && i + 1 >= pending_.size()) break;
            if (csi || pending_[i + 1] == '[') {
                const int start = i + (csi ? 1 : 2);
                int end = start;
                while (end < pending_.size() && !(pending_[end].unicode() >= 0x40 && pending_[end].unicode() <= 0x7e)) ++end;
                if (end == pending_.size()) break;
                if (pending_[end] == 'm') sgr(pending_.mid(start, end - start));
                i = end + 1; continue;
            }
            if (pending_[i + 1] == ']') {
                int end = i + 2;
                while (end < pending_.size() && pending_[end] != QChar(7) && !(pending_[end] == QChar(27) && end + 1 < pending_.size() && pending_[end + 1] == '\\')) ++end;
                if (end == pending_.size()) break;
                i = end + (pending_[end] == QChar(7) ? 1 : 2); continue;
            }
            i += 2; continue;
        }
        if (c == '\r') {
            if (i + 1 == pending_.size()) break;
            if (pending_[i + 1] != '\n') {
                cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
                cursor.removeSelectedText();
            }
            ++i; continue;
        }
        int end = i + 1;
        while (end < pending_.size() && pending_[end] != QChar(27) && pending_[end] != QChar(0x9b) && pending_[end] != '\r') ++end;
        cursor.insertText(pending_.mid(i, end - i), format_);
        i = end;
    }
    pending_.remove(0, i);
    if (pending_.size() > 8192) pending_.clear();
    // Bound both line count and a maliciously long single line.
    if (document->characterCount() > 1000000) {
        QTextCursor trim(document); trim.setPosition(document->characterCount() - 750000, QTextCursor::KeepAnchor); trim.removeSelectedText();
    }
}
