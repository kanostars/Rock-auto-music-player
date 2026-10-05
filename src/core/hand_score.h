#pragma once
#include "music.h"

namespace rock {
// Plain-text playing reference, grouped by quarter-note beats in source ticks.
std::string exportHandScore(const Song& song,const Conversion& result,const Settings& settings);
// KEY:beats gives the wait from this strike to the next, at the header BPM.
std::string exportKeyScore(const Song& song,const Conversion& result,const Settings& settings);
}
