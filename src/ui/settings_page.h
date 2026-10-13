#pragma once
#include "app/preferences.h"
#include "platform/key_output.h"
#include <QFutureWatcher>
#include <QWidget>
#include <QPointer>
class QKeySequenceEdit;class QLabel;class QPushButton;class QDialog;class QShowEvent;
namespace rock {
class SettingsPage:public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent=nullptr);
    void setPerformanceActive(bool active);
    void setKeyboardDiscoveryActive(bool active);
signals:
    void driverInspectionActiveChanged(bool active);
protected:
    void showEvent(QShowEvent* event) override;
private:
    std::array<QKeySequenceEdit*,shortcutCount> editors_{};
    QWidget* shortcutForm_{};QLabel* status_{};QPushButton *save_{},*reset_{},*cancel_{},*test_{};
    QPointer<QDialog> testWindow_;
    bool active_{};
    QLabel *driverStatus_{},*driverDetail_{};
    QPushButton* refreshDriver_{};
    QFutureWatcher<InterceptionStatus> driverWatcher_;
    bool driverRefreshPending_{};
    bool keyboardDiscoveryActive_{};
    void loadBindings(const ShortcutBindings& bindings);
    void saveBindings();
    void updateDraft();
    void refreshInterception();
    void showInterceptionStatus(const InterceptionStatus& result);
    void showInterceptionPaused();
    void showInterceptionWaiting();
};
}
