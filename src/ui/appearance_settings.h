#pragma once
#include <QDialog>
#include <QPointer>
#include <array>
#include <vector>

class QAbstractButton;
class QCloseEvent;
class QColor;
class QComboBox;
class QHideEvent;
class QLabel;
class QPushButton;
namespace rock {
namespace Theme {struct ImageColors;}
class AppearanceSettingsDialog:public QDialog {
    Q_OBJECT
public:
    explicit AppearanceSettingsDialog(QWidget* parent=nullptr);
    void closeWindows();
protected:
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
private:
    std::array<QPushButton*,3> modeButtons_{};
    QComboBox* preset_{};
    QPointer<QDialog> library_,imageWindow_,imagePicker_,colorPicker_,wallpaperWindow_;
    QLabel *libraryStatus_{},*imageStatus_{};
    std::vector<QAbstractButton*> cards_;
    void syncFromPreferences();
    void openLibrary();
    void openWallpapers();
    void importImage();
    void importImageFile(const QString& path);
    void openCustomColor();
    void applyCustomAccent(const QColor& color);
    void showImageColors(const Theme::ImageColors& colors,const QString& filename);
    void closeLibraryChildren();
};
}
