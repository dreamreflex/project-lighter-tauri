#include "ports.h"
#include <QCoreApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <vector>
#else
#include <signal.h>
#include <errno.h>
#include <cstring>
#endif
namespace {
struct Output { int code = -1; QString text, error; };
Output run(const QString &program, const QStringList &arguments) {
    QProcess process;
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    process.start(program, arguments);
    if (!process.waitForStarted(3000)) return {-1, {}, process.errorString()};
    if (!process.waitForFinished(5000)) { process.kill(); process.waitForFinished(); return {-1, {}, QStringLiteral("查询超时")}; }
    return {process.exitCode(), QString::fromUtf8(process.readAllStandardOutput()), QString::fromUtf8(process.readAllStandardError())};
}
}
PortResult Ports::query(quint16 port) {
    PortResult result;
    if (!port) { result.error = QStringLiteral("请输入 1–65535 的端口号"); return result; }
    QSet<qint64> seen;
    auto add = [&](qint64 pid, const QString &name) {
        result.occupied = true;
        if (pid > 0 && !seen.contains(pid)) { seen.insert(pid); result.owners.append({pid, name}); }
    };
#ifdef Q_OS_WIN
    for (ULONG family : {ULONG(AF_INET), ULONG(AF_INET6)}) {
        ULONG size = 0;
        auto status = GetExtendedTcpTable(nullptr, &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (status != ERROR_INSUFFICIENT_BUFFER && status != NO_ERROR) { result.error = QStringLiteral("无法读取 TCP 表：%1").arg(status); return result; }
        std::vector<unsigned char> buffer(size);
        status = GetExtendedTcpTable(buffer.data(), &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (status != NO_ERROR) { result.error = QStringLiteral("无法读取 TCP 表：%1").arg(status); return result; }
        auto ownerName = [](DWORD pid) {
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            wchar_t name[32768]; DWORD length = 32768;
            QString text = QStringLiteral("未知进程");
            if (process) {
                if (QueryFullProcessImageNameW(process, 0, name, &length)) text = QString::fromWCharArray(name, int(length)).section('\\', -1);
                CloseHandle(process);
            }
            return text;
        };
        if (family == AF_INET) {
            auto *table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID *>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto &row = table->table[i];
                if (ntohs(u_short(row.dwLocalPort)) == port) add(row.dwOwningPid, ownerName(row.dwOwningPid));
            }
        } else {
            auto *table = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID *>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto &row = table->table[i];
                if (ntohs(u_short(row.dwLocalPort)) == port) add(row.dwOwningPid, ownerName(row.dwOwningPid));
            }
        }
    }
#else
    const auto lsof = QStandardPaths::findExecutable("lsof");
    if (!lsof.isEmpty()) {
        const auto out = run(lsof, {"-nP", "-a", "-iTCP:" + QString::number(port), "-sTCP:LISTEN", "-Fpc"});
        if (out.code == 0) {
            qint64 pid = 0;
            for (const auto &line : out.text.split('\n')) {
                if (line.startsWith('p')) pid = line.mid(1).toLongLong();
                if (line.startsWith('c') && pid > 0) add(pid, line.mid(1));
            }
        } else if (out.code != 1) result.error = out.error;
    }
    // ss also exposes listeners owned by other users where lsof cannot show a PID.
    const auto ss = QStandardPaths::findExecutable("ss");
    if (!ss.isEmpty()) {
        const auto out = run(ss, {"-H", "-ltnp", "sport = :" + QString::number(port)});
        if (out.code != 0) { if (result.owners.isEmpty()) result.error = out.error.isEmpty() ? QStringLiteral("ss 查询失败") : out.error; }
        else {
            result.error.clear();
            static const QRegularExpression owner("\\(\"([^\"]+)\",pid=(\\d+)");
            for (const auto &line : out.text.split('\n', Qt::SkipEmptyParts)) {
                result.occupied = true;
                auto matches = owner.globalMatch(line);
                while (matches.hasNext()) { const auto match = matches.next(); add(match.captured(2).toLongLong(), match.captured(1)); }
            }
        }
    } else if (lsof.isEmpty()) result.error = QStringLiteral("请安装 lsof 或 iproute2（ss）以查询端口");
#endif
    return result;
}
QString Ports::terminate(quint16 port, const QList<PortOwner> &expected) {
    const auto current = query(port);
    if (!current.error.isEmpty()) return current.error;
    QSet<qint64> expectedIds, actualIds;
    for (const auto &owner : expected) expectedIds.insert(owner.pid);
    for (const auto &owner : current.owners) actualIds.insert(owner.pid);
    if (expectedIds.isEmpty() || expectedIds != actualIds) return QStringLiteral("端口占用已变化，请重新查询后操作");
    for (const auto &owner : current.owners) {
        if (owner.pid <= 1 || owner.pid == QCoreApplication::applicationPid()) return QStringLiteral("拒绝结束系统进程或启动器自身");
#ifdef Q_OS_WIN
        const auto out = run("taskkill.exe", {"/PID", QString::number(owner.pid), "/F", "/T"});
        if (out.code != 0) return QStringLiteral("结束 PID %1 失败：%2 %3").arg(owner.pid).arg(out.error, out.text);
#else
        if (::kill(pid_t(owner.pid), SIGKILL) != 0) return QStringLiteral("结束 PID %1 失败：%2").arg(owner.pid).arg(QString::fromLocal8Bit(std::strerror(errno)));
#endif
    }
    return {};
}
