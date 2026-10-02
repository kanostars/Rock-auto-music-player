#pragma once
#include <QWidget>
#include <QPointer>
class QLabel;class QPushButton;class QSlider;class QListView;class QListWidget;class QFrame;class QVBoxLayout;class QScreen;
namespace rock {
struct MiniPlayerState {
    QString title,status,detail;
    double position{},duration{},rangeFirst{},rangeLast{};
    bool performance{},playing{},canPlay{},busy{},seekEnabled{},modeEnabled{};
    int mode{3};
    int volume{60};
};
class MiniPlayer:public QWidget {
    Q_OBJECT
public:
    explicit MiniPlayer(QListWidget* library);
    void present(QScreen* preferred);
    void setState(const MiniPlayerState& state);
signals:
    void sourceRequested(bool performance);
    void playRequested();
    void navigateRequested(bool previous);
    void modeRequested();
    void seekStarted();
    void seekRequested(double seconds);
    void songPlayRequested(int row);
    void volumeRequested(int value);
    void moveRequested(int from,int to);
    void removeRequested(int row);
    void restoreRequested();
    void quitRequested();
protected:
    bool eventFilter(QObject*,QEvent*) override;
    bool nativeEvent(const QByteArray&,void*,qintptr*) override;
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;
    void closeEvent(QCloseEvent*) override;
private:
    QFrame *capsule_{},*queue_{};
    QVBoxLayout* layout_{};
    QLabel *title_{},*status_{},*elapsed_{},*duration_{},*count_{},*queueMode_{};
    QPushButton *audition_{},*performance_{},*play_{},*previous_{},*next_{},*mode_{},*list_{},*up_{},*down_{},*remove_{};
    QSlider *progress_{},*volume_{};QListView* songs_{};
    QWidget *volumeBox_{},*resizeHandle_{};QLabel* volumeText_{};
    MiniPlayerState state_;bool initialized_{},expanded_{},above_{},dragging_{};QPoint dragOffset_;
    bool resizing_{};QSize capsuleSize_{664,112},resizeStartSize_;
    QPoint resizeStartPoint_,resizeOrigin_;QPointer<QScreen> gestureScreen_;
    int lastCount_{-1};
    void toggleList();
    void arrange(const QPoint& capsuleOrigin,QScreen* preferred=nullptr);
    QPoint boundedPosition(const QPoint& requested,QScreen* screen,QSize extent={}) const;
    void constrain();
    void savePosition();
};
}
