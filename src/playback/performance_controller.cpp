#include "performance_controller.h"
#include "platform/key_output.h"
#include "platform/global_shortcut.h"
#include "app/preferences.h"
namespace rock {
namespace {double now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
constexpr ShortcutAction performanceActions[]{ShortcutAction::PerformancePause,ShortcutAction::PerformanceStop};}
PerformanceController::PerformanceController():PerformanceController(createKeyOutput()) {}
PerformanceController::PerformanceController(std::unique_ptr<KeyOutput> output):engine_(std::move(output)) {
    for(size_t i=0;i<hotkeys_.size();++i)hotkeys_[i]=std::make_unique<GlobalShortcut>(0x5241+static_cast<int>(i));
    connect(hotkeys_[0].get(),&GlobalShortcut::activated,this,&PerformanceController::togglePause);
    connect(hotkeys_[1].get(),&GlobalShortcut::activated,this,&PerformanceController::stop);
}
PerformanceController::~PerformanceController(){stop();}
void PerformanceController::unregisterHotkeys(){
    for(auto& hotkey:hotkeys_)hotkey->disable();
}
bool PerformanceController::start(PerformancePlan plan,const OutputTarget& target,bool activate,QString& error){
    stop();
#ifdef Q_OS_WIN
    auto& preferences=Preferences::instance();
    for(size_t i=0;i<hotkeys_.size();++i)if(!hotkeys_[i]->enable(preferences.shortcut(performanceActions[i]),error)){unregisterHotkeys();return false;}
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
PerformanceSnapshot PerformanceController::snapshot(){std::lock_guard lock(mutex_);auto s=engine_.snapshot();if(!s.active())unregisterHotkeys();auto& p=Preferences::instance();s.message.replace("{performancePause}",p.shortcutText(ShortcutAction::PerformancePause));s.message.replace("{performanceStop}",p.shortcutText(ShortcutAction::PerformanceStop));return s;}
}
