#pragma once
#include "core/practice.h"
#include <QDialog>
#include <array>

class QCheckBox;
class QDoubleSpinBox;
class QKeySequenceEdit;
class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace rock {
struct PracticeOptions {
    bool loopEnabled{false};
    double loopStart{},loopEnd{};
    bool metronomeEnabled{false},followScore{true};
    double metronomeBpm{120};
    int beatsPerBar{4};
    std::array<int,9> inputKeys{keys[0],keys[1],keys[2],keys[3],keys[4],keys[5],keys[6],keys[7],keys[8]};
};

class PracticeSettingsDialog final:public QDialog {
public:
    PracticeSettingsDialog(const PracticeOptions& options,double duration,double position,
                           const std::vector<PracticeGroup>& groups,QWidget* parent=nullptr);
    PracticeOptions options() const;
    void accept() override;
private:
    double duration_{},position_{};
    std::vector<PracticeGroup> groups_;
    QTabWidget* tabs_{};
    QCheckBox *loop_{},*metronome_{},*follow_{};
    QDoubleSpinBox *start_{},*end_{},*bpm_{};
    QSpinBox* beats_{};
    QPushButton *setStart_{},*setEnd_{};
    std::array<QKeySequenceEdit*,9> keyEdits_{};
    QLabel* error_{};
    void updateEnabled();
    void showError(const QString& error,int tab,QWidget* field=nullptr);
};
}
