#pragma once
#include <array>
#include <atomic>
#include <map>
#include <string>
#include <vector>

namespace rock {
inline constexpr std::array<int, 9> pitches{45, 52, 53, 55, 57, 59, 60, 62, 64};
inline constexpr std::array<char, 9> keys{'B', 'F', 'G', 'H', 'J', 'K', 'T', 'Y', 'U'};
struct Note {
    int track{}, pitch{}, velocity{}, start{}, end{};
    // Session-only additions keep stable source IDs. An addition is inactive
    // when it has no NoteEdit (e.g. after undo); it never affects octave fitting.
    bool added{};
    bool derived{}; // Split continuation: do not count twice in octave fitting.
};
struct Track { std::string name; int source{}, channel{}, count{}; };
struct Tempo { int tick{}, micros{500000}; double seconds{}; };
struct Song {
    int ppq{}, format{}, endTick{};
    std::vector<Track> tracks;
    std::vector<Note> notes;
    std::vector<Tempo> tempos;
    std::vector<std::string> warnings;
    double secondsAt(int tick) const;
};
struct Settings {
    bool nearest{true}, fixedTempo{false}, autoTranspose{true};
    double bpm{120}, speed{1};
    int holdMs{30}, gapMs{20};
    std::vector<bool> enabled, solo;
};
// Overrides are stored in source ticks so tempo/speed changes also scale edits.
// Source IDs remain stable; deleting a note never erases the original MIDI data.
struct NoteEdit {
    int startTick{}, endTick{}, target{-1};
    bool deleted{};
    bool skipped{}; // Explicitly skip a conflicting note, retaining it in the score/report.
    bool operator==(const NoteEdit&) const = default;
};
using NoteEdits = std::map<int, NoteEdit>;
enum class Mapping { Exact, Approximate, Skipped, Excluded, Edited, Deleted, ConflictSkipped };
struct MappedNote {
    int source{}, target{-1};
    Mapping mapping{};
    double start{}, duration{};
    bool conflict{};
    int startTick{}, endTick{};
};
struct Conversion {
    std::vector<MappedNote> notes;
    int transpose{}; // Shared chromatic transposition in [-12, 12], before octave folding.
    int octaveFolded{}; // Subset of exact: same pitch class, moved to another octave.
    int exact{}, approximate{}, skipped{}, excluded{}, merged{}, conflicts{}, chords{}; // exact includes same-name octave folds.
    int edited{}, deleted{};
    double duration{};
};
Song parseMidi(const std::string& bytes, const std::atomic_bool* cancel = nullptr);
Settings defaultSettings(const Song& song);
Conversion convert(const Song& song, const Settings& settings, const NoteEdits& edits = {});
NoteEdits resolveSameKeyConflicts(const Conversion& result,const Settings& settings,bool remove);
int tickAtSeconds(const Song& song, const Settings& settings, double seconds);
int nearestTarget(int pitch);
double secondsAtTick(const Song&,const Settings&,int tick);
void deleteTimeRange(Song&,NoteEdits&,int first,int last);
void insertBlankRange(Song&,NoteEdits&,int first,int last);
Conversion playbackRange(const Conversion&,double start,double end,const Settings&);
std::string pitchName(int pitch);
}
