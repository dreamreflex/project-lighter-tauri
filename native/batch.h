#pragma once
#include "config.h"
#include "ports.h"
#include "runner.h"
#include <QObject>
#include <atomic>
#include <memory>

class BatchLauncher : public QObject {
    Q_OBJECT
public:
    explicit BatchLauncher(Runner &runner, QObject *parent = nullptr);
    ~BatchLauncher() override;
    bool busy() const { return phase_ != Idle; }
    bool stopping() const { return phase_ == Cancelling; }
    void start(const Config &config);
    void confirmPorts(bool accepted);
    void stop();
signals:
    void changed();
    void confirmationNeeded(const QString &details);
    void projectStarting(const QString &id);
    void error(const QString &message);
private:
    enum Phase { Idle, Checking, Confirming, Releasing, Launching, Cancelling };
    struct Snapshot { quint16 port; PortResult result; };
    struct Check { QList<Snapshot> ports; QString error; };
    Runner &runner_;
    Phase phase_ = Idle;
    Config pending_;
    QList<Snapshot> ports_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    void launch();
    void finish(const QString &error = {});
};
