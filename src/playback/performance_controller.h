#pragma once
#include "performance_engine.h"
#include <QAbstractNativeEventFilter>
#include <mutex>
#include <thread>
namespace rock {
class PerformanceController:public QAbstractNativeEventFilter {
public:
    PerformanceController();
    explicit PerformanceController(std::unique_ptr<KeyOutput> output);
    ~PerformanceController();
    bool start(PerformancePlan,const OutputTarget&,bool activate,QString& error);
    void togglePause();
    void stop();
    PerformanceSnapshot snapshot();
    bool nativeEventFilter(const QByteArray&,void*,qintptr*) override;
private:
    std::mutex mutex_;PerformanceEngine engine_;std::jthread worker_;bool registered_{};
    void unregisterHotkeys();
};
}
