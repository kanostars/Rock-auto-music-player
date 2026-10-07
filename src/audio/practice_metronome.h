#pragma once
#include "core/music.h"
#include <QString>
#include <memory>
#include <utility>
#include <vector>

namespace rock {
class AudioPlayer;
struct MetronomeBeat { double seconds{}; bool accent{}; };
// Times use the converted score's clock. The meter denominator defines one click.
// Throws when timing data is invalid or the plan would exceed 500,000 clicks.
std::vector<MetronomeBeat> makeMetronomeBeats(const Song&,const Settings&,double endSeconds);
std::pair<double,int> metronomeRhythmAt(const Song&,const Settings&,double seconds);

class MetronomePlayer {
public:
    MetronomePlayer();
    ~MetronomePlayer();
    // Optional score clock must outlive the running metronome. Old beats are never replayed after a clock jump.
    bool play(const std::vector<MetronomeBeat>&,double seconds,double end,double speed,QString& error,const AudioPlayer* clock=nullptr);
    bool start(double bpm,int beatsPerBar,QString& error);
    // Keeps the current beat phase; a changed meter starts a new accent cycle.
    void setRhythm(double bpm,int beatsPerBar);
    void setVolume(float);
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
