#include "ui/main_window.h"
#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("RockAutoMusicPlay");
    QApplication::setApplicationVersion(ROCK_APP_VERSION);
    QApplication::setOrganizationName("RockAutoMusicPlay");
    QApplication::setWindowIcon(QIcon(":/icons/app.png"));
    QApplication::setStyle("Fusion");
    QApplication::setFont(QFont("Microsoft YaHei UI", 10));
    rock::MainWindow window;
    window.showMaximized();
    const auto paths = QApplication::arguments().mid(1);
    if (!paths.isEmpty())
        QTimer::singleShot(0, &window, [&window, paths] { window.openFiles(paths); });
    return QApplication::exec();
}
