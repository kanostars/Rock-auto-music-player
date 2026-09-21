#pragma once
#include "handpan_mixer.h"
#include <QString>
#include <memory>

namespace rock {
enum class AudioBackend { System, NullTest };
SampleBank loadHandpanBank();
class AudioPlayer {
public:
    explicit AudioPlayer(AudioBackend backend=AudioBackend::System);
    ~AudioPlayer();
    bool play(const Song& song,const Conversion& result,double seconds,QString& error);
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
