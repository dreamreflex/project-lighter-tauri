#pragma once
#include "config.h"
#include "runner.h"
#include "ansi.h"
#include "ports.h"
#include "batch.h"
#include <QMainWindow>
#include <QHash>
class QListWidget;
class QLineEdit;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTextEdit;
class QSpinBox;
class QCheckBox;
class QComboBox;
class Window : public QMainWindow {
    Q_OBJECT
public:
    explicit Window(QString configPath = ConfigStore::defaultPath(), QWidget *parent = nullptr);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    ConfigStore store_;
    Config config_;
    Runner runner_;
    BatchLauncher batch_;
    QHash<QString, QTextDocument *> logs_;
    QHash<QString, AnsiRenderer> renderers_;
    bool dark_ = false, loaded_ = false, closing_ = false;
    QStackedWidget *pages_, *detailStack_;
    QListWidget *projects_;
    QLineEdit *search_;
    QComboBox *filter_;
    QLabel *total_, *active_, *idle_, *title_, *directory_, *commands_, *state_, *portResult_;
    QPushButton *start_, *stop_, *edit_, *remove_, *new_, *portQuery_, *portKill_;
    QPushButton *batchButton_, *startupPortsButton_;
    QSpinBox *port_;
    QCheckBox *follow_;
    QTextEdit *terminal_;
    QTextDocument *emptyLog_;
    PortResult lastPort_;
    quint16 queriedPort_ = 0;
    QWidget *projectPage();
    QWidget *portPage();
    QWidget *settingsPage();
    QString selectedId() const;
    const Project *selected() const;
    void refreshList(const QString &select = {});
    void updateDetail();
    void updateStats();
    void load();
    bool save(const Config &config, bool stopRunning);
    void editProject(bool create);
    void removeProject();
    void startProject();
    void editStartupPorts();
    void editJson();
    void importConfig();
    void exportConfig();
    void queryPort();
    void killPort();
    void appendOutput(const QString &id, const QString &text);
    void showError(const QString &message);
    bool confirm(const QString &text);
};
