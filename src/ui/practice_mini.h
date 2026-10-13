#pragma once
#include "core/music.h"
#include <QPointer>
#include <QWidget>
#include <array>
#include <cstdint>
#include <memory>

class QLabel;class QPushButton;class QSlider;class QFrame;class QStackedWidget;class QVBoxLayout;class QHBoxLayout;class QScreen;
namespace rock {
class NumberMeasureView;
struct PracticeMiniState {
    QString title,status,hint,progress;
    uint16_t keys{},matched{};
    std::array<int,9> inputKeys{rock::keys[0],rock::keys[1],rock::keys[2],rock::keys[3],rock::keys[4],rock::keys[5],rock::keys[6],rock::keys[7],rock::keys[8]};
    bool following{},playing{},canPlay{};
    int practiceGroup{-1};
    double position{};
    double playbackPosition{},duration{};
    int volume{60};
};
class PracticeMini final:public QWidget {
    Q_OBJECT
public:
    explicit PracticeMini(QWidget* parent=nullptr);
    void present(QScreen* preferred=nullptr);
    void setSong(const QString& name,std::shared_ptr<const Song> song,
                 std::shared_ptr<const Conversion> result,const Settings& settings);
    void setState(const PracticeMiniState& state);
signals:
    void playRequested();
    void restartRequested();
    void followingRequested(bool following);
    void restoreRequested();
    void seekStarted();
    void seekRequested(double seconds);
    void volumeRequested(int value);
protected:
    bool eventFilter(QObject*,QEvent*) override;
    bool nativeEvent(const QByteArray&,void*,qintptr*) override;
    void showEvent(QShowEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void closeEvent(QCloseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    QFrame *capsule_{},*appearance_{};
    QVBoxLayout *layout_{},*capsuleLayout_{},*appearanceLayout_{};
    QHBoxLayout *headerLayout_{},*keysLayout_{},*toolsLayout_{},*opacityLayout_{},*sizeLayout_{};
    QLabel *title_{},*status_{},*progress_{},*clock_{},*volumeValue_{},*empty_{},*opacityValue_{},*sizeValue_{};
    QPushButton *play_{},*restart_{},*mode_{},*display_{},*appearanceButton_{},*restore_{},*volumeButton_{};
    QSlider *opacity_{},*sizeSlider_{},*seek_{},*volume_{};
    QStackedWidget* views_{};QWidget* keyView_{};NumberMeasureView* measure_{};
    std::array<QLabel*,9> chips_{};
    PracticeMiniState state_;
    bool numbers_{},appearanceExpanded_{},dragging_{},arranging_{},screenConnected_{};
    double scale_{1};QPoint dragOffset_;
    double seekDuration_{};std::array<int,2> audibleVolumes_{60,60};
    QPointer<QScreen> screen_,gestureScreen_;
    QMetaObject::Connection screenGeometry_,screenDpi_;
    void refreshText();
    void refreshIcons();
    void refreshClock();
    void applyScale(double scale);
    void arrange(const QPoint& origin,QScreen* preferred=nullptr);
    void watchScreen(QScreen* screen);
    QPoint boundedPosition(const QPoint& origin,QScreen* screen,const QSize& size) const;
    void savePosition();
};
}
