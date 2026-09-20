#include "config.h"
#include "runner.h"
#include "ansi.h"
#include "ports.h"
#include "window.h"
#include "editor.h"
#include "batch.h"
#include <QComboBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QtTest>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QFile>
#include <QListWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#ifndef Q_OS_WIN
#include <signal.h>
#endif
class LighterTest : public QObject {
    Q_OBJECT
private slots:
    void legacyAndRoundtrip();
    void invalidConfig();
    void atomicSaveAndBackup();
    void ansiChunks();
    void commandSequence();
    void failureStopsSequence();
    void invalidDirectory();
    void stopTree();
    void startingCancellation();
    void queryPort();
    void editorAndWindow();
    void terminatePortOwner();
    void windowCloseStopsProjects();
    void scriptConfigAndEditor();
    void batchRunsEveryProjectOnce();
    void batchPortPreflight();
    void batchStopsAll();
    void windowBatchControls();
};
static Project project(QString command, QString cwd = {}) { return {"test", QStringLiteral("测试项目"), cwd, {{"step", command}}, {}, "command", {}}; }
void LighterTest::legacyAndRoundtrip() {
    const auto config = Config::parse(R"({"custom":42,"projects":[{"id":"a","name":"旧项目","command":"echo hello","workingDir":"","custom":true},{"id":"b","name":"新项目","commands":[{"name":"准备","command":"echo ready"},{"command":"echo done"}]}]})");
    QCOMPARE(config.projects.size(), 2); QCOMPARE(config.projects[0].commands[0].command, "echo hello");
    const auto loaded = Config::parse(config.json()); QCOMPARE(loaded.projects[1].commands.size(), 2); QCOMPARE(loaded.extra["custom"].toInt(), 42); QVERIFY(loaded.projects[0].extra["custom"].toBool());
}
void LighterTest::invalidConfig() {
    for (const auto &text : {"{", "[]", "{}", R"({"projects":[{"id":"a","name":"x","commands":[]}]})", R"({"projects":[{"id":"a","name":"x","command":"ok"},{"id":"a","name":"y","command":"ok"}]})", R"({"projects":[{"id":"a","name":"x","commands":"oops","command":"ok"}]})", R"({"projects":[{"id":"a","name":"x","workingDir":2,"command":"ok"}]})"}) {
        QVERIFY_EXCEPTION_THROWN(Config::parse(text), std::runtime_error);
    }
}
void LighterTest::atomicSaveAndBackup() {
    QTemporaryDir directory; ConfigStore store(directory.filePath("nested/config.json"));
    QCOMPARE(store.load().projects.size(), 0);
    const QByteArray legacy = R"({"projects":[{"id":"a","name":"legacy","command":"echo old"}]})";
    ConfigStore::write(store.path(), legacy);
    Config config; config.projects.append(project("echo new")); store.save(config);
    QFile backup(store.path() + ".pre-qt.bak"); QVERIFY(backup.open(QIODevice::ReadOnly)); QCOMPARE(backup.readAll(), legacy);
    QCOMPARE(store.load().projects[0].commands[0].command, "echo new");
    auto bad = config; bad.projects[0].commands.clear(); QVERIFY_EXCEPTION_THROWN(store.save(bad), std::runtime_error);
    QCOMPARE(store.load().projects[0].commands[0].command, "echo new");
}
void LighterTest::ansiChunks() {
    QTextDocument doc; AnsiRenderer ansi;
    ansi.append(&doc, "<tag>\x1b["); ansi.append(&doc, "31m红色\x1b[0m\n");
    QCOMPARE(doc.toPlainText(), QStringLiteral("<tag>红色\n"));
    QTextCursor cursor(&doc); cursor.setPosition(5); cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor); QCOMPARE(cursor.charFormat().foreground().color(), QColor("#f07178"));
    ansi.append(&doc, "\x1b]0;hidden"); ansi.append(&doc, "\aok"); QVERIFY(doc.toPlainText().endsWith("ok"));
    ansi.append(&doc, "\rreplace\n"); QVERIFY(doc.toPlainText().endsWith("replace\n"));
    ansi.append(&doc, QString(1100000, 'x')); QVERIFY(doc.characterCount() <= 1000000);
}
void LighterTest::commandSequence() {
    QTemporaryDir dir; Runner runner; QSignalSpy done(&runner, &Runner::finished); QString output;
    connect(&runner, &Runner::output, this, [&](const QString &, const QString &text) { output += text; });
#ifdef Q_OS_WIN
    auto p = project("$env:LIGHTER_TEST = '共享'; Write-Output '中文'", dir.path());
    p.commands.append(Command{"second", "Write-Output $env:LIGHTER_TEST; [Console]::Error.WriteLine('stderr'); Set-Content -Path result.txt -Value done"});
#else
    auto p = project("export LIGHTER_TEST='共享'; printf '中文\\n'", dir.path());
    p.commands.append(Command{"second", "printf '%s\\n' \"$LIGHTER_TEST\"; printf 'stderr\\n' >&2; printf done > result.txt"});
#endif
    runner.start(p); runner.start(p); QCOMPARE(runner.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000); QCOMPARE(done[0][1].toInt(), 0); QCOMPARE(runner.count(), 0);
    output.replace("\r\n", "\n"); // PowerShell writes CRLF; POSIX shells write LF.
    QVERIFY(QFile::exists(dir.filePath("result.txt"))); QVERIFY(output.contains(QStringLiteral("\n中文\n"))); QVERIFY(output.contains(QStringLiteral("\n共享\n"))); QVERIFY(output.contains("\nstderr\n"));
    runner.start(p); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 15000);
}
void LighterTest::failureStopsSequence() {
    QTemporaryDir dir; Runner runner; QSignalSpy done(&runner, &Runner::finished);
#ifdef Q_OS_WIN
    auto p = project("cmd /c exit 7", dir.path()); p.commands.append(Command{"skip", "Set-Content skipped.txt bad"});
#else
    auto p = project("false", dir.path()); p.commands.append(Command{"skip", "touch skipped.txt"});
#endif
    runner.start(p); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000); QVERIFY(done[0][1].toInt() != 0); QVERIFY(!QFile::exists(dir.filePath("skipped.txt")));
}
void LighterTest::invalidDirectory() {
    QTemporaryDir dir; Runner runner; QSignalSpy errors(&runner, &Runner::error);
    runner.start(project("echo test", dir.filePath("absent"))); QCOMPARE(errors.count(), 1); QCOMPARE(runner.count(), 0);
}
void LighterTest::stopTree() {
    QTemporaryDir dir; Runner runner; QSignalSpy done(&runner, &Runner::finished);
#ifdef Q_OS_WIN
    auto p = project("$p = Start-Process powershell.exe -ArgumentList '-NoProfile', '-Command', 'Start-Sleep 2; Set-Content escaped.txt bad' -WorkingDirectory (Get-Location).Path -PassThru; Set-Content child.pid $p.Id; Start-Sleep 30", dir.path());
#else
    auto p = project("(sleep 2; touch escaped.txt) & echo $! > child.pid; wait", dir.path());
#endif
    runner.start(p);
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(dir.filePath("child.pid")), 10000);
    runner.stop(p.id); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000); QTest::qWait(2200); QVERIFY(!QFile::exists(dir.filePath("escaped.txt"))); QCOMPARE(runner.count(), 0);
}
void LighterTest::startingCancellation() {
    Runner runner; QSignalSpy done(&runner, &Runner::finished);
#ifdef Q_OS_WIN
    runner.start(project("Start-Sleep 30"));
#else
    runner.start(project("sleep 30"));
#endif
    runner.stopAll(); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000); QCOMPARE(runner.count(), 0);
}
void LighterTest::queryPort() {
    QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost, 0)); const auto port = server.serverPort();
    const auto result = Ports::query(port); QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QVERIFY(result.occupied);
    bool found = false; for (const auto &owner : result.owners) if (owner.pid == QCoreApplication::applicationPid()) found = true; QVERIFY(found);
    QVERIFY(!Ports::terminate(port, {{9999999, "stale"}}).isEmpty());
    QVERIFY(!Ports::terminate(port, result.owners).isEmpty()); // never kill this test runner
    server.close(); const auto empty = Ports::query(port); QVERIFY(empty.error.isEmpty()); QVERIFY(!empty.occupied);
}
void LighterTest::editorAndWindow() {
    ProjectEditor editor(project("echo test")); editor.show();
    auto *name = editor.findChild<QLineEdit *>("projectNameInput"); QVERIFY(name); name->setText(QStringLiteral("更新项目"));
    QCOMPARE(editor.project().name, QStringLiteral("更新项目"));
    QTemporaryDir dir; ConfigStore store(dir.filePath("config.json")); Config config;
    auto frontend = project("echo 'Ready on localhost:5173'"); frontend.id = "web"; frontend.name = QStringLiteral("前端工作台"); frontend.workingDir = dir.path();
    config.projects.append(frontend);
    auto api = project("echo 'API ready'"); api.id = "api"; api.name = QStringLiteral("API 服务"); config.projects.append(api);
    auto docs = project("echo 'Documentation ready'"); docs.id = "docs"; docs.name = QStringLiteral("文档站点"); config.projects.append(docs); store.save(config);
    Window window(store.path()); window.show(); auto *list = window.findChild<QListWidget *>("projectList"); QVERIFY(list);
    QTRY_COMPARE(list->count(), 3); auto *search = window.findChild<QLineEdit *>("projectSearch"); search->setText("API"); QCOMPARE(list->count(), 1); search->clear(); QCOMPARE(list->count(), 3);
    QPushButton *start = nullptr; for (auto *b : window.findChildren<QPushButton *>()) if (b->text().contains(QStringLiteral("▶ 启动"))) start = b; QVERIFY(start);
    start->click(); auto *terminal = window.findChild<QTextEdit *>("terminal"); QTRY_VERIFY_WITH_TIMEOUT(terminal->toPlainText().contains(QStringLiteral("进程已退出")), 15000);
    if (qEnvironmentVariableIsSet("LIGHTER_SCREENSHOT")) { window.resize(1240, 820); QTest::qWait(200); QVERIFY(window.grab().save(qEnvironmentVariable("LIGHTER_SCREENSHOT")));
        for (auto *b : window.findChildren<QPushButton *>()) if (b->text().contains(QStringLiteral("切换明暗主题"))) b->click();
        QVERIFY(window.grab().save(qEnvironmentVariable("LIGHTER_SCREENSHOT") + ".dark.png"));
        window.resize(920, 660); QTest::qWait(100); QVERIFY(terminal->height() >= 120); QVERIFY(window.grab().save(qEnvironmentVariable("LIGHTER_SCREENSHOT") + ".compact.png")); }
    window.close();
}
void LighterTest::terminatePortOwner() {
    QTemporaryDir dir; const auto portFile = dir.filePath("port.txt");
    QProcess child; child.start(QCoreApplication::applicationFilePath(), {"--listen", portFile});
    QVERIFY(child.waitForStarted(5000)); QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(portFile), 10000);
    QFile file(portFile); QVERIFY(file.open(QIODevice::ReadOnly)); const auto port = file.readAll().toUShort(); QVERIFY(port > 0);
    const auto owners = Ports::query(port); QVERIFY2(owners.error.isEmpty(), qPrintable(owners.error)); QVERIFY(!owners.owners.isEmpty());
    QCOMPARE(owners.owners[0].pid, child.processId());
    const auto error = Ports::terminate(port, owners.owners); QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(child.waitForFinished(5000)); QVERIFY(!Ports::query(port).occupied);
}
void LighterTest::windowCloseStopsProjects() {
    QTemporaryDir dir; ConfigStore store(dir.filePath("config.json")); Config config;
#ifdef Q_OS_WIN
    config.projects.append(project("Start-Sleep 30", dir.path()));
#else
    config.projects.append(project("sleep 30", dir.path()));
#endif
    store.save(config); Window window(store.path()); window.show();
    auto *list = window.findChild<QListWidget *>("projectList"); QTRY_COMPARE(list->count(), 1);
    for (auto *b : window.findChildren<QPushButton *>()) if (b->text().contains(QStringLiteral("▶ 启动"))) b->click();
    auto answer = [](QMessageBox::StandardButton value) {
        QTimer::singleShot(50, [value] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(value)->click(); });
    };
    answer(QMessageBox::No); window.close(); QVERIFY(window.isVisible());
    answer(QMessageBox::Yes); window.close(); QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 10000);
}
void LighterTest::scriptConfigAndEditor() {
    const auto config = Config::parse(R"({"startupPorts":[3000,8080,3000],"projects":[{"id":"s","name":"脚本","type":"script","script":"echo first\necho second"}]})");
    QCOMPARE(config.startupPorts, QList<quint16>({3000, 8080}));
    QCOMPARE(config.projects[0].executionCommands().size(), 1);
    QCOMPARE(Config::parse(config.json()).projects[0].script, QString("echo first\necho second"));
    for (const auto &json : {R"({"startupPorts":[0],"projects":[]})", R"({"startupPorts":[65536],"projects":[]})", R"({"startupPorts":[3.5],"projects":[]})", R"({"startupPorts":["80"],"projects":[]})", R"({"projects":[{"id":"s","name":"s","type":"script","script":" "}]})", R"({"projects":[{"id":"s","name":"s","type":"unknown","command":"ok"}]})"})
        QVERIFY_EXCEPTION_THROWN(Config::parse(json), std::runtime_error);
    ProjectEditor editor(config.projects[0]); editor.show();
    auto *type = editor.findChild<QComboBox *>("projectTypeInput"); auto *script = editor.findChild<QPlainTextEdit *>("scriptInput");
    QVERIFY(type); QVERIFY(script); QCOMPARE(type->currentData().toString(), "script"); QVERIFY(script->isVisible());
    script->setPlainText("echo new\necho multiline"); QCOMPARE(editor.project().script, script->toPlainText());
    type->setCurrentIndex(0); QVERIFY(script->isHidden()); type->setCurrentIndex(1); QCOMPARE(editor.project().script, "echo new\necho multiline");
}
void LighterTest::batchRunsEveryProjectOnce() {
    QTemporaryDir dir; Runner runner; BatchLauncher batch(runner); QSignalSpy done(&runner, &Runner::finished); QSignalSpy errors(&batch, &BatchLauncher::error);
    Config config; auto command = project("echo command", dir.path()); config.projects.append(command);
    auto script = project({}, dir.path()); script.id = "script"; script.type = "script"; script.commands.clear();
#ifdef Q_OS_WIN
    script.script = "$value = '中文'\nAdd-Content -Path count.txt -Value $value\nWrite-Output $value";
#else
    script.script = "value='中文'\nprintf '%s\\n' \"$value\" >> count.txt\nprintf '%s\\n' \"$value\"";
#endif
    config.projects.append(script); batch.start(config); batch.start(config);
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 15000); QCOMPARE(errors.count(), 0); QVERIFY(!batch.busy()); QCOMPARE(runner.count(), 0);
    for (const auto &event : done) QCOMPARE(event[1].toInt(), 0);
    QFile file(dir.filePath("count.txt")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto once = file.readAll(); QCOMPARE(once.count('\n'), 1); file.close();
    batch.start(config); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 4, 15000);
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll().count('\n'), 2);
}
void LighterTest::batchPortPreflight() {
    QTemporaryDir dir; const auto portFile = dir.filePath("port.txt");
    QProcess child; child.start(QCoreApplication::applicationFilePath(), {"--listen", portFile});
    QVERIFY(child.waitForStarted(5000)); QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(portFile), 10000);
    QFile file(portFile); QVERIFY(file.open(QIODevice::ReadOnly)); const auto port = file.readAll().toUShort(); QVERIFY(port);
    Runner runner; BatchLauncher batch(runner); QSignalSpy confirm(&batch, &BatchLauncher::confirmationNeeded); QSignalSpy started(&batch, &BatchLauncher::projectStarting); QSignalSpy errors(&batch, &BatchLauncher::error); QSignalSpy done(&runner, &Runner::finished);
    Config config; config.projects.append(project("echo ready", dir.path())); config.startupPorts = {port};
    batch.start(config); batch.stop(); QTRY_VERIFY_WITH_TIMEOUT(!batch.busy(), 15000); QCOMPARE(started.count(), 0); QCOMPARE(confirm.count(), 0); QVERIFY(Ports::query(port).occupied);
    batch.start(config); QTRY_COMPARE_WITH_TIMEOUT(confirm.count(), 1, 15000); QCOMPARE(started.count(), 0); batch.confirmPorts(false); QVERIFY(!batch.busy()); QVERIFY(Ports::query(port).occupied);
    batch.start(config); QTRY_COMPARE_WITH_TIMEOUT(confirm.count(), 2, 15000); batch.confirmPorts(true);
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000); QCOMPARE(errors.count(), 0); QCOMPARE(started.count(), 1); QVERIFY(!Ports::query(port).occupied);
    if (child.state() != QProcess::NotRunning) QVERIFY(child.waitForFinished(5000));
    // An inaccessible/protected owner must leave every project unstarted.
    QTcpServer own; QVERIFY(own.listen(QHostAddress::LocalHost, 0)); config.startupPorts = {own.serverPort()};
    batch.start(config); QTRY_COMPARE_WITH_TIMEOUT(confirm.count(), 3, 15000); batch.confirmPorts(true);
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 15000); QCOMPARE(started.count(), 1); QCOMPARE(runner.count(), 0); QVERIFY(!batch.busy());
}
void LighterTest::batchStopsAll() {
    Runner runner; BatchLauncher batch(runner); QSignalSpy done(&runner, &Runner::finished); Config config;
#ifdef Q_OS_WIN
    auto first = project("Start-Sleep 30");
#else
    auto first = project("sleep 30");
#endif
    config.projects.append(first); first.id = "second"; config.projects.append(first);
    batch.start(config); QTRY_COMPARE_WITH_TIMEOUT(runner.count(), 2, 10000); batch.stop();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 10000); QCOMPARE(runner.count(), 0); QVERIFY(!batch.busy());
}
void LighterTest::windowBatchControls() {
    QTemporaryDir dir; ConfigStore store(dir.filePath("config.json")); Config config;
#ifdef Q_OS_WIN
    config.projects.append(project("Write-Output ready; Start-Sleep 30", dir.path()));
#else
    config.projects.append(project("echo ready; sleep 30", dir.path()));
#endif
    store.save(config); Window window(store.path()); window.show();
    auto *list = window.findChild<QListWidget *>("projectList"); QTRY_COMPARE(list->count(), 1);
    auto *batch = window.findChild<QPushButton *>("batchButton"); QVERIFY(batch); QCOMPARE(batch->text(), QStringLiteral("一键启动")); batch->click();
    QCOMPARE(batch->text(), QStringLiteral("一键关闭"));
    auto *terminal = window.findChild<QTextEdit *>("terminal"); QTRY_VERIFY_WITH_TIMEOUT(terminal->toPlainText().contains("ready"), 15000);
    batch->click(); QTRY_COMPARE_WITH_TIMEOUT(batch->text(), QStringLiteral("一键启动"), 10000);
    QTimer::singleShot(50, [] {
        auto *dialog = QApplication::activeModalWidget(); if (!dialog) return;
        auto *port = dialog->findChild<QSpinBox *>(); if (!port) return; port->setValue(3300);
        const auto buttons = dialog->findChildren<QPushButton *>();
        for (auto *button : buttons) if (button->text() == QStringLiteral("添加")) { button->click(); button->click(); }
        for (auto *button : buttons) if (button->text() == QStringLiteral("保存")) button->click();
    });
    window.findChild<QPushButton *>("startupPortsButton")->click();
    QCOMPARE(store.load().startupPorts, QList<quint16>({3300})); window.close();
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (app.arguments().size() == 3 && app.arguments()[1] == "--listen") {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) return 2;
        ConfigStore::write(app.arguments()[2], QByteArray::number(server.serverPort()));
        return app.exec();
    }
    LighterTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_lighter.moc"
