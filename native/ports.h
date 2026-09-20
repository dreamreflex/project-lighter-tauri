#pragma once
#include <QList>
#include <QString>
struct PortOwner { qint64 pid; QString name; };
struct PortResult { QList<PortOwner> owners; QString error; bool occupied = false; };
namespace Ports {
PortResult query(quint16 port);
QString terminate(quint16 port, const QList<PortOwner> &expected);
}
