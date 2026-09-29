#include "music.h"
#include <MidiFile.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace rock {
namespace {
// Validate SMF boundaries before passing untrusted bytes to Midifile.
struct Reader {
    const std::string& b; size_t pos{}, end{};
    unsigned byte() {
        if (pos >= end) throw std::runtime_error("MIDI 文件被截断：事件或数据块不完整。");
        return static_cast<unsigned char>(b[pos++]);
    }
    unsigned be(int count) { unsigned v=0; while(count--) v=(v<<8)|byte(); return v; }
    unsigned vlq() {
        unsigned v=0;
        for (int i=0;i<4;++i) { auto x=byte(); v=(v<<7)|(x&127); if (!(x&128)) return v; }
        throw std::runtime_error("MIDI 可变长度数值超出 4 字节。");
    }
    void skip(size_t n) {
        if (n>end-pos) throw std::runtime_error("MIDI 事件长度超出数据块边界。");
        pos+=n;
    }
};
void cancelled(const std::atomic_bool* flag) {
    if (flag && flag->load()) throw std::runtime_error("导入已取消。");
}
void validate(const std::string& bytes, const std::atomic_bool* cancel) {
    if (bytes.size()>32*1024*1024) throw std::runtime_error("文件超过初版 32 MB 限制。");
    Reader r{bytes,0,bytes.size()};
    if (r.be(4)!=0x4d546864 || r.be(4)!=6) throw std::runtime_error("不是有效的标准 MIDI 文件（MThd）。");
    const auto format=r.be(2), tracks=r.be(2), division=r.be(2);
    if (format>1) throw std::runtime_error("初版支持 MIDI Format 0 和 1，暂不支持 Format 2。");
    if (!tracks || tracks>256 || (format==0 && tracks!=1)) throw std::runtime_error("MIDI 轨道数无效或超过 256 轨限制。");
    if (!division || (division&0x8000)) throw std::runtime_error("初版只支持 PPQ 时间制，暂不支持 SMPTE。");
    size_t events=0, notes=0;
    for (unsigned t=0;t<tracks;++t) {
        cancelled(cancel);
        if (r.be(4)!=0x4d54726b) throw std::runtime_error("MIDI 缺少 MTrk 轨道数据。");
        const auto len=r.be(4);
        if(len>r.end-r.pos) throw std::runtime_error("MIDI 轨道长度无效。");
        Reader e{bytes,r.pos,r.pos+len}; r.skip(len);
        unsigned running=0; uint64_t tick=0; bool ended=false;
        while(e.pos<e.end) {
            if (++events>2000000) throw std::runtime_error("MIDI 超过初版 200 万事件限制。");
            if (events%4096==0) cancelled(cancel);
            tick+=e.vlq();
            if(tick>std::numeric_limits<int>::max()) throw std::runtime_error("MIDI tick 时间超出支持范围。");
            auto status=e.byte(); bool firstData=false;
            if (status<128) {
                if (!running) throw std::runtime_error("MIDI running status 无效。");
                status=running; firstData=true;
            }
            if(status==0xff) {
                running=0;
                auto type=e.byte(), n=e.vlq();
                if ((type==0x51 && n!=3)||(type==0x2f && n!=0)||(type==0x58 && n!=4))
                    throw std::runtime_error("MIDI 元事件长度无效。");
                if(type==0x51) { if (!e.be(3)) throw std::runtime_error("MIDI 速度值不能为零。"); }
                else e.skip(n);
                if(type==0x2f) { ended=true; if(e.pos!=e.end) throw std::runtime_error("轨道结束标记后仍有数据。"); }
            } else if(status==0xf0 || status==0xf7) { running=0; e.skip(e.vlq()); }
            else if(status<0xf0) {
                running=status;
                int count=((status&0xf0)==0xc0||(status&0xf0)==0xd0)?1:2;
                if(firstData) --count;
                while(count--) if(e.byte()>127) throw std::runtime_error("MIDI 通道事件的数据字节无效。");
                if((status&0xf0)==0x90 && ++notes>200000) throw std::runtime_error("MIDI 超过初版 20 万 Note On 限制。");
            } else throw std::runtime_error("SMF 中包含不支持的系统事件。");
        }
        if(!ended) throw std::runtime_error("MIDI 轨道缺少结束标记。");
    }
    if(r.pos!=r.end) throw std::runtime_error("MIDI 文件尾部存在额外数据。");
}
std::string metaText(const smf::MidiEvent& e) {
    size_t p=2; unsigned n=0;
    for(int i=0;i<4 && p<e.size();++i) { auto v=e[p++]; n=(n<<7)|(v&127); if(!(v&128)) break; }
    n=std::min<size_t>(n,e.size()-p);
    return std::string(e.begin()+static_cast<long>(p),e.begin()+static_cast<long>(p+n));
}
}

