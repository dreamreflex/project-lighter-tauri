#pragma once
#include <QTextCharFormat>
#include <QTextDocument>
class AnsiRenderer {
public:
    void append(QTextDocument *document, const QString &text);
    void reset();
private:
    QString pending_;
    QTextCharFormat format_;
    void sgr(const QString &parameters);
};
