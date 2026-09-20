#pragma once
#include "config.h"
#include <QObject>
#include <QHash>
#include <QProcess>
#include <QStringDecoder>
#include <memory>

class Runner : public QObject {
    Q_OBJECT
public:
    explicit Runner(QObject *parent = nullptr);
    ~Runner() override;
    bool running(const QString &id) const { return jobs_.contains(id); }
    int count() const { return jobs_.size(); }
    void start(const Project &project);
    void stop(const QString &id);
    void stopAll();
    static QString script(const Project &project);
signals:
    void output(const QString &id, const QString &text);
    void stateChanged(const QString &id);
    void finished(const QString &id, int code);
    void error(const QString &message);
private:
    struct Job {
        QProcess *process = nullptr;
        qint64 pid = 0;
        bool stopping = false;
        QStringDecoder decoder{QStringDecoder::Utf8};
#ifdef Q_OS_WIN
        void *handle = nullptr;
#endif
    };
    QHash<QString, std::shared_ptr<Job>> jobs_;
    void killTree(const std::shared_ptr<Job> &job);
    void complete(const QString &id, const std::shared_ptr<Job> &job, int code);
};
