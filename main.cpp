#include "ui/main_window.h"
#include <QApplication>
#include <QFont>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("RockAutoMusicPlay");
    QApplication::setApplicationVersion(ROCK_APP_VERSION);
    QApplication::setOrganizationName("RockAutoMusicPlay");
    QApplication::setStyle("Fusion");
    QApplication::setFont(QFont("Microsoft YaHei UI", 10));
    rock::MainWindow window;
    window.showMaximized();
    if (QApplication::arguments().size() > 1)
        window.importFiles({QApplication::arguments().at(1)});
    return QApplication::exec();
}
