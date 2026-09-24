#pragma once
#include "core/music.h"
#include "platform/output_discovery.h"
#include "audio/audio_player.h"
#include "playback/performance_controller.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QWidget>
#include <memory>
class QLabel; class QComboBox; class QSpinBox; class QPushButton; class QDialog; class QCheckBox; class QTimer; class QListWidget;
namespace rock {
// Output controls and playlist scheduling; the workbench owns the only score and timeline.
class PerformancePanel:public QWidget {
    Q_OBJECT
public:
    explicit PerformancePanel(QWidget* parent=nullptr,OutputDiscovery discovery={},AudioBackend backend=AudioBackend::System,std::unique_ptr<KeyOutput> output={});
    ~PerformancePanel() override;
    void stopPerformance();
    void startPerformance();
    bool active() const{return running_;}
    void setSong(std::shared_ptr<const Song>,std::shared_ptr<const Conversion>,const QString& path,const Settings&);
    void setPreviewPosition(double seconds);
    double previewPosition() const{return position_;}
    void setLibrary(QListWidget*);
    void setLibraryBusy(bool);
    QWidget* playlistControls() const{return playlistControls_;}
    QWidget* transportControls() const{return transportControls_;}
signals:
    void startRequested();
    void activeChanged(bool active);
    void statusChanged(const QString& text);
    void sourcePositionChanged(double seconds);
    void songChangeRequested(int row);
    void songRemoveRequested(int row);
    void songMoveRequested(int from,int to);
private:
    QLabel *state_{},*keyboardStatus_{},*windowStatus_{};
    QSpinBox* countdown_{};
    OutputDiscovery discovery_;AudioBackend audioBackend_;
    QFutureWatcher<DiscoveryResult> keyboardWatcher_,windowWatcher_;
    QComboBox *keyboards_{},*windows_{};
    QPushButton* playMode_{};
    int playModeIndex_{};
    void updatePlayMode();
    QPushButton *refreshKeyboards_{},*refreshWindows_{},*start_{},*testKeys_{};
    QPushButton *previousSong_{},*nextSong_{},*removeSong_{},*moveUp_{},*moveDown_{};
    QPointer<QDialog> keyTestWindow_;
    QPointer<QListWidget> library_;
    std::shared_ptr<const Song> song_;std::shared_ptr<const Conversion> result_;Settings settings_;
    double position_{};PerformanceController controller_;QTimer* timer_{};QCheckBox* activate_{};
    QWidget *playlistControls_{},*transportControls_{};
    bool running_{},libraryBusy_{},switchingSong_{},queueActive_{};
    std::vector<int> randomRemaining_,songHistory_;int historyCursor_{-1};
    void resetQueue();int nextSong(bool natural);void navigateSong(bool previous);void switchSong(int row,bool continuePlaying);
    void beginSong();void updateControls();void updatePerformance();void refreshDevices(bool);void discoveryFinished(bool);void showKeyTestWindow();
};
}
