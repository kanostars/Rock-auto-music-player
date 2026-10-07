#pragma once
#include <QWidget>

class QLabel;
class QLineEdit;

namespace rock {
class TimeSeekEdit final:public QWidget {
    Q_OBJECT
public:
    explicit TimeSeekEdit(QWidget* parent=nullptr);
    void setPosition(double seconds,double duration);
    void cancelEditing();
signals:
    void seekRequested(double seconds);
protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* object,QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
private:
    QLineEdit* input_{};
    QLabel* durationLabel_{};
    double position_{},duration_{};
    bool editing_{},submitting_{};
    QString error_;
    void refreshText();
    void updateAppearance();
    void updateWidth();
    void submit(bool leaving);
};
}
