#pragma once
#include "core/music.h"
#include <QString>
#include <memory>
#include <cstdint>

namespace rock {
struct OutputTarget {QString keyboard;quint64 window{};quint32 process{};};
struct KeyBatch {uint16_t down{},up{};};
enum class TargetStatus {Ready,NotForeground,Missing,KeyboardMissing};
class KeyOutput {
public:
    virtual ~KeyOutput()=default;
    virtual bool prepare(const OutputTarget&,QString& error)=0;
    virtual bool activate()=0;
    virtual TargetStatus targetStatus()=0;
    virtual bool modifiersHeld()=0;
    virtual bool send(KeyBatch)=0;
    virtual void close(){}
};
struct Strike {double time{};uint16_t keys{};};
struct PerformancePlan {std::vector<Strike> strikes;double duration{},start{},hold{},gap{};int countdownSeconds{5};bool bounded{};};
PerformancePlan makePerformancePlan(const Conversion&,double start,int holdMs,int gapMs,double end=-1);
enum class PerformanceState {Idle,Countdown,Playing,Paused,Finished,Stopped,Failed};
struct PerformanceSnapshot {
    PerformanceState state{PerformanceState::Idle};double position{},countdown{};QString message;
    bool active() const{return state==PerformanceState::Countdown||state==PerformanceState::Playing||state==PerformanceState::Paused;}
};
// All methods are serialized by the controller. Explicit monotonic time permits deterministic tests.
class PerformanceEngine {
public:
    explicit PerformanceEngine(std::unique_ptr<KeyOutput> output):output_(std::move(output)){}
    ~PerformanceEngine();
    bool start(PerformancePlan,const OutputTarget&,bool activate,double now);
    void tick(double now);
    void togglePause(double now);
    void stop();
    PerformanceSnapshot snapshot() const{return state_;}
private:
    std::unique_ptr<KeyOutput> output_;PerformancePlan plan_;PerformanceSnapshot state_;
    size_t next_{};uint16_t held_{};
    std::array<double,9> releaseAt_{},lastRelease_{};
    double anchor_{},base_{},deadline_{},remaining_{};bool pausedCountdown_{};
    bool releaseAll(double now);
    void pause(double now,const QString& reason);
    void fail(const QString& reason,double now);
};
}
