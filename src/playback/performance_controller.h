#pragma once
#include "performance_engine.h"
#include "platform/global_shortcut.h"
#include <mutex>
#include <thread>
namespace rock {
class PerformanceController:public QObject {
    Q_OBJECT
public:
    PerformanceController();
    explicit PerformanceController(std::unique_ptr<KeyOutput> output);
    ~PerformanceController();
    bool start(PerformancePlan,const OutputTarget&,bool activate,QString& error);
    void togglePause();
    void beginSeek();
    void seek(PerformancePlan);
    void cancelSeek(const QString& reason="定位已取消，演奏保持暂停。");
    void stop();
    PerformanceSnapshot snapshot();
private:
    std::mutex mutex_;PerformanceEngine engine_;std::jthread worker_;
    std::array<std::unique_ptr<GlobalShortcut>,2> hotkeys_;
    void unregisterHotkeys();
};
}
