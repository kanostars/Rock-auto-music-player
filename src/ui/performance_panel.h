#pragma once
#include "core/music.h"
#include "platform/output_discovery.h"
#include "playback/performance_controller.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QWidget>
#include <memory>
class QLabel;
class QComboBox;
class QSpinBox;
class QPushButton;
class QCheckBox;
class QTimer;
class QListWidget;

namespace rock {
    // Output controls and playlist scheduling; the workbench owns the only score and timeline.
    class PerformancePanel : public QWidget {
        Q_OBJECT

    public:
        explicit PerformancePanel(QWidget *parent = nullptr);

        ~PerformancePanel() override;

        void stopPerformance();

        void startPerformance();

        void beginSeek();

        void seekPerformance(double seconds);

        bool active() const { return running_; }

        bool outputReady() const;

        QString statusText() const;

        PerformanceSnapshot snapshot() const { return snapshot_; }
        int playMode() const { return playModeIndex_; }

        bool canChangePlayMode() const {
            return !libraryBusy_ && (!running_ || snapshot_.state == PerformanceState::Paused);
        }

        void cyclePlayMode();
        int countdown() const;
        bool activatesTarget() const;
        void setStartOptions(int countdown,bool activateTarget);

        void navigateSong(bool previous);

        void setSong(std::shared_ptr<const Song>, std::shared_ptr<const Conversion>, const QString &path,
                     const Settings &);

        void setPreviewPosition(double seconds);

        void setPlaybackRange(double first, double last) {
            rangeStart_ = first;
            rangeEnd_ = last;
        }

        void setLibrary(QListWidget *);

        void setLibraryBusy(bool);

        void setPreviewPlaying(bool playing) { previewPlaying_ = playing; }

        void previewFinished();

        QWidget *playlistControls() const { return playlistControls_; }
        QWidget *transportControls() const { return transportControls_; }
    signals:
        void startRequested();

        void targetActivationRequested();

        void previewStartRequested();

        void activeChanged(bool active);

        void statusChanged(const QString &text);

        void sourcePositionChanged(double seconds);

        void songChangeRequested(int row);

        void songRemoveRequested(int row);

        void songMoveRequested(int from, int to);

    private:
        QLabel *state_{}, *keyboardStatus_{}, *windowStatus_{};
        QSpinBox *countdown_{};
        QFutureWatcher<DiscoveryResult> keyboardWatcher_, windowWatcher_;
        QComboBox *keyboards_{}, *windows_{};
        QPushButton *playMode_{};
        int playModeIndex_{3}; // Sequence, repeat one, shuffle, play once (default).
        void updatePlayMode();

        QPushButton *refreshKeyboards_{}, *refreshWindows_{}, *start_{};
        QLabel* shortcutHint_{};
        QPushButton *previousSong_{}, *nextSong_{}, *removeSong_{}, *moveUp_{}, *moveDown_{};
        QPointer<QListWidget> library_;
        std::shared_ptr<const Song> song_;
        std::shared_ptr<const Conversion> result_;
        Settings settings_;
        double position_{};
        PerformanceController controller_;
        QTimer *timer_{};
        QCheckBox *activate_{};
        double rangeStart_{}, rangeEnd_{-1};
        QWidget *playlistControls_{}, *transportControls_{};
        bool running_{}, libraryBusy_{}, switchingSong_{}, queueActive_{}, previewPlaying_{};
        PerformanceSnapshot snapshot_;
        std::vector<int> randomRemaining_, songHistory_;
        int historyCursor_{-1};

        void resetQueue();

        int nextSong(bool natural);

        void switchSong(int row, bool continuePlaying, bool preview = false);

        void beginSong();

        void updateControls();

        void updatePerformance();

        void refreshDevices(bool);

        void discoveryFinished(bool);

    };
}
