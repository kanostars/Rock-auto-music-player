#include "performance_engine.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace rock {
Conversion retimePerformance(const Song& song,const Conversion& source,const Settings& settings) {
    if(song.ppq<=0||!std::isfinite(settings.bpm)||settings.bpm<=0||!std::isfinite(settings.speed)||settings.speed<=0)throw std::invalid_argument("演奏速度参数无效。");
    auto result=source;result.conflicts=0;
    auto time=[&](int tick){return (settings.fixedTempo?double(tick)/song.ppq*60/settings.bpm:song.secondsAt(tick))/settings.speed;};
    result.duration=time(song.endTick);std::map<std::pair<int,int>,std::vector<size_t>> groups;
    for(size_t i=0;i<result.notes.size();++i){auto& n=result.notes[i];n.start=time(n.startTick);n.duration=time(n.endTick)-n.start;n.conflict=false;
        if(n.target>=0&&n.target<9){result.duration=std::max(result.duration,n.start+n.duration);groups[{n.startTick,n.target}].push_back(i);}}
    std::array<double,9> last;last.fill(-1e100);
    for(const auto& [key,ids]:groups){double time=result.notes[ids.front()].start;if(time-last[key.second]+1e-9<(settings.holdMs+settings.gapMs)/1000.0){++result.conflicts;for(auto id:ids)result.notes[id].conflict=true;}last[key.second]=time;}
    return result;
}
PerformancePlan makePerformancePlan(const Conversion& result,double start,int holdMs,int gapMs) {
    if(!std::isfinite(start)||start<0||holdMs<1||holdMs>1000||gapMs<0||gapMs>1000)throw std::invalid_argument("演奏参数超出范围。");
    PerformancePlan plan;plan.start=std::min(start,result.duration);plan.duration=result.duration;plan.hold=holdMs/1000.0;plan.gap=gapMs/1000.0;
    std::map<double,uint16_t> strikes;
    for(const auto& note:result.notes)if(note.target>=0&&note.target<9&&note.start+1e-9>=plan.start){
        if(!std::isfinite(note.start)||note.start<0)throw std::invalid_argument("音符时间无效。");
        strikes[note.start]|=uint16_t(1<<note.target);
    }
    std::array<double,9> last;last.fill(-1e100);
    for(auto [time,mask]:strikes){
        for(int key=0;key<9;++key)if(mask&(1<<key)){if(time-last[key]+1e-9<plan.hold+plan.gap)throw std::invalid_argument("当前演奏参数下存在同键过密冲突，请降低 BPM 或缩短按下时长 / 松开间隔。");last[key]=time;}
        plan.strikes.push_back({time,mask});plan.duration=std::max(plan.duration,time+plan.hold);
    }
    if(plan.strikes.empty())throw std::invalid_argument("当前位置之后没有可演奏音符，请重新定位。");
    return plan;
}
PerformanceEngine::~PerformanceEngine(){stop();}
bool PerformanceEngine::releaseAll(double now) {
    if(!held_)return true;const auto mask=held_;bool ok=output_->send({0,mask});
    // Keep uncertain keys for another cleanup attempt on stop/destruction.
    if(ok){held_=0;for(int i=0;i<9;++i)if(mask&(1<<i))lastRelease_[i]=now;}
    return ok;
}
void PerformanceEngine::fail(const QString& reason,double now){const bool clean=releaseAll(now);if(clean)output_->close();state_.state=PerformanceState::Failed;state_.message=reason+(clean?QString():"；松键失败，请手动松开 B/F/G/H/J/K/T/Y/U。");}
bool PerformanceEngine::start(PerformancePlan plan,const OutputTarget& target,bool activate,double now) {
    if(!releaseAll(now)){fail("无法释放上次演奏按键",now);return false;}
    QString error;if(!output_||!output_->prepare(target,error)){if(output_)output_->close();state_.state=PerformanceState::Failed;state_.message=error;return false;}
    plan_=std::move(plan);next_=0;held_=0;lastRelease_.fill(-1e100);releaseAt_.fill(0);
    base_=state_.position=plan_.start;anchor_=now;deadline_=now+5;remaining_=5;pausedCountdown_=false;
    state_.state=PerformanceState::Countdown;state_.countdown=5;
    state_.message=activate&&!output_->activate()?"未能切到目标窗口，请在倒计时结束前手动切换。":"5 秒后开始演奏，请保持目标窗口在前台。";
    return true;
}
void PerformanceEngine::pause(double now,const QString& reason) {
    pausedCountdown_=state_.state==PerformanceState::Countdown;
    if(pausedCountdown_)remaining_=std::max(0.0,deadline_-now);
    if(!releaseAll(now)){fail("暂停时松键失败",now);return;}
    state_.state=PerformanceState::Paused;state_.message=reason;
}
void PerformanceEngine::togglePause(double now) {
    if(state_.state==PerformanceState::Playing||state_.state==PerformanceState::Countdown){pause(now,"已暂停 · Ctrl+Alt+Q 继续，Ctrl+Alt+E 终止");}
    else if(state_.state==PerformanceState::Paused){
        if(output_->targetStatus()!=TargetStatus::Ready){state_.message="请先手动切换到目标窗口，再按 Ctrl+Alt+Q 继续。";return;}
        anchor_=now;base_=state_.position;deadline_=now+remaining_;
        state_.state=pausedCountdown_?PerformanceState::Countdown:PerformanceState::Playing;state_.message="继续演奏";
    }
}
void PerformanceEngine::stop(){if(!output_)return;const bool clean=releaseAll(0);if(clean)output_->close();if(state_.active()||!clean){state_.state=clean?PerformanceState::Stopped:PerformanceState::Failed;state_.message=clean?"演奏已终止，已释放按键。":"松键失败，请手动松开 B/F/G/H/J/K/T/Y/U。";}}
void PerformanceEngine::tick(double now) {
    if(!state_.active())return;
    const auto target=output_->targetStatus();
    if(target==TargetStatus::Missing||target==TargetStatus::KeyboardMissing){fail(target==TargetStatus::Missing?"目标窗口已关闭，演奏终止。":"键盘已断开，演奏终止。",now);return;}
    if(state_.state==PerformanceState::Paused)return;
    if(state_.state==PerformanceState::Countdown){state_.countdown=std::max(0.0,deadline_-now);if(now<deadline_)return;
        state_.state=PerformanceState::Playing;anchor_=now;base_=state_.position;state_.message="正在演奏";}
    if(target!=TargetStatus::Ready){pause(now,"目标窗口不在前台，已暂停。切回目标后按 Ctrl+Alt+Q 继续。");return;}
    if(output_->modifiersHeld()){
        if(!releaseAll(now)){fail("松键失败",now);return;}anchor_=now;base_=state_.position;state_.message="等待松开 Ctrl / Alt / Shift / Win 后继续";return;
    }
    uint16_t up=0;for(int key=0;key<9;++key)if((held_&(1<<key))&&now>=releaseAt_[key])up|=uint16_t(1<<key);
    if(up){if(!output_->send({0,up})){fail("按键释放失败，演奏终止。",now);return;}held_&=~up;for(int key=0;key<9;++key)if(up&(1<<key))lastRelease_[key]=now;}
    state_.position=std::min(plan_.duration,base_+std::max(0.0,now-anchor_));
    if(next_<plan_.strikes.size()&&plan_.strikes[next_].time<=state_.position+1e-9){
        const auto strike=plan_.strikes[next_];
        if(state_.position-strike.time>.1){state_.position=strike.time;pause(now,"系统调度延迟过大，已暂停；按 Ctrl+Alt+Q 从当前音符继续。");return;}
        // Preflight already rejects score-level conflicts. A late dispatch/release can
        // still make a valid repeat arrive before the physical key is ready. Wait for
        // the full release gap and rebase the score clock, keeping this chord intact
        // and preventing later notes from catching up in a burst. This is not a pause.
        for(int key=0;key<9;++key)if((strike.keys&(1<<key))&&((held_&(1<<key))||now-lastRelease_[key]+1e-9<plan_.gap)){
            base_=state_.position=strike.time;anchor_=now;return;
        }
        held_|=strike.keys;for(int key=0;key<9;++key)if(strike.keys&(1<<key))releaseAt_[key]=now+plan_.hold;
        if(!output_->send({strike.keys,0})){fail("按键发送失败，演奏终止。",now);return;}++next_;state_.message="正在演奏 · Ctrl+Alt+Q 暂停，Ctrl+Alt+E 终止";
    }
    if(next_==plan_.strikes.size()&&!held_&&state_.position>=plan_.duration){output_->close();state_.state=PerformanceState::Finished;state_.message="演奏完成，已释放按键。";}
}
}
