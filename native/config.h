#pragma once
#include <QJsonObject>
#include <QList>
#include <QString>

struct Command { QString name; QString command; };
struct Project {
    QString id, name, workingDir;
    QList<Command> commands;
    QJsonObject extra;
    QString type = "command";
    QString script;
    QList<Command> executionCommands() const;
};
struct Config {
    QList<Project> projects;
    QJsonObject extra;
    QList<quint16> startupPorts;
    QByteArray json() const;
    static Config parse(const QByteArray &data);
};
class ConfigStore {
public:
    explicit ConfigStore(QString path = defaultPath());
    Config load() const;
    void save(const Config &config) const;
    QString path() const { return path_; }
    static QString defaultPath();
    static void write(const QString &path, const QByteArray &data);
private:
    QString path_;
};
