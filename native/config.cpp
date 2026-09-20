#include "config.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <stdexcept>

static void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }
Config Config::parse(const QByteArray &data) {
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) fail(QStringLiteral("JSON 解析失败：%1（位置 %2）").arg(error.errorString()).arg(error.offset));
    if (!doc.isObject() || !doc.object()["projects"].isArray()) fail(QStringLiteral("projects 必须是数组"));
    Config config;
    config.extra = doc.object();
    const auto ports = doc.object().value("startupPorts");
    if (!ports.isUndefined() && !ports.isArray()) fail(QStringLiteral("startupPorts 必须是端口数组"));
    QSet<int> seenPorts;
    for (const auto &value : ports.toArray()) {
        const int port = value.toInt(-1);
        if (!value.isDouble() || value.toDouble() != port || port < 1 || port > 65535)
            fail(QStringLiteral("端口必须是 1–65535 的整数"));
        if (!seenPorts.contains(port)) config.startupPorts.append(quint16(port));
        seenPorts.insert(port);
    }
    QSet<QString> ids;
    for (const auto &value : doc.object()["projects"].toArray()) {
        if (!value.isObject()) fail(QStringLiteral("项目必须是对象"));
        const auto obj = value.toObject();
        Project p;
        p.extra = obj;
        p.id = obj["id"].toString(); p.name = obj["name"].toString();
        if (p.id.trimmed().isEmpty() || p.name.trimmed().isEmpty()) fail(QStringLiteral("项目 id 和名称不能为空"));
        if (ids.contains(p.id)) fail(QStringLiteral("项目 id 重复：%1").arg(p.id));
        ids.insert(p.id);
        if (!obj["workingDir"].isUndefined() && !obj["workingDir"].isNull() && !obj["workingDir"].isString()) fail(QStringLiteral("workingDir 必须是字符串"));
        p.workingDir = obj["workingDir"].toString();
        p.type = obj.contains("type") ? obj["type"].toString() : QStringLiteral("command");
        if (p.type != "command" && p.type != "script") fail(QStringLiteral("未知项目类型：%1").arg(p.type));
        if (p.type == "script") {
            p.script = obj["script"].toString();
            if (p.script.trimmed().isEmpty()) fail(QStringLiteral("脚本不能为空：%1").arg(p.name));
        } else if (obj["commands"].isArray()) {
            for (const auto &entry : obj["commands"].toArray()) {
                const auto c = entry.toObject();
                if (!entry.isObject() || c["command"].toString().trimmed().isEmpty()) fail(QStringLiteral("命令不能为空：%1").arg(p.name));
                if (!c["name"].isUndefined() && !c["name"].isNull() && !c["name"].isString()) fail(QStringLiteral("命令名称必须是字符串"));
                p.commands.append({c["name"].toString(), c["command"].toString()});
            }
        } else {
            if (!obj["commands"].isUndefined() && !obj["commands"].isNull()) fail(QStringLiteral("commands 必须是数组"));
            const auto command = obj["command"].toString();
            if (!command.trimmed().isEmpty()) p.commands.append({QStringLiteral("主命令"), command});
        }
        if (p.type == "command" && p.commands.isEmpty()) fail(QStringLiteral("项目至少需要一条命令：%1").arg(p.name));
        config.projects.append(p);
    }
    return config;
}
QByteArray Config::json() const {
    QJsonObject root = extra;
    QJsonArray projectsJson;
    for (const auto &p : projects) {
        auto obj = p.extra;
        obj["id"] = p.id; obj["name"] = p.name; obj["workingDir"] = p.workingDir;
        obj.remove("command");
        obj["type"] = p.type;
        if (p.type == "script") {
            obj.remove("commands"); obj["script"] = p.script;
        } else {
            obj.remove("script");
            QJsonArray commands;
            for (const auto &c : p.commands) commands.append(QJsonObject{{"name", c.name}, {"command", c.command}});
            obj["commands"] = commands;
        }
        projectsJson.append(obj);
    }
    root["projects"] = projectsJson;
    QJsonArray ports;
    for (const auto port : startupPorts) ports.append(int(port));
    root["startupPorts"] = ports;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}
QList<Command> Project::executionCommands() const {
    if (type == "script") return {{QStringLiteral("脚本"), script}};
    return commands;
}
QString ConfigStore::defaultPath() {
    // Tauri app_config_dir: Roaming on Windows, Application Support on macOS,
    // XDG_CONFIG_HOME on Linux. Do not add Qt's organization/application suffix.
#ifdef Q_OS_WIN
    QString base = qEnvironmentVariable("APPDATA");
    if (base.isEmpty()) base = QDir::homePath() + "/AppData/Roaming";
#elif defined(Q_OS_MACOS)
    const QString base = QDir::homePath() + "/Library/Application Support";
#else
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!QDir::isAbsolutePath(base)) base = QDir::homePath() + "/.config";
#endif
    return base + "/com.dreamreflex.lighter/config.json";
}
ConfigStore::ConfigStore(QString path) : path_(std::move(path)) {}
Config ConfigStore::load() const {
    if (!QFileInfo::exists(path_)) return {};
    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
    return Config::parse(file.readAll());
}
void ConfigStore::write(const QString &path, const QByteArray &data) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) fail(QStringLiteral("无法创建配置目录"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) fail(file.errorString());
}
void ConfigStore::save(const Config &config) const {
    Config::parse(config.json());
    // Preserve the original on first Qt write, even if later saves fail.
    if (QFileInfo::exists(path_) && !QFileInfo::exists(path_ + ".pre-qt.bak")) {
        if (!QFile::copy(path_, path_ + ".pre-qt.bak")) fail(QStringLiteral("无法备份原配置，未覆盖原文件"));
    }
    write(path_, config.json());
}
