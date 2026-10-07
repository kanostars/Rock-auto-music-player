#pragma once
#include "music.h"
#include <cstdint>
#include <utility>

namespace rock {
// Layout is quantized to sixteenth notes. Source playback times are untouched.
struct NumberScoreEvent {
    double startTick{}, endTick{}, sourceSeconds{};
    int units{}; // Sixteenth notes: 1 / 2 / 3 / 4 / 6.
    std::uint16_t keys{}; // Zero is a rest; other bits index rock::keys.
    bool tieIn{}, tieOut{}, extension{};
};
struct NumberScoreMeasure {
    int number{}, numerator{4}, denominator{4};
    double startTick{}, endTick{}, startSeconds{}, endSeconds{}, bpm{120};
    // Global voice indices are stable, including voices resting in this bar.
    std::vector<std::vector<NumberScoreEvent>> voices;
};
struct NumberScore {
    std::vector<NumberScoreMeasure> measures;
    int voiceCount{};
    double bpm{120};
};
// Digit and octave shift relative to unmarked 1 = C3 (MIDI pitch 48).
std::pair<int,int> numberDegree(int target);
NumberScore makeNumberScore(const Song&, const Conversion&, const Settings&);
}
