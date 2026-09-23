#pragma once
#include "core/music.h"
#include "platform/output_discovery.h"
#include "audio/audio_player.h"
#include "playback/performance_controller.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QWidget>
#include <memory>

class QLabel; class QComboBox; class QDoubleSpinBox; class QSpinBox; class QPushButton; class QDialog; class QCheckBox; class QTimer; class QHideEvent;
namespace rock {
class PianoRoll;
class PerformancePage : public QWidget {
    Q_OBJECT
public:
    explicit PerformancePage(QWidget* parent=nullptr,OutputDiscovery discovery={},AudioBackend backend=AudioBackend::System);
    ~PerformancePage() override;
    void stopPerformance();
    void inheritPreviewPosition(double seconds);
    void setSong(std::shared_ptr<const Song> song,std::shared_ptr<const Conversion> result,
                 const QString& path,const Settings& settings);
    void setPreviewPosition(double seconds);
    double previewPosition() const{return position_;}
signals:
    // Shared position expressed in the workbench's tempo, including fractional ticks.
    void sourcePositionChanged(double seconds);
protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
private:
    PianoRoll* preview_{};
    QLabel *title_{},*metadata_{},*clock_{},*empty_{},*state_{};
    QComboBox* tempoMode_{};
    QDoubleSpinBox* bpm_{};
    QSpinBox *hold_{},*gap_{},*countdown_{};
    OutputDiscovery discovery_;
    AudioBackend audioBackend_;
    QFutureWatcher<DiscoveryResult> keyboardWatcher_,windowWatcher_;
    QComboBox *keyboards_{},*windows_{};
    QPushButton *refreshKeyboards_{},*refreshWindows_{};
    QLabel *keyboardStatus_{},*windowStatus_{};
    QPointer<QDialog> keyTestWindow_;
    std::shared_ptr<const Song> song_;
    std::shared_ptr<const Conversion> result_;
    bool fitPending_{true};
    double position_{};
    PerformanceController controller_;
    Settings sourceSettings_,playSettings_;
    std::shared_ptr<const Conversion> sourceResult_;
    QPushButton *start_{},*stop_{},*testKeys_{};
    QCheckBox* activate_{};
    QTimer* timer_{};
    bool updating_{},running_{};
    void updateControls();
    void updatePerformance();
    void startPerformance();
    void retime();
    void refreshDevices(bool keyboards);
    void discoveryFinished(bool keyboards);
    void showKeyTestWindow();
};
}
