#include "handpan_mixer.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace rock {
namespace {
constexpr uint64_t releaseFrames=audioRate*35/100;
uint64_t frame(double seconds) {
    if(!std::isfinite(seconds)||seconds<0||seconds>1e12)
        throw std::invalid_argument("音频时间超出支持范围。");
    return static_cast<uint64_t>(std::llround(seconds*audioRate));
}
}
HandpanMixer::HandpanMixer(SampleBank bank):bank_(std::move(bank)) {
    for(const auto& variants:bank_)for(const auto& sample:variants)
        if(sample.empty()||sample.size()%2)throw std::invalid_argument("手碟立体声采样不完整。");
}
void HandpanMixer::prepare(const Song& song,const Conversion& result) {
    events_.clear();voices_.fill(nullptr);next_=0;cursor_=0;
    endFrame_=frame(result.duration);
    // Same tick + same key is one strike, with longest duration / greatest velocity.
    std::map<std::pair<int,int>,AudioEvent> merged;
    for(const auto& note:result.notes) {
        if(note.target<0||note.target>=9)continue;
        const auto& source=song.notes.at(note.source);
        AudioEvent e;e.start=frame(note.start);e.release=std::max(e.start+1,frame(note.start+note.duration));
        e.target=note.target;e.gain=std::clamp(source.velocity/127.f,0.f,1.f);
        auto [it,inserted]=merged.emplace(std::pair{note.startTick,note.target},e);
        if(!inserted){it->second.release=std::max(it->second.release,e.release);it->second.gain=std::max(it->second.gain,e.gain);}
    }
    for(auto [key,e]:merged)events_.push_back(e);
    std::stable_sort(events_.begin(),events_.end(),[](const auto& a,const auto& b){return a.start<b.start;});
    // Choose once per merged strike, in timeline order. Rebuilding the same song
    // on pause/resume or seek preserves its variants and therefore its tails.
    std::array<int,9> nextVariant{};
    for(auto& e:events_) {
        e.variant=nextVariant[e.target]++%sampleVariants;
        e.end=std::min(e.start+bank_[e.target][e.variant].size()/2,e.release+releaseFrames);
        endFrame_=std::max(endFrame_,e.end);
    }
    seek(0);
}
void HandpanMixer::activate(const AudioEvent& event) {
    auto it=std::find(voices_.begin(),voices_.end(),nullptr);
    if(it==voices_.end())it=std::min_element(voices_.begin(),voices_.end(),[](const auto* a,const auto* b){return a->end<b->end;});
    *it=&event;
}
void HandpanMixer::seek(double seconds) {
    cursor_=std::min(frame(seconds),endFrame_);fadeStart_=cursor_;voices_.fill(nullptr);next_=0;
    while(next_<events_.size()&&events_[next_].start<cursor_) {
        if(events_[next_].end>cursor_)activate(events_[next_]);
        ++next_;
    }
}
void HandpanMixer::render(float* stereo,size_t frames,float volume) {
    std::fill_n(stereo,frames*2,0.f);
    uint64_t blockEnd=std::min(cursor_+frames,endFrame_);
    for(auto& voice:voices_)if(voice&&voice->end<=cursor_)voice=nullptr;
    while(next_<events_.size()&&events_[next_].start<blockEnd)activate(events_[next_++]);
    for(auto& voice:voices_)if(voice) {
        const auto& sample=bank_[voice->target][voice->variant];
        for(uint64_t at=std::max(cursor_,voice->start);at<std::min(blockEnd,voice->end);++at) {
            float envelope=1;
            if(at>=voice->release) {
                envelope=1-float(at-voice->release)/releaseFrames;envelope*=envelope;
            }
            for(size_t channel=0;channel<2;++channel)
                stereo[(at-cursor_)*2+channel]+=sample[(at-voice->start)*2+channel]*voice->gain*envelope;
        }
        if(voice->end<=blockEnd)voice=nullptr;
    }
    float targetVolume=std::isfinite(volume)?std::clamp(volume,0.f,1.f):0.f;
    for(size_t i=0;i<frames;++i) {
        volume_+=(targetVolume-volume_)*.004f;
        float fade=std::min(1.f,float(cursor_+i-fadeStart_)/240.f);
        for(size_t channel=0;channel<2;++channel)
            stereo[i*2+channel]=std::tanh(stereo[i*2+channel]*volume_)*fade;
    }
    cursor_=blockEnd;
}
}
