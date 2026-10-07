#include "music.h"
#include <algorithm>
#include <stdexcept>
#include <limits>
namespace rock {
namespace {
using SignatureMap=std::map<int,std::pair<int,int>>;
void setSignatures(Song& song,const SignatureMap& signatures){
    song.timeSignatures.clear();
    for(const auto& [tick,signature]:signatures){
        if(!song.timeSignatures.empty()){
            const auto& previous=song.timeSignatures.back();
            if(previous.numerator==signature.first&&previous.denominator==signature.second)continue;
        }
        song.timeSignatures.push_back({tick,signature.first,signature.second});
    }
}
}
double secondsAtTick(const Song& song,const Settings& settings,int tick){
    return (settings.fixedTempo?double(tick)/song.ppq*60/settings.bpm:song.secondsAt(tick))/settings.speed;
}
void deleteTimeRange(Song& song,NoteEdits& edits,int first,int last){
    if(first<0||last<=first)throw std::invalid_argument("删除区间无效。");
    auto shift=[&](int tick){return tick<=first?tick:tick<last?first:tick-(last-first);};
    for(size_t i=0;i<song.notes.size();++i){
        auto& n=song.notes[i];auto it=edits.find(static_cast<int>(i));
        const int start=it==edits.end()?n.start:it->second.startTick;
        const int end=it==edits.end()?n.end:it->second.endTick;
        if(start>=first&&end<=last){
            // Preserve IDs, including excluded/skipped notes, for history and reports.
            auto& edit=edits[static_cast<int>(i)];edit.deleted=true;
            edit.startTick=shift(start);edit.endTick=std::max(edit.startTick+1,shift(end));
        }else if(it!=edits.end()){
            it->second.startTick=shift(start);it->second.endTick=std::max(it->second.startTick+1,shift(end));
        }
        n.start=shift(n.start);n.end=std::max(n.start+1,shift(n.end));
    }
    // The tempo active at the right edge must resume at the splice point.
    std::map<int,int> tempos;tempos[0]=500000;int atEnd=500000;
    for(const auto& t:song.tempos){if(t.tick<=last)atEnd=t.micros;
        if(t.tick<first)tempos[t.tick]=t.micros;else if(t.tick>=last)tempos[shift(t.tick)]=t.micros;}
    tempos[first]=atEnd;song.tempos.clear();
    for(auto [tick,micros]:tempos){const double seconds=song.secondsAt(tick);song.tempos.push_back({tick,micros,seconds});}
    SignatureMap signatures{{0,{4,4}}};std::pair<int,int> signatureAtEnd{4,4};
    for(const auto& signature:song.timeSignatures){
        if(signature.tick<=last)signatureAtEnd={signature.numerator,signature.denominator};
        if(signature.tick<first)signatures[signature.tick]={signature.numerator,signature.denominator};
        else if(signature.tick>=last)signatures[shift(signature.tick)]={signature.numerator,signature.denominator};
    }
    signatures[first]=signatureAtEnd;setSignatures(song,signatures);
    song.endTick=shift(song.endTick);
}
void insertBlankRange(Song& song,NoteEdits& edits,int first,int last){
    if(first<0||last<=first)throw std::invalid_argument("空片段区间无效。");
    const int length=last-first;
    auto checked=[&](int tick){if(tick>std::numeric_limits<int>::max()-length)throw std::invalid_argument("插入后超出可编辑时间范围。");return tick+length;};
    checked(std::max(song.endTick,first));size_t splits=0;
    const size_t count=song.notes.size();
    for(size_t i=0;i<count;++i){const auto& n=song.notes[i];checked(n.end);
        const auto it=edits.find(static_cast<int>(i));const int a=it==edits.end()?n.start:it->second.startTick,b=it==edits.end()?n.end:it->second.endTick;checked(b);
        if(a<first&&b>first&&!(n.added&&it==edits.end())&&(it==edits.end()||!it->second.deleted))++splits;
    }
    for(const auto& t:song.tempos)checked(t.tick);
    for(const auto& signature:song.timeSignatures)checked(signature.tick);
    if(count+splits>200000)throw std::invalid_argument("插入时拆分音符将超过每曲目 20 万音符上限。");
    song.notes.reserve(count+splits);
    for(size_t i=0;i<count;++i){auto& n=song.notes[i];auto it=edits.find(static_cast<int>(i));
        const int a=it==edits.end()?n.start:it->second.startTick,b=it==edits.end()?n.end:it->second.endTick;
        if(a<first&&b>first&&!(n.added&&it==edits.end())&&(it==edits.end()||!it->second.deleted)){
            Note continuation=n;continuation.start=last;continuation.end=b+length;continuation.derived=true;
            n.start=a;n.end=first;
            if(it!=edits.end()){auto right=it->second;right.startTick=last;right.endTick=b+length;
                it->second.endTick=first;edits.emplace(static_cast<int>(song.notes.size()),right);}
            song.notes.push_back(continuation);
        }else{
            if(n.start>=first)n.start+=length;if(n.end>first)n.end+=length;
            if(it!=edits.end()){if(it->second.startTick>=first)it->second.startTick+=length;if(it->second.endTick>first)it->second.endTick+=length;}
        }
    }
    // Repeat the selected tempo span in the silence, then resume the original
    // tempo at the insertion point. This preserves both its seconds and the song.
    std::map<int,int> tempos;tempos[0]=500000;int atStart=500000;
    for(const auto& t:song.tempos){if(t.tick<=first)atStart=t.micros;
        if(t.tick<first)tempos[t.tick]=t.micros;
        else {tempos[t.tick+length]=t.micros;if(t.tick<last)tempos[t.tick]=t.micros;}}
    tempos[first]=atStart;tempos[last]=atStart;song.tempos.clear();
    for(auto [tick,micros]:tempos){const double seconds=song.secondsAt(tick);song.tempos.push_back({tick,micros,seconds});}
    SignatureMap signatures{{0,{4,4}}};std::pair<int,int> signatureAtStart{4,4};
    for(const auto& signature:song.timeSignatures){
        if(signature.tick<=first)signatureAtStart={signature.numerator,signature.denominator};
        const std::pair value{signature.numerator,signature.denominator};
        if(signature.tick<first)signatures[signature.tick]=value;
        else {signatures[signature.tick+length]=value;if(signature.tick<last)signatures[signature.tick]=value;}
    }
    signatures[first]=signatureAtStart;signatures[last]=signatureAtStart;setSignatures(song,signatures);
    song.endTick=std::max(song.endTick,first)+length;
}
Conversion playbackRange(const Conversion& source,double start,double end,const Settings& settings){
    auto result=source;result.duration=end;result.conflicts=0;
    std::map<std::pair<int,int>,double> strikes;
    for(auto& n:result.notes){n.conflict=false;
        // No strikes or sample tails originating outside the selected interval.
        if(n.start+1e-9<start||n.start>=end){n.target=-1;continue;}
        if(n.target>=0){n.duration=std::min(n.duration,end-n.start);strikes[{n.startTick,n.target}]=n.start;}}
    std::array<double,9> last;last.fill(-1e100);
    for(auto [key,time]:strikes){if(time-last[key.second]+1e-9<(settings.holdMs+settings.gapMs)/1000.0)++result.conflicts;last[key.second]=time;}
    return result;
}
}
