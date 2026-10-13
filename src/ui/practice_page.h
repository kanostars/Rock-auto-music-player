#pragma once
#include "audio/audio_player.h"
#include "audio/live_handpan.h"
#include "audio/practice_metronome.h"
#include "core/practice.h"
#include "platform/practice_input.h"
#include "practice_settings_dialog.h"
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QLabel;class QPushButton;class QComboBox;class QSlider;class QGridLayout;class QDialog;class QStackedWidget;class QSpinBox;class QScreen;
namespace rock {
class PianoRoll;class HandpanBoard;class TimeSeekEdit;class NumberScoreView;class PracticeMini;
class PracticePage:public QWidget {
    Q_OBJECT
public:
    explicit PracticePage(QWidget* parent=nullptr);
    ~PracticePage() override;
    void setSong(const QString& name,std::shared_ptr<const Song> song,
                 std::shared_ptr<const Conversion> result,const Settings& settings);
    void setVolume(int value);
    void stop();
    double position() const{return position_;}
    QString pageTitle() const{return pageTitle_;}
    QString pageSubtitle() const{return pageSubtitle_;}
    bool miniMode() const{return miniMode_;}
    void enterMiniMode(QScreen* preferred=nullptr);
    void leaveMiniMode();
signals:
    void miniModeChanged(bool enabled);
    void miniRestoreRequested();
protected:
    bool eventFilter(QObject*,QEvent*) override;
    void hideEvent(QHideEvent*) override;
private:
    std::shared_ptr<const Song> song_;
    std::shared_ptr<const Conversion> result_;
    Settings settings_;
    PracticeOptions options_;
    GroupPractice practice_;
    AudioPlayer audio_;
    LiveHandpanPlayer live_;
    MetronomePlayer metronome_;
    PracticeInput miniInput_;
    PracticeMini* mini_{};
    bool miniMode_{};
    bool miniSeeking_{},miniSeekResume_{};
    int miniPreviewVolume_{60},miniFollowingVolume_{};
    std::vector<MetronomeBeat> metronomeBeats_;
    bool metronomePlanReady_{};
    QTimer timer_;
    PianoRoll* timeline_{};HandpanBoard* board_{};
    NumberScoreView* numberScore_{};QStackedWidget* scoreViews_{};
    QPushButton *timelineMode_{},*numberMode_{},*downloadScore_{},*previousPage_{},*nextPage_{},*followPage_{};
    QSpinBox* scorePage_{};QLabel* scoreDisplayHint_{};
    bool scoreReady_{},scoreFollow_{true},exportingScore_{};
    QString songName_;
    TimeSeekEdit* clock_{};
    QLabel *zoom_{},*promptTitle_{},*groupCount_{},*hint_{};
    QString pageTitle_,pageSubtitle_;
    QPushButton *play_{},*automatic_{},*following_{},*miniButton_{};
    QComboBox* speed_{};QSlider *progress_{},*volume_{};
    QGridLayout* chipsLayout_{};
    std::array<QLabel*,9> chips_{};
    std::array<bool,9> keyboardHeld_{},mouseHeld_{};
    QPointer<QDialog> settingsDialog_;
    bool followingMode_{},running_{},seekResume_{};
    double position_{};
    uint16_t shownMask_{0xffff};
    QString feedback_;
    void setMode(bool following);
    void start();
    void restart();
    void pause();
    void applyVolume();
    int outputVolume() const;
    void beginMiniSeek();
    void finishMiniSeek(double seconds);
    bool startMiniInput();
    void seek(double seconds,bool resume=true);
    void tick();
    void updatePosition(double seconds,bool focusScore=false);
    void updateScorePosition(bool forceFocus=false);
    void updatePrompt();
    void updatePlayButton();
    void press(int target,bool mouse);
    void release(int target,bool mouse);
    void clearInput();
    void openSettings();
    void applyOptions(const PracticeOptions& options);
    void updateKeyLabels();
    void setScoreMode(bool numbers);
    void browseScorePage(int page);
    void refreshScorePages(int page,int count);
    void downloadScore(bool allPages);
    void loadPreferences();
    void startMetronome();
    void updateMetronomeRhythm();
    void alignFollowingGroup();
    double rangeStart() const;
    double rangeEnd() const;
    std::pair<size_t,size_t> groupRange() const;
    bool acceptsInput() const;
};
}
