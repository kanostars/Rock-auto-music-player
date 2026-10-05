#include "hand_score.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace rock {
namespace {
enum class ScoreLabel { Hand, Keys };
struct ScoreData {
    std::map<int,unsigned> strikes;
    int endTick{},initialMicros{500000};
    std::int64_t beatCount{};
    bool tempoChanges{};
    double bpm{};
};
ScoreData prepareScore(const Song& song,const Conversion& result,const Settings& settings,ScoreLabel label){
    const std::string errorPrefix=label==ScoreLabel::Hand?"无法生成手弹谱：":"无法生成键谱：";
    if(song.ppq<1||song.ppq>32767||song.endTick<0||
       !std::isfinite(settings.speed)||settings.speed<=0||
       !std::isfinite(settings.bpm)||settings.bpm<=0)
        throw std::invalid_argument(errorPrefix+"曲目节奏参数无效。");

    ScoreData score;score.endTick=song.endTick;
    // Same-tick duplicate keys merge; distinct ticks remain separate strikes.
    for(const auto& note:result.notes){
        if(note.target<0)continue;
        if(note.target>=static_cast<int>(keys.size())||note.startTick<0||note.endTick<=note.startTick)
            throw std::invalid_argument(errorPrefix+"音符音高或时间无效。");
        score.strikes[note.startTick]|=1u<<note.target;
        score.endTick=std::max(score.endTick,note.endTick);
        if(label==ScoreLabel::Keys&&score.strikes.size()>1'000'000)
            throw std::invalid_argument("曲目超过 100 万组按键，键谱过长，无法复制。");
    }
    if(score.strikes.empty())throw std::invalid_argument("当前曲目没有可复制的九键音符。");
    score.beatCount=(static_cast<std::int64_t>(score.endTick)+song.ppq-1)/song.ppq;
    if(label==ScoreLabel::Hand&&score.beatCount>1'000'000)
        throw std::invalid_argument("曲目超过 100 万拍，手弹谱过长，无法复制。");

    if(!settings.fixedTempo){
        int lastTick=-1;
        for(const auto& tempo:song.tempos){
            if(tempo.tick>score.endTick)break;
            if(tempo.tick<0||tempo.tick<lastTick||tempo.micros<=0)
                throw std::invalid_argument(errorPrefix+"原曲速度无效。");
            lastTick=tempo.tick;
            if(tempo.tick==0)score.initialMicros=tempo.micros;
            else if(tempo.micros!=score.initialMicros)score.tempoChanges=true;
        }
    }
    score.bpm=(settings.fixedTempo?settings.bpm:60000000.0/score.initialMicros)*settings.speed;
    if(!std::isfinite(score.bpm)||score.bpm<=0)
        throw std::invalid_argument(errorPrefix+"参考速度无效。");
    return score;
}
void appendKeys(std::string& text,unsigned mask){
    if(!mask){text+="—";return;}
    const bool chord=std::popcount(mask)>1;
    if(chord)text+='[';
    for(size_t target=0;target<keys.size();++target)if(mask&(1u<<target))text+=keys[target];
    if(chord)text+=']';
}
std::string bpmNumber(double bpm){
    std::ostringstream number;number.imbue(std::locale::classic());
    number<<std::fixed<<std::setprecision(2)<<bpm;
    auto text=number.str();
    while(text.back()=='0')text.pop_back();
    if(text.back()=='.')text.pop_back();
    return text;
}
std::string beatNumber(long double beats){
    if(!std::isfinite(beats)||beats<=0)
        throw std::invalid_argument("无法生成键谱：按键间隔无效。");
    // Nine decimals normally suffice; preserve very short positive intervals.
    const int precision=beats<0.0000000005L?
        std::max(9,static_cast<int>(std::ceil(-std::log10(beats)))+1):9;
    std::ostringstream number;number.imbue(std::locale::classic());
    number<<std::fixed<<std::setprecision(precision)<<beats;
    auto text=number.str();
    while(text.back()=='0')text.pop_back();
    if(text.back()=='.')text.pop_back();
    return text;
}
}
std::string exportHandScore(const Song& song,const Conversion& result,const Settings& settings){
    const auto score=prepareScore(song,result,settings,ScoreLabel::Hand);
    const auto& strikes=score.strikes;

    std::string text="参考速度："+bpmNumber(score.bpm)+" BPM\n"
        "大写字母是游戏按键；空格仅用于分组，不代表固定停顿。\n"
        "按节拍参考分组，每 4 拍换行；— 表示这一拍没有新按键，等待或让前音延续。\n"
        "同拍内从左到右依次弹奏；[BF] 表示括号内的按键同时按下。\n";
    if(score.tempoChanges)text+="原曲含变速，以上 BPM 为起始参考速度。\n";
    text+='\n';
    auto strike=strikes.begin();
    for(std::int64_t beat=0;beat<score.beatCount;++beat){
        if(beat>0)text+=beat%4==0?'\n':' ';
        if(strike==strikes.end()||strike->first/song.ppq!=beat){appendKeys(text,0);continue;}
        while(strike!=strikes.end()&&strike->first/song.ppq==beat){
            appendKeys(text,strike->second);
            ++strike;
        }
    }
    text+='\n';return text;
}

std::string exportKeyScore(const Song& song,const Conversion& result,const Settings& settings){
    const auto score=prepareScore(song,result,settings,ScoreLabel::Keys);
    const auto& strikes=score.strikes;

    // Integrate each interval directly to avoid subtracting large timestamps.
    // At one header BPM, tempo changes become longer/shorter equivalent beats.
    auto tempo=song.tempos.begin();
    int currentMicros=500000;
    auto beatsBetween=[&](int first,int last){
        if(settings.fixedTempo||!score.tempoChanges)
            return static_cast<long double>(last-first)/song.ppq;
        while(tempo!=song.tempos.end()&&tempo->tick<=first){
            currentMicros=tempo->micros;++tempo;
        }
        long double beats=0;
        int cursor=first;
        while(tempo!=song.tempos.end()&&tempo->tick<last){
            beats+=static_cast<long double>(tempo->tick-cursor)*currentMicros/(static_cast<long double>(song.ppq)*score.initialMicros);
            cursor=tempo->tick;currentMicros=tempo->micros;++tempo;
        }
        return beats+static_cast<long double>(last-cursor)*currentMicros/(static_cast<long double>(song.ppq)*score.initialMicros);
    };

    std::string text="# BPM: "+bpmNumber(score.bpm)+"\n";
    bool firstToken=true;
    auto append=[&](unsigned mask,int first,int last){
        if(!firstToken)text+=' ';
        firstToken=false;
        appendKeys(text,mask);
        text+=':';text+=beatNumber(beatsBetween(first,last));
    };
    auto strike=strikes.begin();
    if(strike->first>0)append(0,0,strike->first);
    while(strike!=strikes.end()){
        const auto current=strike++;
        append(current->second,current->first,strike==strikes.end()?score.endTick:strike->first);
    }
    text+='\n';return text;
}
}
