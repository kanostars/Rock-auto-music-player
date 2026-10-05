#pragma once
#include "app/preferences.h"
#include <QWidget>
#include <QPointer>
class QKeySequenceEdit;class QLabel;class QPushButton;class QDialog;
namespace rock {
class SettingsPage:public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent=nullptr);
    void setPerformanceActive(bool active);
private:
    std::array<QKeySequenceEdit*,shortcutCount> editors_{};
    QWidget* shortcutForm_{};QLabel* status_{};QPushButton *save_{},*reset_{},*cancel_{},*test_{};
    QPointer<QDialog> testWindow_;
    bool active_{};
    void loadBindings(const ShortcutBindings& bindings);
    void saveBindings();
    void updateDraft();
};
}
