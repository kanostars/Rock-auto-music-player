#pragma once
#include "core/music.h"
#include "platform/output_discovery.h"
#include "audio/audio_player.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QWidget>
#include <memory>

class QLabel; class QComboBox; class QDoubleSpinBox; class QSpinBox; class QPushButton; class QDialog;
namespace rock {
class PianoRoll;
// Device/window discovery is live; key dispatch is not connected yet.
class PerformancePage : public QWidget {
public:
    explicit PerformancePage(QWidget* parent=nullptr,OutputDiscovery discovery={},AudioBackend backend=AudioBackend::System);
    void setSong(std::shared_ptr<const Song> song,std::shared_ptr<const Conversion> result,
                 const QString& path,const Settings& settings);
    void setPreviewPosition(double seconds);
    double previewPosition() const{return position_;}
protected:
    void showEvent(QShowEvent* event) override;
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
    void refreshDevices(bool keyboards);
    void discoveryFinished(bool keyboards);
    void showKeyTestWindow();
};
}
