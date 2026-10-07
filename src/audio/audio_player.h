#pragma once
#include "handpan_mixer.h"
#include <QString>
#include <memory>

namespace rock {
SampleBank loadHandpanBank();
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();
    // Positions remain in the conversion's timeline; speed changes strike timing, not sample pitch.
    bool play(const Song& song,const Conversion& result,double seconds,QString& error,double end=-1,double speed=1,bool allowConflicts=false);
    void pause();
    void setVolume(float volume);
    double position() const;
    bool finished() const;
    bool running() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
