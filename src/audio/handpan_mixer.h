#pragma once
#include "core/music.h"
#include <array>
#include <cstdint>
#include <vector>

namespace rock {
inline constexpr int audioRate=44100;
inline constexpr int sampleVariants=4;
// Each variant stores interleaved left/right samples at the original sample rate.
using SampleBank=std::array<std::array<std::vector<float>,sampleVariants>,9>;
struct AudioEvent {
    uint64_t start{}, release{}, end{};
    int target{},variant{};
    float gain{};
};
// Owned by the audio callback while running. Prepare/seek only with device stopped.
class HandpanMixer {
public:
    explicit HandpanMixer(SampleBank bank);
    void prepare(const Song& song,const Conversion& result);
    void seek(double seconds);
    void limitEnd(double seconds);
    void render(float* stereo,size_t frames,float volume);
    uint64_t cursor() const {return cursor_;}
    uint64_t endFrame() const {return endFrame_;}
private:
    SampleBank bank_;
    std::vector<AudioEvent> events_;
    std::array<const AudioEvent*,128> voices_{};
    size_t next_{};
    uint64_t cursor_{},endFrame_{},fadeStart_{};
    float volume_{.6f};
    void activate(const AudioEvent& event);
};
}
