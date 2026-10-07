#pragma once
#include "audio_player.h"
#include <atomic>

namespace rock {
// Main/UI thread enqueues strikes; only the audio callback renders voices.
class LiveHandpanMixer {
public:
    explicit LiveHandpanMixer(SampleBank bank);
    bool strike(int target);
    void render(float* stereo,size_t frames,float volume=.6f);
    void clear(); // Only after the playback device has stopped.
private:
    SampleBank bank_;
    struct Voice {const std::vector<float>* sample{};size_t frame{};};
    std::array<Voice,128> voices_{};
    std::array<int,9> variants_{};
    std::array<int,256> queue_{};
    std::atomic<unsigned> read_{},write_{};
};
class LiveHandpanPlayer {
public:
    LiveHandpanPlayer();
    ~LiveHandpanPlayer();
    bool start(QString& error);
    bool strike(int target);
    void setVolume(float volume);
    void stop();
    bool running() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
