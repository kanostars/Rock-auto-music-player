#pragma once
#include "music.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rock {
struct PracticeGroup {
    int tick{};
    double start{},end{};
    std::uint16_t keys{};
};
std::vector<PracticeGroup> makePracticeGroups(const Conversion& result);

enum class PracticePress { Ignored, Wrong, Partial, Advanced, Finished };
class GroupPractice {
public:
    void setGroups(std::vector<PracticeGroup> groups);
    // Start at the first group whose onset is at or after this position.
    void seek(double seconds);
    // Wrap a completed loop without allowing held keys to count twice.
    void loopTo(double seconds);
    void restart();
    const std::vector<PracticeGroup>& groups() const { return groups_; }
    std::size_t index() const { return index_; }
    bool finished() const { return index_>=groups_.size(); }
    std::uint16_t matchedMask() const { return matched_; }
    bool waitingRelease() const { return release_!=0; }
    PracticePress press(int target);
    void release(int target);
    // Use when pausing or losing keyboard focus to prevent stuck keys.
    void clearHeld();
private:
    std::vector<PracticeGroup> groups_;
    std::size_t index_{};
    std::uint16_t held_{},matched_{},release_{};
};
}
