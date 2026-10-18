#pragma once
#include <QDialog>
#include <QPointer>
#include <QStringList>

class QCloseEvent;
class QFileDialog;
class QHideEvent;
class QInputDialog;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QWidget;
namespace rock {
class WallpaperLibraryDialog final:public QDialog {
    Q_OBJECT
public:
    explicit WallpaperLibraryDialog(QWidget* parent=nullptr);
    void importFiles(const QStringList& paths);
protected:
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
private:
    QWidget* content_{};
    QScrollArea* scroll_{};
    QLabel* status_{};
    QProgressBar* progress_{};
    QPushButton* import_{};
    QPushButton* none_{};
    QPointer<QFileDialog> picker_;
    QPointer<QInputDialog> renameWindow_;
    QString renamingId_;
    QStringList entryKeys_;
    QString importReport_;
    bool rebuildPending_{};
    void openImport();
    void renameWallpaper(const QString& id);
    void scheduleRebuild();
    void rebuild();
    void syncSelection();
    void syncBusy(bool busy);
};
}
