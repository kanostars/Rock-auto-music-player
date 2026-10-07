#include "number_score.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>

namespace rock {
namespace {
constexpr std::size_t maxMeasures=4096, maxVoices=64, maxEvents=1000000;
constexpr double epsilon=1e-7;
struct Strike {
    int sourceStart{}, sourceEnd{}, target{};
    double sourceSeconds{};
};
struct Chord {
    double start{}, end{}, sourceSeconds{};
    std::uint16_t keys{};
};
bool playable(Mapping mapping){
    return mapping==Mapping::Exact||mapping==Mapping::Approximate||mapping==Mapping::Edited;
}
double secondsAt(const Song& song,const Settings& settings,double tick){
    if(settings.fixedTempo)return tick/song.ppq*60/settings.bpm/settings.speed;
    const int whole=static_cast<int>(std::floor(std::clamp(tick,0.0,double(std::numeric_limits<int>::max()))));
    auto tempo=std::upper_bound(song.tempos.begin(),song.tempos.end(),whole,
        [](int value,const Tempo& item){return value<item.tick;});
    const int micros=tempo==song.tempos.begin()?500000:std::prev(tempo)->micros;
    return (song.secondsAt(whole)+(tick-whole)*micros/(song.ppq*1000000.0))/settings.speed;
}
double bpmAt(const Song& song,const Settings& settings,double tick){
    if(settings.fixedTempo)return settings.bpm*settings.speed;
    auto tempo=std::upper_bound(song.tempos.begin(),song.tempos.end(),tick,
        [](double value,const Tempo& item){return value<item.tick;});
    return 60000000.0/(tempo==song.tempos.begin()?500000:std::prev(tempo)->micros)*settings.speed;
}
void appendSpan(std::vector<NumberScoreEvent>& events,const NumberScoreMeasure& measure,
                double start,double end,const Chord* chord,const Song& song,
                const Settings& settings,double step,std::size_t& count){
    const double beatUnits=measure.denominator==8&&measure.numerator>3&&measure.numerator%3==0?
        6.0:16.0/measure.denominator;
    while(start<end-epsilon){
        if(++count>maxEvents)throw std::runtime_error("简谱内容过多，请缩短曲目或减少同时演奏的音轨。");
        const double local=(start-measure.startTick)/step;
        const double boundary=measure.startTick+(std::floor((local+epsilon)/beatUnits)+1)*beatUnits*step;
        const double available=std::max(0.0,(std::min(end,boundary)-start)/step);
        int units=1;
        for(int candidate:{6,4,3,2,1}){
            if(candidate==6&&beatUnits<6-epsilon)continue;
            if(candidate<=available+epsilon){units=candidate;break;}
        }
        // A signature change or MIDI end may truncate the last quantized unit.
        const double atomEnd=std::min({end,boundary,start+units*step});
        if(atomEnd<=start+epsilon)throw std::runtime_error("简谱拍号的时间范围过小，无法排版。");
        const bool tieIn=chord&&start>chord->start+epsilon;
        const bool tieOut=chord&&atomEnd<chord->end-epsilon;
        events.push_back({start,atomEnd,chord?chord->sourceSeconds:secondsAt(song,settings,start),
            units,chord?chord->keys:std::uint16_t{},tieIn,tieOut,
            chord&&tieIn&&units==4&&start>measure.startTick+epsilon&&
                std::abs(local/4-std::round(local/4))<epsilon});
        start=atomEnd;
    }
}
}

std::pair<int,int> numberDegree(int target){
    static constexpr std::pair<int,int> degrees[]={{6,-1},{3,0},{4,0},{5,0},{6,0},
        {7,0},{1,1},{2,1},{3,1}};
    return target>=0&&target<static_cast<int>(keys.size())?degrees[target]:std::pair{0,0};
}

NumberScore makeNumberScore(const Song& song,const Conversion& conversion,const Settings& settings){
    if(song.ppq<=0||song.endTick<0||!std::isfinite(settings.speed)||settings.speed<=0||
       !std::isfinite(settings.bpm)||settings.bpm<=0||
       !std::isfinite(conversion.duration)||conversion.duration<0)
        throw std::invalid_argument("简谱节奏或时间参数无效。");
    int previousTempo=-1;
    for(const auto& tempo:song.tempos){
        if(tempo.tick<previousTempo||tempo.tick<0||tempo.micros<=0||!std::isfinite(tempo.seconds))
            throw std::invalid_argument("简谱速度信息无效。");
        previousTempo=tempo.tick;
    }
    if(conversion.notes.size()>200000)throw std::runtime_error("简谱超过 20 万音符上限。");
    NumberScore score;score.bpm=bpmAt(song,settings,0);
    if(!std::isfinite(score.bpm)||score.bpm<=0)
        throw std::invalid_argument("简谱速度超出支持范围。");
    std::map<std::pair<int,int>,Strike> strikes;
    double endTick=song.endTick;
    for(const auto& note:conversion.notes){
        if(note.target<0||note.target>=static_cast<int>(keys.size())||!playable(note.mapping)||
           note.startTick<0||note.endTick<=note.startTick||!std::isfinite(note.start)||note.start<0||
           !std::isfinite(note.duration)||note.duration<=0)continue;
        const auto key=std::pair{note.startTick,note.target};
        const auto [it,inserted]=strikes.try_emplace(key,Strike{note.startTick,note.endTick,note.target,note.start});
        if(!inserted){it->second.sourceEnd=std::max(it->second.sourceEnd,note.endTick);
            it->second.sourceSeconds=std::min(it->second.sourceSeconds,note.start);}
        endTick=std::max(endTick,double(note.endTick));
    }
    if(endTick<=0)return score;
    std::map<int,TimeSignature> signatures;
    signatures.emplace(0,TimeSignature{0,4,4});
    for(const auto& signature:song.timeSignatures){
        if(signature.tick<0||signature.numerator<=0||signature.numerator>255||
           signature.denominator<=0||(signature.denominator&(signature.denominator-1))!=0)
            throw std::invalid_argument("简谱拍号无效。");
        signatures[signature.tick]=signature;
    }
    auto signature=signatures.begin();
    double cursor=0;
    while(cursor<endTick-epsilon){
        if(score.measures.size()>=maxMeasures)
            throw std::runtime_error("简谱超过 4096 小节上限，请缩短曲目或检查 MIDI 拍号。");
        while(std::next(signature)!=signatures.end()&&std::next(signature)->first<=cursor+epsilon)++signature;
        const auto& active=signature->second;
        const double length=double(song.ppq)*4*active.numerator/active.denominator;
        double next=std::min(endTick,cursor+length);
        if(std::next(signature)!=signatures.end())next=std::min(next,double(std::next(signature)->first));
        if(!std::isfinite(next)||next<=cursor+epsilon)
            throw std::runtime_error("简谱拍号的时间范围过小，无法排版。");
        score.measures.push_back({static_cast<int>(score.measures.size())+1,active.numerator,active.denominator,
            cursor,next,secondsAt(song,settings,cursor),secondsAt(song,settings,next),
            bpmAt(song,settings,cursor),{}});
        const auto& measure=score.measures.back();
        if(!std::isfinite(measure.startSeconds)||!std::isfinite(measure.endSeconds)||
           !std::isfinite(measure.bpm)||measure.bpm<=0||measure.endSeconds<=measure.startSeconds)
            throw std::invalid_argument("简谱速度或时间超出支持范围。");
        cursor=next;
    }
    const double step=song.ppq/4.0;
    // Only identical source onsets become chords. Quantization never fuses two
    // distinct strikes just because they round to the same grid position.
    std::map<std::tuple<int,double,double>,Chord> bySpan;
    for(const auto& [key,strike]:strikes){
        const double lastGrid=std::max(0.0,(std::ceil(endTick/step)-1)*step);
        const double start=std::clamp(std::round(strike.sourceStart/step)*step,0.0,lastGrid);
        const double end=std::min(endTick,std::max(start+step,std::round(strike.sourceEnd/step)*step));
        if(start>=endTick-epsilon||end<=start+epsilon)continue;
        const auto [it,inserted]=bySpan.try_emplace(std::tuple{strike.sourceStart,start,end},
            Chord{start,end,strike.sourceSeconds,0});
        it->second.keys|=static_cast<std::uint16_t>(1u<<strike.target);
        it->second.sourceSeconds=std::min(it->second.sourceSeconds,strike.sourceSeconds);
    }
    std::vector<Chord> chords;chords.reserve(bySpan.size());
    for(const auto& [span,chord]:bySpan)chords.push_back(chord);
    std::stable_sort(chords.begin(),chords.end(),[](const Chord& left,const Chord& right){
        if(left.start!=right.start)return left.start<right.start;
        if(left.end!=right.end)return left.end>right.end;
        return left.keys>right.keys;
    });
    std::vector<std::vector<Chord>> voices;
    for(const auto& chord:chords){
        auto voice=std::find_if(voices.begin(),voices.end(),[&](const auto& notes){
            return notes.back().end<=chord.start+epsilon;});
        if(voice==voices.end()){
            if(voices.size()>=maxVoices)throw std::runtime_error("简谱同时声部超过 64 个，请减少同时演奏的音轨。");
            voices.emplace_back();voice=std::prev(voices.end());
        }
        voice->push_back(chord);
    }
    if(voices.empty())voices.emplace_back();
    score.voiceCount=static_cast<int>(voices.size());
    std::size_t eventCount=0;
    for(std::size_t voiceIndex=0;voiceIndex<voices.size();++voiceIndex){
        const auto& notes=voices[voiceIndex];std::size_t noteIndex=0;
        for(auto& measure:score.measures){
            if(measure.voices.empty())measure.voices.resize(voices.size());
            auto& events=measure.voices[voiceIndex];cursor=measure.startTick;
            while(noteIndex<notes.size()&&notes[noteIndex].end<=cursor+epsilon)++noteIndex;
            std::size_t index=noteIndex;
            while(index<notes.size()&&notes[index].start<measure.endTick-epsilon){
                const auto& chord=notes[index];
                const double begin=std::max(measure.startTick,chord.start),end=std::min(measure.endTick,chord.end);
                if(begin>cursor+epsilon)appendSpan(events,measure,cursor,begin,nullptr,song,settings,step,eventCount);
                if(end>begin+epsilon)appendSpan(events,measure,begin,end,&chord,song,settings,step,eventCount);
                cursor=std::max(cursor,end);++index;
            }
            if(cursor<measure.endTick-epsilon)appendSpan(events,measure,cursor,measure.endTick,nullptr,song,settings,step,eventCount);
        }
    }
    return score;
}
}
