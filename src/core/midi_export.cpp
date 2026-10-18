#include "midi_export.h"
#include <MidiFile.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace rock {
namespace {
struct ExportRange { int first{},last{}; };
std::string serializeMidi(const Song& song,const Conversion& result,const Settings& settings,
                          std::optional<ExportRange> range){
    if(song.ppq<1||song.ppq>32767||!std::isfinite(settings.speed)||settings.speed<=0||
       !std::isfinite(settings.bpm)||settings.bpm<=0)
        throw std::invalid_argument("无法导出：曲目节奏参数无效。");
    struct Strike {int end{},velocity{};};
    std::map<std::pair<int,int>,Strike> strikes;
    int endTick=range?range->last-range->first:song.endTick;
    for(const auto& note:result.notes){
        if(note.target<0)continue;
        if(note.target>=9||note.source<0||note.source>=static_cast<int>(song.notes.size())||
           note.startTick<0||note.endTick<=note.startTick)
            throw std::invalid_argument("无法导出：音符音高或时间无效。");
        int start=note.startTick,end=note.endTick;
        if(range){
            if(end<=range->first||start>=range->last)continue;
            start=std::max(start,range->first)-range->first;
            end=std::min(end,range->last)-range->first;
        }
        auto& strike=strikes[{start,note.target}];
        strike.end=std::max(strike.end,end);
        strike.velocity=std::max(strike.velocity,std::clamp(song.notes[note.source].velocity,1,127));
        endTick=std::max(endTick,end);
    }
    if(strikes.empty())throw std::invalid_argument(range?"所选区域没有可导出的九键音符。":"当前曲目没有可导出的九键音符。");

    smf::MidiFile midi;midi.setTPQ(song.ppq);midi.addTrackName(0,0,"Nine-key handpan");
    auto addTempo=[&](int tick,double micros){
        const double scaled=std::round(micros/settings.speed);
        if(tick<0||!std::isfinite(scaled)||scaled<1||scaled>0xffffff)
            throw std::invalid_argument("当前速度超出 MIDI 可保存范围，请调整播放倍率后重试。");
        const auto value=static_cast<int>(scaled);
        std::vector<unsigned char> data{static_cast<unsigned char>(value>>16),static_cast<unsigned char>(value>>8),static_cast<unsigned char>(value)};
        midi.addMetaEvent(0,tick,0x51,data);
    };
    if(settings.fixedTempo)addTempo(0,60000000.0/settings.bpm);
    else if(range){
        int active=500000;
        for(const auto& tempo:song.tempos)if(tempo.tick<=range->first)active=tempo.micros;
        addTempo(0,active);
        for(const auto& tempo:song.tempos)
            if(tempo.tick>range->first&&tempo.tick<range->last)addTempo(tempo.tick-range->first,tempo.micros);
    }
    else {
        if(song.tempos.empty()||song.tempos.front().tick>0)addTempo(0,500000);
        for(const auto& tempo:song.tempos)if(tempo.tick<=endTick)addTempo(tempo.tick,tempo.micros);
    }
    auto addSignature=[&](int tick,int numerator,int denominator){
        unsigned power=0;for(auto value=denominator;value>1;value/=2)++power;
        std::vector<unsigned char> data{static_cast<unsigned char>(numerator),static_cast<unsigned char>(power),24,8};
        midi.addMetaEvent(0,tick,0x58,data);
    };
    TimeSignature activeSignature{0,4,4};
    for(const auto& signature:song.timeSignatures){
        if(signature.tick<0||signature.numerator<1||signature.numerator>255||signature.denominator<1||
           (signature.denominator&(signature.denominator-1)))throw std::invalid_argument("无法导出：曲目拍号无效。");
        if(range){
            if(signature.tick<=range->first)activeSignature=signature;
            else if(signature.tick<range->last)addSignature(signature.tick-range->first,signature.numerator,signature.denominator);
        }else if(signature.tick<=endTick)addSignature(signature.tick,signature.numerator,signature.denominator);
    }
    if(range)addSignature(0,activeSignature.numerator,activeSignature.denominator);
    // Separate overlapping strikes of the same pitch onto melodic channels. This
    // preserves their individual releases (including nested notes) on re-import.
    std::array<std::array<int,16>,9> releaseTicks{};
    for(const auto& [key,strike]:strikes){
        const auto [start,target]=key;int channel=-1;
        for(int candidate=0;candidate<16;++candidate)
            if(candidate!=9&&releaseTicks[target][candidate]<=start){channel=candidate;break;}
        if(channel<0)throw std::invalid_argument("同一音高同时重叠超过 15 个音符，请缩短重叠音符后再导出。");
        releaseTicks[target][channel]=strike.end;
        midi.addNoteOn(0,start,channel,pitches[target],strike.velocity);
        midi.addNoteOff(0,strike.end,channel,pitches[target]);
    }
    midi.addMetaEvent(0,endTick,0x2f,std::string{});
    midi.sortTracksNoteOffsBeforeOns();
    // MIDI delta times use at most four bytes. Reject rather than writing a file
    // that the importer (or another player) cannot read.
    int previous=0;
    for(int i=0;i<midi[0].size();++i){
        if(midi[0][i].tick-previous>0x0fffffff)
            throw std::invalid_argument("音符之间的空白时间过长，超出 MIDI 导出范围。");
        previous=midi[0][i].tick;
    }
    std::ostringstream stream(std::ios::binary);
    if(!midi.write(stream)||!stream)throw std::runtime_error("MIDI 编码失败。");
    return stream.str();
}
}
std::string exportMidi(const Song& song,const Conversion& result,const Settings& settings){
    return serializeMidi(song,result,settings,std::nullopt);
}
std::string exportMidiRange(const Song& song,const Conversion& result,const Settings& settings,int firstTick,int lastTick){
    if(firstTick<0||lastTick<=firstTick)throw std::invalid_argument("MIDI 导出区间无效。");
    return serializeMidi(song,result,settings,ExportRange{firstTick,lastTick});
}
}
