#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("DreamReflexLighter");
    QCoreApplication::setOrganizationName("DreamReflex");
    QCoreApplication::setApplicationVersion("2.0.0");
    app.setWindowIcon(QIcon(":/icon.png"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("原生 Qt 多项目启动器")); parser.addHelpOption(); parser.addVersionOption();
    QCommandLineOption configOption("config", QStringLiteral("使用指定的 JSON 配置文件"), "path"); parser.addOption(configOption); parser.process(app);
    const QString path = QFileInfo(parser.isSet(configOption) ? parser.value(configOption) : ConfigStore::defaultPath()).absoluteFilePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) { QMessageBox::critical(nullptr, QStringLiteral("无法启动"), QStringLiteral("无法创建配置目录")); return 1; }
    QLockFile lock(path + ".lock");
    if (!lock.tryLock(100)) { QMessageBox::warning(nullptr, QStringLiteral("无法打开配置"), QStringLiteral("配置文件已由另一个启动器使用，或目录不可写。\n%1").arg(path)); return 1; }
    Window window(path); window.show();
    return app.exec();
}
