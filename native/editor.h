#pragma once
#include "config.h"
#include <QDialog>
class QLineEdit;
class QTableWidget;
class QLabel;
class QComboBox;
class QPlainTextEdit;
class ProjectEditor : public QDialog {
    Q_OBJECT
public:
    explicit ProjectEditor(const Project &project, QWidget *parent = nullptr);
    Project project() const;
private:
    Project original_;
    QLineEdit *name_, *directory_;
    QTableWidget *commands_;
    QComboBox *type_;
    QPlainTextEdit *script_;
    QLabel *error_;
    void addCommand(const Command &command = {});
    void validate();
};
