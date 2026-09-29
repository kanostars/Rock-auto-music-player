#pragma once
#include "music.h"

namespace rock {
// Serialize the applied nine-key score; file dialogs and atomic writes belong to the UI.
std::string exportMidi(const Song& song,const Conversion& result,const Settings& settings);
}
