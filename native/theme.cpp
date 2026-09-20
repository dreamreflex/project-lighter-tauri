#include "theme.h"
#include <QApplication>
#include <QPalette>
#include <QFontDatabase>
void applyTheme(QApplication &application, bool dark) {
    application.setStyle("Fusion");
    auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
#ifdef Q_OS_WIN
    font.setFamilies({"Segoe UI", "Microsoft YaHei UI"});
#else
    font.setFamilies({"Noto Sans", "Noto Sans CJK SC", "sans-serif"});
#endif
    application.setFont(font);
    const QString bg = dark ? "#101620" : "#f5f7fb";
    const QString panel = dark ? "#18212e" : "#ffffff";
    const QString text = dark ? "#e8edf5" : "#202c40";
    const QString muted = dark ? "#94a4ba" : "#718096";
    const QString border = dark ? "#2c394b" : "#e2e8f0";
    const QString hover = dark ? "#26374a" : "#edf3fa";
    const QString selected = dark ? "#1b4146" : "#e4f4ef";
    const QString accent = dark ? "#73dabc" : "#188571";
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(bg)); palette.setColor(QPalette::WindowText, QColor(text));
    palette.setColor(QPalette::Base, QColor(panel)); palette.setColor(QPalette::AlternateBase, QColor(bg));
    palette.setColor(QPalette::Text, QColor(text)); palette.setColor(QPalette::Button, QColor(panel));
    palette.setColor(QPalette::ButtonText, QColor(text)); palette.setColor(QPalette::Highlight, QColor("#188571"));
    palette.setColor(QPalette::HighlightedText, Qt::white); palette.setColor(QPalette::PlaceholderText, QColor(muted));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(muted)); palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(muted));
    application.setPalette(palette);
    application.setStyleSheet(QString(R"(
        QWidget { color: %3; font-size: 14px; }
        QMainWindow, QDialog { background: %1; }
        QLabel { background: transparent; }
        QFrame#sidebar { background: %2; border-right: 1px solid %5; }
        QLabel#brand { font-size: 17px; font-weight: 600; }
        QLabel#pageTitle { font-size: 18px; font-weight: 600; }
        QLabel#sectionTitle { font-size: 17px; font-weight: 600; }
        QLabel#muted { color: %4; }
        QLabel#error { color: #e16c67; }
        QLabel#badge { color: %8; background: %7; border-radius: 10px; padding: 6px 12px; }
        QFrame#panel { background: %2; border: 1px solid %5; border-radius: 6px; }
        QPushButton { background: %2; border: 1px solid %5; border-radius: 7px; padding: 8px 14px; min-height: 20px; }
        QPushButton:hover { background: %6; border-color: #a3b7c7; }
        QPushButton:pressed { background: %7; }
        QPushButton:focus, QLineEdit:focus, QPlainTextEdit:focus, QTableWidget:focus { border: 1px solid #188571; }
        QPushButton:disabled { color: %4; background: %1; }
        QPushButton#primary, QPushButton[primary="true"] { color: white; background: #188571; border-color: #188571; font-weight: 600; }
        QPushButton#primary:hover, QPushButton[primary="true"]:hover { background: #116e5d; }
        QPushButton#primary:disabled, QPushButton[primary="true"]:disabled { background: %5; color: %4; border-color: %5; }
        QPushButton#danger { color: #d55d58; }
        QPushButton#danger:disabled { color: %4; }
        QComboBox::drop-down { border: none; width: 22px; }
        QPushButton#nav { text-align: left; padding: 7px 12px; border: none; background: transparent; color: %4; }
        QPushButton#nav:checked { color: %8; background: %7; font-weight: 600; }
        QPushButton#nav:hover { background: %6; }
        QLineEdit, QSpinBox, QComboBox { background: %2; border: 1px solid %5; border-radius: 7px; padding: 9px 12px; selection-background-color: #188571; }
        QPlainTextEdit, QTableWidget { background: %2; border: 1px solid %5; border-radius: 8px; padding: 8px; gridline-color: %5; }
        QListWidget { background: %2; border: 1px solid %5; border-radius: 6px; padding: 4px; outline: none; }
        QListWidget::item { border-radius: 7px; padding: 8px 10px; margin: 1px 0; }
        QListWidget::item:selected { background: %7; color: %8; }
        QListWidget::item:hover:!selected { background: %6; }
        QHeaderView::section { background: %1; color: %4; border: none; padding: 10px; }
        QTextEdit#terminal { background: #111a27; color: #d4dfed; border: none; border-radius: 9px; padding: 15px; selection-background-color: #365778; }
        QSplitter::handle { background: transparent; width: 12px; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
        QScrollBar::handle:vertical { background: %5; border-radius: 5px; min-height: 24px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QStatusBar { background: %2; color: %4; border-top: 1px solid %5; }
        QToolTip { background: %2; color: %3; border: 1px solid %5; padding: 5px; }
    )").arg(bg, panel, text, muted, border, hover, selected, accent));
}