Song parseMidi(const std::string& bytes, const std::atomic_bool* cancel) {
    validate(bytes,cancel);
    std::istringstream stream(bytes,std::ios::binary);
    smf::MidiFile midi;
    if(!midi.readSmf(stream)) throw std::runtime_error("MIDI 解析失败。");
    cancelled(cancel);
    Song song; song.format=static_cast<unsigned char>(bytes[9]); song.ppq=midi.getTPQ();
    struct RawTempo {int tick, track, order, micros;};
    std::vector<RawTempo> tempos;
    int orphan=0, truncated=0, invalid=0, overlap=0;
    bool expression=false;
    for(int t=0;t<midi.getTrackCount();++t) {
        cancelled(cancel);
        std::string name;
        int endTick=0;
        std::map<int,int> channelTracks;
        std::map<std::pair<int,int>,std::deque<std::pair<int,int>>> active;
        for(int i=0;i<midi[t].size();++i) {
            const auto& e=midi[t][i]; endTick=std::max(endTick,e.tick);
            if(e.size()>2 && e[0]==0xff && e[1]==3) name=metaText(e);
            if(e.isTempo()) tempos.push_back({e.tick,t,i,(int(e[3])<<16)|(int(e[4])<<8)|e[5]});
        }
        auto trackFor=[&](int channel) {
            if(auto it=channelTracks.find(channel);it!=channelTracks.end()) return it->second;
            int id=static_cast<int>(song.tracks.size());
            song.tracks.push_back({name.empty()?"未命名音轨":name,t,channel,0});
            channelTracks[channel]=id; return id;
        };
        auto append=[&](int ch,int pitch,int start,int end,int vel) {
            if(end<=start) {++invalid; return;}
            int tr=trackFor(ch); song.notes.push_back({tr,pitch,vel,start,end}); ++song.tracks[tr].count;
        };
        for(int i=0;i<midi[t].size();++i) {
            if(i%4096==0) cancelled(cancel);
            const auto& e=midi[t][i];
            if(e.isNoteOn()) {
                auto& queue=active[{e.getChannel(),e.getKeyNumber()}];
                if(!queue.empty()) ++overlap;
                queue.emplace_back(e.tick,e.getVelocity());
            } else if(e.isNoteOff()) {
                auto& queue=active[{e.getChannel(),e.getKeyNumber()}];
                if(queue.empty()) {++orphan; continue;}
                const auto [start,velocity]=queue.front(); queue.pop_front();
                append(e.getChannel(),e.getKeyNumber(),start,e.tick,velocity);
            } else if(!e.empty() && ((e[0]&0xf0)==0xe0 || ((e[0]&0xf0)==0xb0 && e.size()>1 && (e[1]==64||e[1]==11)))) expression=true;
        }
        for(const auto& [key,queue]:active) for(auto [start,velocity]:queue) {
            append(key.first,key.second,start,endTick,velocity); ++truncated;
        }
        song.endTick=std::max(song.endTick,endTick);
    }
    if(orphan) song.warnings.push_back(std::to_string(orphan)+" 个孤立 Note Off 已忽略。");
    if(truncated) song.warnings.push_back(std::to_string(truncated)+" 个缺少 Note Off 的音符在轨道末尾截断。");
    if(invalid) song.warnings.push_back(std::to_string(invalid)+" 个非正时长音符已排除。");
    if(overlap) song.warnings.push_back(std::to_string(overlap)+" 处同音重叠按 FIFO 配对。");
    if(expression) song.warnings.push_back("弯音、延音踏板和表情控制未转换为九键演奏效果。");
    std::sort(tempos.begin(),tempos.end(),[](const auto& a,const auto& b){
        return std::tie(a.tick,a.track,a.order)<std::tie(b.tick,b.track,b.order);
    });
    song.tempos.push_back({0,500000,0});
    int tempoConflicts=0;
    for(size_t i=0;i<tempos.size();) {
        size_t end=i+1, chosen=i;
        while(end<tempos.size() && tempos[end].tick==tempos[i].tick) {
            if(tempos[end].track==tempos[i].track) chosen=end;
            ++end;
        }
        for(size_t k=i;k<end;++k) if(tempos[k].micros!=tempos[chosen].micros) {++tempoConflicts; break;}
        const auto& p=tempos[chosen];
        double seconds=song.secondsAt(p.tick);
        if(song.tempos.back().tick==p.tick) song.tempos.back()={p.tick,p.micros,seconds};
        else song.tempos.push_back({p.tick,p.micros,seconds});
        i=end;
    }
    if(tempoConflicts) song.warnings.push_back("同 tick 的不同速度已按最低轨号、同轨最后事件处理。");
    // Display channels consistently, independent of the order notes end.
    std::vector<int> order(song.tracks.size()), remap(song.tracks.size());
    std::iota(order.begin(),order.end(),0);
    std::sort(order.begin(),order.end(),[&](int a,int b){
        return std::tie(song.tracks[a].source,song.tracks[a].channel)<std::tie(song.tracks[b].source,song.tracks[b].channel);
    });
    auto originalTracks=std::move(song.tracks);
    for(size_t i=0;i<order.size();++i){remap[order[i]]=static_cast<int>(i);song.tracks.push_back(std::move(originalTracks[order[i]]));}
    for(auto& note:song.notes)note.track=remap[note.track];
    return song;
}

