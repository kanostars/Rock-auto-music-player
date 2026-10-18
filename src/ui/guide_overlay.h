#pragma once

#include <QMetaObject>
#include <QPointer>
#include <QRect>
#include <QWidget>
#include <vector>

class QFrame;
class QLabel;
class QPushButton;
class QScrollArea;
class QScreen;
class QWindow;

namespace rock {
class GuideOverlay final : public QWidget {
    Q_OBJECT
public:
    explicit GuideOverlay(QWidget* host,bool detached=false);
    ~GuideOverlay() override;
    void present(QWidget* target,const QString& topic,const QString& title,const QString& body,int index,int total);
    void dismiss();

signals:
    void previousRequested();
    void nextRequested();
    void exitRequested();

protected:
    void paintEvent(QPaintEvent*) override;
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched,QEvent* event) override;

private:
    QPointer<QWidget> host_,target_,previousFocus_;
    QPointer<QWindow> hostWindow_;
    QPointer<QScreen> screen_;
    std::vector<QPointer<QWidget>> ancestors_;
    QMetaObject::Connection targetDestroyed_,hostScreenChanged_,screenGeometryChanged_,screenAvailableChanged_;
    QFrame* card_{};
    QLabel* title_{};
    QLabel* body_{};
    QLabel* progress_{};
    QScrollArea* bodyScroll_{};
    QPushButton* exit_{};
    QPushButton* previous_{};
    QPushButton* next_{};
    QRect spotlight_;
    bool active_{},geometryPending_{},detached_{};

    void refreshGeometry();
    void scheduleGeometry();
    void requestExit();
    void cycleFocus(bool backwards);
    bool containsObject(QObject* object)const;
    bool watchesObject(QObject* object)const;
};
}
