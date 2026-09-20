#include "runner.h"
#include <QDir>
#include <QProcessEnvironment>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

Runner::Runner(QObject *parent) : QObject(parent) {}
Runner::~Runner() {
    const auto jobs = jobs_;
    for (const auto &job : jobs) {
        job->process->disconnect(this);
        killTree(job);
        job->process->waitForFinished(3000);
    }
}
QString Runner::script(const Project &project) {
    QString script;
#ifdef Q_OS_WIN
    // Hold execution until the parent assigns this shell to its Windows Job.
    script = "[void][Console]::ReadLine();\n"
             "[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false);\n"
             "$OutputEncoding = [Console]::OutputEncoding;\n"
             "$ErrorActionPreference = 'Stop';\ntry {\n";
    for (const auto &command : project.executionCommands()) {
        script += command.command + "\nif (!$?) { if ($LASTEXITCODE) { exit $LASTEXITCODE }; exit 1 }\n";
    }
    script += "} catch { [Console]::Error.WriteLine($_); exit 1 }; exit 0\n";
#else
    // One shell preserves cd/export and other state between steps. Every failed
    // step stops the sequence; commands are intentionally user-authored shell code.
    script = "set -e\n";
    for (const auto &command : project.executionCommands()) script += command.command + "\n";
#endif
    return script;
}
void Runner::start(const Project &project) {
    if (running(project.id)) return;
    const auto commands = project.executionCommands();
    if (commands.isEmpty() || (project.type == "script" && project.script.trimmed().isEmpty())) { emit error(QStringLiteral("项目没有配置命令或脚本")); return; }
    const QString cwd = project.workingDir.isEmpty() ? QDir::currentPath() : project.workingDir;
    if (!QFileInfo(cwd).isDir()) { emit error(QStringLiteral("工作目录不存在：%1").arg(cwd)); return; }
    auto job = std::make_shared<Job>();
    auto *process = new QProcess(this);
    job->process = process;
    process->setWorkingDirectory(cwd);
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8"); env.insert("PYTHONUTF8", "1");
    process->setProcessEnvironment(env);
#ifdef Q_OS_WIN
    job->handle = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job->handle || !SetInformationJobObject(job->handle, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        if (job->handle) CloseHandle(job->handle);
        process->deleteLater(); emit error(QStringLiteral("无法创建 Windows 进程组")); return;
    }
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#else
    process->setChildProcessModifier([] { if (::setsid() < 0) ::_exit(127); });
#endif
    jobs_.insert(project.id, job);
    const auto id = project.id;
    connect(process, &QProcess::readyReadStandardOutput, this, [this, id, job] {
        emit output(id, job->decoder.decode(job->process->readAllStandardOutput()));
    });
    connect(process, &QProcess::started, this, [this, id, job] {
        job->pid = job->process->processId();
#ifdef Q_OS_WIN
        HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(job->pid));
        const bool assigned = child && AssignProcessToJobObject(job->handle, child);
        if (child) CloseHandle(child);
        if (!assigned) {
            emit error(QStringLiteral("无法管理子进程，已取消启动"));
            job->process->kill(); return;
        }
        job->process->write("\n");
#endif
        job->process->closeWriteChannel();
        if (job->stopping) killTree(job);
        emit stateChanged(id);
    });
    connect(process, &QProcess::errorOccurred, this, [this, id, job](QProcess::ProcessError value) {
        if (value == QProcess::FailedToStart) {
            emit error(QStringLiteral("启动失败：%1").arg(job->process->errorString()));
            complete(id, job, -1);
        }
    });
    connect(process, &QProcess::finished, this, [this, id, job](int code, QProcess::ExitStatus status) {
        complete(id, job, status == QProcess::CrashExit ? -1 : code);
    });
    emit stateChanged(id);
    emit output(id, QStringLiteral("工作目录  %1\n").arg(cwd));
    for (int i = 0; i < commands.size(); ++i)
        emit output(id, QStringLiteral("%1. %2  ›  %3\n").arg(i + 1).arg(commands[i].name, commands[i].command));
    emit output(id, "\n");
#ifdef Q_OS_WIN
    const auto source = script(project);
    QByteArray utf16;
    for (QChar c : source) { utf16.append(char(c.unicode() & 255)); utf16.append(char(c.unicode() >> 8)); }
    process->start("powershell.exe", {"-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-EncodedCommand", QString::fromLatin1(utf16.toBase64())});
#else
    process->start("/bin/sh", {"-c", script(project)});
#endif
}
void Runner::killTree(const std::shared_ptr<Job> &job) {
#ifdef Q_OS_WIN
    if (job->handle) { TerminateJobObject(job->handle, 1); CloseHandle(job->handle); job->handle = nullptr; }
#else
    if (job->pid > 0) ::kill(-pid_t(job->pid), SIGKILL);
#endif
    if (job->process->state() != QProcess::NotRunning) job->process->kill();
}
void Runner::complete(const QString &id, const std::shared_ptr<Job> &job, int code) {
    if (jobs_.value(id) != job) return;
    emit output(id, job->decoder.decode(job->process->readAllStandardOutput()));
    killTree(job);
    jobs_.remove(id);
    emit output(id, QStringLiteral("\n[进程已退出，退出码：%1]\n").arg(code));
    emit stateChanged(id);
    emit finished(id, code);
    job->process->deleteLater();
}
void Runner::stop(const QString &id) {
    const auto job = jobs_.value(id);
    if (!job) return;
    job->stopping = true;
    // A Starting process is killed after started(), once the process group exists.
    if (job->pid > 0) killTree(job);
}
void Runner::stopAll() { for (const auto &id : jobs_.keys()) stop(id); }
