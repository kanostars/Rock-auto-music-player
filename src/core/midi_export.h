#pragma once
#include "music.h"

namespace rock {
// Serialize the applied nine-key score; file dialogs and atomic writes belong to the UI.
std::string exportMidi(const Song& song,const Conversion& result,const Settings& settings);
// Export [firstTick,lastTick), clipping crossing notes and rebasing the range to
// tick zero. Retain the tempo/meter active at its start and changes within it.
std::string exportMidiRange(const Song& song,const Conversion& result,const Settings& settings,
                           int firstTick,int lastTick);
}
