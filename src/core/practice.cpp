#include "practice.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace rock {
namespace {
constexpr std::uint16_t allKeys=(1u<<keys.size())-1;
std::uint16_t targetBit(int target){
    return target>=0&&target<static_cast<int>(keys.size())?
        static_cast<std::uint16_t>(1u<<target):0;
}
bool playable(Mapping mapping){
    return mapping==Mapping::Exact||mapping==Mapping::Approximate||mapping==Mapping::Edited;
}
}
std::vector<PracticeGroup> makePracticeGroups(const Conversion& result){
    std::map<int,PracticeGroup> byTick;
    for(const auto& note:result.notes){
        const auto bit=targetBit(note.target);
        const double end=note.start+note.duration;
        if(!bit||!playable(note.mapping)||note.startTick<0||note.endTick<=note.startTick||
           !std::isfinite(note.start)||!std::isfinite(end)||note.start<0||end<=note.start)continue;
        const auto [it,inserted]=byTick.try_emplace(note.startTick,
            PracticeGroup{note.startTick,note.start,end,bit});
        if(!inserted){
            it->second.start=std::min(it->second.start,note.start);
            it->second.end=std::max(it->second.end,end);
            it->second.keys|=bit;
        }
    }
    std::vector<PracticeGroup> groups;groups.reserve(byTick.size());
    for(const auto& [tick,group]:byTick)groups.push_back(group);
    return groups;
}

void GroupPractice::setGroups(std::vector<PracticeGroup> groups){
    for(auto& group:groups)group.keys&=allKeys;
    std::erase_if(groups,[](const PracticeGroup& group){
        return !group.keys||group.tick<0||!std::isfinite(group.start)||
            !std::isfinite(group.end)||group.start<0||group.end<=group.start;
    });
    std::stable_sort(groups.begin(),groups.end(),[](const PracticeGroup& a,const PracticeGroup& b){
        return a.start!=b.start?a.start<b.start:a.tick<b.tick;
    });
    groups_=std::move(groups);restart();
}
void GroupPractice::seek(double seconds){
    loopTo(seconds);clearHeld();
}
void GroupPractice::loopTo(double seconds){
    if(std::isnan(seconds))seconds=0;
    index_=static_cast<std::size_t>(std::lower_bound(groups_.begin(),groups_.end(),seconds,
        [](const PracticeGroup& group,double time){return group.start<time;})-groups_.begin());
    matched_=0;
}
void GroupPractice::restart(){index_=0;clearHeld();}
PracticePress GroupPractice::press(int target){
    const auto bit=targetBit(target);
    if(!bit||finished()||(held_&bit))return PracticePress::Ignored;
    held_|=bit;
    // Presses made before the previous chord is released must be played again.
    if(waitingRelease())return PracticePress::Ignored;
    const auto expected=groups_[index_].keys;
    if(!(expected&bit))return PracticePress::Wrong;
    matched_|=bit;
    if(matched_!=expected)return PracticePress::Partial;
    release_=expected;matched_=0;++index_;
    return finished()?PracticePress::Finished:PracticePress::Advanced;
}
void GroupPractice::release(int target){
    const auto bit=targetBit(target);
    held_&=static_cast<std::uint16_t>(~bit);
    matched_&=static_cast<std::uint16_t>(~bit);
    release_&=static_cast<std::uint16_t>(~bit);
}
void GroupPractice::clearHeld(){held_=0;matched_=0;release_=0;}
}
