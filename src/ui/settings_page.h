#pragma once
#include "app/preferences.h"
#include "platform/key_output.h"
#include <QFutureWatcher>
#include <QWidget>
#include <QPointer>
class QKeySequenceEdit;class QLabel;class QPushButton;class QDialog;class QHideEvent;
namespace rock {
class SettingsPage:public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent=nullptr);
    void setPerformanceActive(bool active);
    void setKeyboardDiscoveryActive(bool active);
    void closeWindows();
signals:
    void driverInspectionActiveChanged(bool active);
    void openLogsRequested();
protected:
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched,QEvent* event) override;
private:
    std::array<QKeySequenceEdit*,shortcutCount> editors_{};
    QWidget* shortcutForm_{};QLabel* status_{};QPushButton *save_{},*reset_{},*cancel_{},*test_{};
    QPushButton* shortcutButton_{};
    QPointer<QDialog> appearanceWindow_,shortcutWindow_,driverWindow_;
    QPointer<QDialog> testWindow_,aboutWindow_;
    bool active_{};
    QLabel *driverStatus_{},*driverDetail_{};
    QPushButton* refreshDriver_{};
    QFutureWatcher<InterceptionStatus> driverWatcher_;
    bool driverRefreshPending_{};
    bool keyboardDiscoveryActive_{};
    void openSettingsWindow(QDialog* window);
    void loadBindings(const ShortcutBindings& bindings);
    void saveBindings();
    void updateDraft();
    void refreshInterception();
    void showInterceptionStatus(const InterceptionStatus& result);
    void showInterceptionPaused();
    void showInterceptionWaiting();
};
}
