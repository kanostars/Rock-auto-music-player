#pragma once
#include "app/app_log.h"
#include <QDialog>
#include <QTimer>

class QCheckBox;class QComboBox;class QLabel;class QLineEdit;class QPlainTextEdit;class QPushButton;
namespace rock {
class LogDialog final:public QDialog {
    Q_OBJECT
public:
    explicit LogDialog(QWidget* parent=nullptr);
private:
    QPlainTextEdit* text_{};
    QComboBox* level_{};
    QLineEdit* search_{};
    QCheckBox* follow_{};
    QLabel *summary_{},*path_{},*error_{};
    QPushButton *copy_{},*save_{},*folder_{};
    QTimer refreshTimer_;
    QList<quint64> displayed_;
    bool dirty_{true};
    void refreshLogs(bool force=false);
    void saveLogs();
};
}
