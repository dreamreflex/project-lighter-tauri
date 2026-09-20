#include "batch.h"
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSet>
#include <QThread>
#include <QtConcurrent>

BatchLauncher::BatchLauncher(Runner &runner, QObject *parent) : QObject(parent), runner_(runner) {}
BatchLauncher::~BatchLauncher() { if (cancelled_) cancelled_->store(true); }
void BatchLauncher::finish(const QString &message) {
    phase_ = Idle; pending_ = {}; ports_.clear(); emit changed();
    if (!message.isEmpty()) emit error(message);
}
void BatchLauncher::start(const Config &config) {
    if (busy() || runner_.count() || config.projects.isEmpty()) return;
    try { pending_ = Config::parse(config.json()); }
    catch (const std::exception &e) { emit error(QString::fromUtf8(e.what())); return; }
    // Validate all working directories before terminating any port owners.
    for (const auto &project : pending_.projects) {
        if (!project.workingDir.isEmpty() && !QFileInfo(project.workingDir).isDir()) {
            finish(QStringLiteral("工作目录不存在：%1").arg(project.workingDir)); return;
        }
    }
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    phase_ = Checking; emit changed();
    auto *watcher = new QFutureWatcher<Check>(this);
    connect(watcher, &QFutureWatcher<Check>::finished, this, [this, watcher] {
        const auto check = watcher->result(); watcher->deleteLater();
        if (cancelled_->load()) { finish(); return; }
        if (!check.error.isEmpty()) { finish(check.error); return; }
        ports_ = check.ports;
        QStringList lines;
        for (const auto &snapshot : ports_) for (const auto &owner : snapshot.result.owners)
            lines << QStringLiteral("TCP %1 · %2 · PID %3").arg(snapshot.port).arg(owner.name).arg(owner.pid);
        if (lines.isEmpty()) { launch(); return; }
        phase_ = Confirming; emit changed(); emit confirmationNeeded(lines.join('\n'));
    });
    watcher->setFuture(QtConcurrent::run([ports = pending_.startupPorts, cancelled = cancelled_] {
        Check check;
        for (const auto port : ports) {
            if (cancelled->load()) return check;
            const auto result = Ports::query(port);
            if (!result.error.isEmpty()) { check.error = result.error; break; }
            if (result.occupied && result.owners.isEmpty()) {
                check.error = QStringLiteral("端口 %1 被占用，但无法获取进程信息。请手动释放后重试。").arg(port); break;
            }
            check.ports.append({port, result});
        }
        return check;
    }));
}
void BatchLauncher::confirmPorts(bool accepted) {
    if (phase_ != Confirming) return;
    if (!accepted) { finish(); return; }
    phase_ = Releasing; emit changed();
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        const auto message = watcher->result(); watcher->deleteLater();
        if (cancelled_->load()) { finish(); return; }
        if (!message.isEmpty()) { finish(message); return; }
        launch();
    });
    watcher->setFuture(QtConcurrent::run([snapshots = ports_, cancelled = cancelled_] {
        QSet<qint64> terminated;
        for (const auto &snapshot : snapshots) {
            if (cancelled->load()) return QString();
            if (!snapshot.result.occupied) continue;
            auto current = Ports::query(snapshot.port);
            if (!current.error.isEmpty()) return current.error;
            if (!current.occupied) continue;
            QList<PortOwner> expected;
            for (const auto &owner : snapshot.result.owners) if (!terminated.contains(owner.pid)) expected.append(owner);
            // One process may listen on multiple configured ports. A successful
            // earlier termination must not be mistaken for a new port owner.
            bool onlyTerminated = !current.owners.isEmpty();
            for (const auto &owner : current.owners) if (!terminated.contains(owner.pid)) onlyTerminated = false;
            if (onlyTerminated) continue;
            if (cancelled->load()) return QString();
            const auto error = Ports::terminate(snapshot.port, expected);
            if (!error.isEmpty()) return error;
            for (const auto &owner : expected) terminated.insert(owner.pid);
        }
        // Wait for the OS to remove listeners, and detect a new process binding
        // a checked port before starting any project. Never kill that new owner.
        for (int attempt = 0; attempt < 30; ++attempt) {
            if (cancelled->load()) return QString();
            bool occupied = false;
            for (const auto &snapshot : snapshots) {
                if (cancelled->load()) return QString();
                const auto result = Ports::query(snapshot.port);
                if (!result.error.isEmpty()) return result.error;
                occupied = occupied || result.occupied;
            }
            if (!occupied) return QString();
            QThread::msleep(100);
        }
        return QStringLiteral("端口仍被占用，未启动项目。请重新检查端口。");
    }));
}
void BatchLauncher::launch() {
    if (cancelled_->load()) { finish(); return; }
    phase_ = Launching; emit changed();
    const auto projects = pending_.projects;
    for (const auto &project : projects) {
        if (cancelled_->load()) break;
        if (runner_.running(project.id)) continue;
        emit projectStarting(project.id); runner_.start(project);
    }
    finish();
}
void BatchLauncher::stop() {
    if (cancelled_) cancelled_->store(true);
    runner_.stopAll();
    if (phase_ == Checking || phase_ == Releasing) { phase_ = Cancelling; emit changed(); }
    else if (phase_ == Confirming) finish();
    else emit changed();
}
