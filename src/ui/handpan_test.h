 #pragma once
#include "audio/live_handpan.h"
#include <QDialog>

class QLabel;
class QPlainTextEdit;
namespace rock {
class HandpanBoard : public QWidget {
    Q_OBJECT
public:
    explicit HandpanBoard(QWidget* parent=nullptr);
    void setHighlighted(int target,bool value);
signals:
    void padPressed(int target);
    void padReleased(int target);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    std::array<bool,9> held_{};
    int mouseTarget_{-1};
    QTransform boardTransform() const;
};
class HandpanTestDialog : public QDialog {
    Q_OBJECT
public:
    explicit HandpanTestDialog(QWidget* parent=nullptr);
    ~HandpanTestDialog() override;
protected:
    bool event(QEvent*) override;
    bool eventFilter(QObject*,QEvent*) override;
private:
    LiveHandpanPlayer audio_;
    HandpanBoard* board_{};
    QLabel* status_{};
    QPlainTextEdit* log_{};
    std::array<bool,9> keyboardHeld_{},mouseHeld_{};
    bool active_{};
    bool acceptsInput() const;
    void synchronizeFocus();
    void silence();
    void press(int target,bool mouse);
    void release(int target,bool mouse);
};
}