double Song::secondsAt(int tick) const {
    auto it=std::upper_bound(tempos.begin(),tempos.end(),tick,[](int t,const Tempo& x){return t<x.tick;});
    if(it==tempos.begin()) return double(tick)*0.5/ppq;
    --it; return it->seconds+double(tick-it->tick)*it->micros/(ppq*1000000.0);
}
int nearestTarget(int pitch) {
    int result=0;
    for(int i=1;i<9;++i) if(std::abs(pitch-pitches[i])<std::abs(pitch-pitches[result])) result=i;
    return result;
}
namespace {
int pitchClassTarget(int pitch) {
    const int pitchClass=(pitch%12+12)%12;
    int result=-1;
    for(int i=0;i<static_cast<int>(pitches.size());++i)
        if(pitches[i]%12==pitchClass&&(result<0||std::abs(pitch-pitches[i])<std::abs(pitch-pitches[result])))result=i;
    return result; // Ascending targets break equal-distance ties toward the lower pitch.
}
}
Settings defaultSettings(const Song& song) {
    Settings s;
    for(const auto& tr:song.tracks) {s.enabled.push_back(tr.channel!=9); s.solo.push_back(false);}
    return s;
}
int tickAtSeconds(const Song& song,const Settings& s,double seconds) {
    if(!std::isfinite(seconds)||!std::isfinite(s.speed)||s.speed<=0||
       !std::isfinite(s.bpm)||s.bpm<=0||song.ppq<=0)
        throw std::invalid_argument("音符时间或节奏参数无效。");
    seconds=std::max(0.0,seconds)*s.speed;
    double tick;
    if(s.fixedTempo) tick=seconds*song.ppq*s.bpm/60.0;
    else {
        auto it=std::upper_bound(song.tempos.begin(),song.tempos.end(),seconds,
            [](double t,const Tempo& tempo){return t<tempo.seconds;});
        if(it==song.tempos.begin())tick=seconds*song.ppq*2.0;
        else {--it;tick=it->tick+(seconds-it->seconds)*song.ppq*1000000.0/it->micros;}
    }
    return static_cast<int>(std::llround(std::clamp(tick,0.0,double(std::numeric_limits<int>::max()))));
}
Conversion convert(const Song& song,const Settings& s,const NoteEdits& edits) {
    if(!std::isfinite(s.speed)||!std::isfinite(s.bpm)||s.speed<=0||s.bpm<=0||s.holdMs<1||s.gapMs<0)
        throw std::invalid_argument("节奏或按键参数无效。");
    Conversion out;
    auto enabled=[&](size_t i){return i<s.enabled.size()?s.enabled[i]:true;};
    auto solo=[&](size_t i){return i<s.solo.size()?s.solo[i]:false;};
    bool anySolo=false;
    for(size_t i=0;i<song.tracks.size();++i) if(enabled(i)&&solo(i)) anySolo=true;
    if(s.autoTranspose) {
        // SIFT v0.1.6: prefer keeping pitch classes, then minimize octave movement.
        // One shared transpose for participating original tracks keeps their tuning
        // consistent. Manual targets/additions and nearest/skip mode do not bias it.
        std::array<int,128> histogram{};
        for(const auto& n:song.notes)
            if(!n.added&&!n.derived&&enabled(n.track)&&(!anySolo||solo(n.track))) ++histogram.at(n.pitch);
        auto score=[&](int shift) {
            int kept=0;long long movement=0;
            for(int pitch=0;pitch<128;++pitch) if(histogram[pitch]) {
                const int adjusted=pitch+shift,target=pitchClassTarget(adjusted);
                if(target>=0){kept+=histogram[pitch];movement+=static_cast<long long>(histogram[pitch])*std::abs(adjusted-pitches[target]);}
            }
            return std::tuple{-kept,movement,std::abs(shift),shift};
        };
        auto best=score(0);
        for(int shift=-12;shift<=12;++shift) {
            auto candidate=score(shift);
            if(candidate<best) {
                best=candidate;out.transpose=shift;
            }
        }
    }
    auto time=[&](int tick){return (s.fixedTempo?double(tick)/song.ppq*60.0/s.bpm:song.secondsAt(tick))/s.speed;};
    out.duration=time(song.endTick);
    std::map<std::pair<int,int>,std::vector<size_t>> groups;
    for(size_t i=0;i<song.notes.size();++i) {
        const auto& n=song.notes[i];
        MappedNote m; m.source=static_cast<int>(i);m.startTick=n.start;m.endTick=n.end;
        auto edit=edits.find(m.source);
        if(n.added&&edit==edits.end()) {
            m.mapping=Mapping::Excluded;out.notes.push_back(m);continue;
        }
        if(edit!=edits.end()) {
            const auto& e=edit->second;
            if(e.startTick<0||e.endTick<=e.startTick||(!e.deleted&&(e.target<0||e.target>=9)))
                throw std::invalid_argument("音符编辑超出九键范围或时长无效。");
            m.startTick=e.startTick;m.endTick=e.endTick;
        }
        m.start=time(m.startTick);m.duration=time(m.endTick)-m.start;
        if(edit!=edits.end()&&edit->second.deleted) {m.mapping=Mapping::Deleted;++out.deleted;}
        else if(edit!=edits.end()&&edit->second.skipped) {m.mapping=Mapping::ConflictSkipped;++out.skipped;}
        else if(!enabled(n.track)||(anySolo&&!solo(n.track))) {m.mapping=Mapping::Excluded; ++out.excluded;}
        else if(edit!=edits.end()) {m.mapping=Mapping::Edited;m.target=edit->second.target;++out.edited;}
        else {
            int adjusted=n.pitch+out.transpose;
            int target=pitchClassTarget(adjusted);
            if(target>=0) {m.mapping=Mapping::Exact; m.target=target; ++out.exact;if(pitches[target]!=adjusted)++out.octaveFolded;}
            else if(s.nearest) {m.mapping=Mapping::Approximate; m.target=nearestTarget(adjusted); ++out.approximate;}
            else {m.mapping=Mapping::Skipped; ++out.skipped;}
        }
        out.notes.push_back(m);
        if(m.target>=0) {
            groups[{m.startTick,m.target}].push_back(i);
            out.duration=std::max(out.duration,m.start+m.duration);
        }
    }
    std::array<double,9> last; last.fill(-1e100);
    std::map<int,int> chordCounts;
    for(const auto& [key,ids]:groups) {
        ++chordCounts[key.first]; out.merged+=static_cast<int>(ids.size())-1;
        double start=out.notes[ids.front()].start;
        if(start-last[key.second]+1e-9<(s.holdMs+s.gapMs)/1000.0) {
            ++out.conflicts; for(auto id:ids) out.notes[id].conflict=true;
        }
        last[key.second]=start;
    }
    for(auto [tick,count]:chordCounts) if(count>1) ++out.chords;
    return out;
}
NoteEdits resolveSameKeyConflicts(const Conversion& result,const Settings& settings,bool remove){
    std::map<std::pair<int,int>,std::vector<const MappedNote*>> groups;
    for(const auto& note:result.notes)if(note.target>=0)groups[{note.startTick,note.target}].push_back(&note);
    std::array<double,9> last;last.fill(-1e100);NoteEdits edits;
    for(const auto& [key,notes]:groups){
        const auto* first=notes.front();
        // Compare with the last retained strike, not with a strike just removed.
        if(first->start-last[key.second]+1e-9<(settings.holdMs+settings.gapMs)/1000.0){
            for(const auto* note:notes)edits[note->source]={note->startTick,note->endTick,note->target,remove,!remove};
        }else last[key.second]=first->start;
    }
    return edits;
}
std::string pitchName(int pitch) {
    static const char* names[]{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    return std::string(names[pitch%12])+std::to_string(pitch/12-1);
}
}
