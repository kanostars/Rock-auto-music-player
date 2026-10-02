#include "performance_controller.h"
#include "platform/key_output.h"
#include <QCoreApplication>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace rock {
namespace {double now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
constexpr int pauseId=0x5241,stopId=0x5242;}
PerformanceController::PerformanceController():PerformanceController(createKeyOutput()) {}
PerformanceController::PerformanceController(std::unique_ptr<KeyOutput> output):engine_(std::move(output)) {QCoreApplication::instance()->installNativeEventFilter(this);}
PerformanceController::~PerformanceController(){stop();QCoreApplication::instance()->removeNativeEventFilter(this);}
void PerformanceController::unregisterHotkeys(){
#ifdef Q_OS_WIN
    if(registered_){UnregisterHotKey(nullptr,pauseId);UnregisterHotKey(nullptr,stopId);registered_=false;}
#endif
}
bool PerformanceController::start(PerformancePlan plan,const OutputTarget& target,bool activate,QString& error){
    stop();
#ifdef Q_OS_WIN
    if(!RegisterHotKey(nullptr,pauseId,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'Q')){error="Ctrl+Alt+Q 已被其他程序占用，无法开始演奏。";return false;}
    if(!RegisterHotKey(nullptr,stopId,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'E')){UnregisterHotKey(nullptr,pauseId);error="Ctrl+Alt+E 已被其他程序占用，无法开始演奏。";return false;}
    registered_=true;
    {std::lock_guard lock(mutex_);if(!engine_.start(std::move(plan),target,activate,now())){error=engine_.snapshot().message;unregisterHotkeys();return false;}}
    worker_=std::jthread([this](std::stop_token token){while(!token.stop_requested()){{std::lock_guard lock(mutex_);engine_.tick(now());if(!engine_.snapshot().active())break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}});
    return true;
#else
    error="自动演奏仅支持 Windows。";return false;
#endif
}
void PerformanceController::togglePause(){std::lock_guard lock(mutex_);engine_.togglePause(now());}
void PerformanceController::beginSeek(){std::lock_guard lock(mutex_);engine_.beginSeek(now());}
void PerformanceController::seek(PerformancePlan plan){std::lock_guard lock(mutex_);engine_.seek(std::move(plan),now());}
void PerformanceController::cancelSeek(const QString& reason){std::lock_guard lock(mutex_);engine_.cancelSeek(reason);}
void PerformanceController::stop(){if(worker_.joinable()){worker_.request_stop();worker_.join();}{std::lock_guard lock(mutex_);engine_.stop();}unregisterHotkeys();}
PerformanceSnapshot PerformanceController::snapshot(){std::lock_guard lock(mutex_);auto s=engine_.snapshot();if(!s.active())unregisterHotkeys();return s;}
bool PerformanceController::nativeEventFilter(const QByteArray&,void* message,qintptr* result){
#ifdef Q_OS_WIN
    auto* msg=static_cast<MSG*>(message);if(registered_&&msg&&msg->message==WM_HOTKEY){if(msg->wParam==pauseId)togglePause();else if(msg->wParam==stopId)stop();else return false;if(result)*result=0;return true;}
#endif
    return false;
}
}
