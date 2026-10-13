#include "ui/main_window.h"
#include "app/app_log.h"
#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("RockAutoMusicPlay");
    QApplication::setApplicationVersion(ROCK_APP_VERSION);
    QApplication::setOrganizationName("RockAutoMusicPlay");
    auto& log=rock::AppLog::instance();log.initialize();
    log.write(rock::LogLevel::Info,"应用",QString("启动 RockAutoMusicPlay %1 · Qt %2").arg(QApplication::applicationVersion(),QString::fromLatin1(qVersion())));
    QApplication::setWindowIcon(QIcon(":/icons/app.png"));
    QApplication::setStyle("Fusion");
    QApplication::setFont(QFont("Microsoft YaHei UI", 10));
    rock::MainWindow window;
    window.showMaximized();
    const auto paths = QApplication::arguments().mid(1);
    if (!paths.isEmpty())
        QTimer::singleShot(0, &window, [&window, paths] { window.openFiles(paths); });
    const int result=QApplication::exec();
    log.write(rock::LogLevel::Info,"应用",QString("应用退出，退出码 %1。").arg(result));
    return result;
}
