#include "ui/main_window.h"
#include "app/app_log.h"
#include "app/branding.h"
#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("RockAutoMusicPlay");
    QApplication::setApplicationDisplayName(QString::fromUtf8(rock::appDisplayName));
    QApplication::setApplicationVersion(ROCK_APP_VERSION);
    QApplication::setOrganizationName("RockAutoMusicPlay");
    auto& log=rock::AppLog::instance();log.initialize();
    log.write(rock::LogLevel::Info,"应用",QString("启动 %1 %2 · Qt %3").arg(QApplication::applicationDisplayName(),QApplication::applicationVersion(),QString::fromLatin1(qVersion())));
    QApplication::setWindowIcon(QIcon(":/icons/app.png"));
    QApplication::setStyle("Fusion");
    QApplication::setFont(QFont("Microsoft YaHei UI", 10));
    rock::MainWindow window;
    window.showMaximized();
    const auto paths = QApplication::arguments().mid(1);
    QTimer::singleShot(0, &window, [&window, paths] {
        if (paths.isEmpty()) window.restoreRecentProject();
        else window.openFiles(paths);
        window.showStartupHelp();
    });
    const int result=QApplication::exec();
    log.write(rock::LogLevel::Info,"应用",QString("应用退出，退出码 %1。").arg(result));
    return result;
}
