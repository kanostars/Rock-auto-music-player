#include "core/music.h"
#include <MidiFile.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace rock;
namespace {
int checks=0;
void check(bool condition,const char* message) {++checks;if(!condition)throw std::runtime_error(message);}
bool close(double a,double b) {return std::abs(a-b)<1e-8;}
std::string serialize(smf::MidiFile& midi) {
    for(int t=0;t<midi.getTrackCount();++t)
        if(midi[t].size()==0)midi.addMetaEvent(t,0,0x2f,std::string{});
    midi.sortTracksNoteOffsBeforeOns();std::ostringstream out(std::ios::binary);midi.write(out);return out.str();
}
void add(smf::MidiFile& midi,int track,int channel,int pitch,int tick,int length=240) {
    midi.addNoteOn(track,tick,channel,pitch,100);midi.addNoteOff(track,tick+length,channel,pitch);
}
bool rejects(const std::string& b) {try{parseMidi(b);return false;}catch(const std::exception&){return true;}}
void save(const std::filesystem::path& file,const std::string& bytes) {std::ofstream(file,std::ios::binary).write(bytes.data(),static_cast<std::streamsize>(bytes.size()));}
std::string read(const std::filesystem::path& file) {
    std::ifstream in(file,std::ios::binary);
    if(!in)throw std::runtime_error("Missing regression fixture: "+file.string());
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
}
int main(int argc,char** argv) {
    try {
        smf::MidiFile nine;nine.setTPQ(480);nine.addTempo(0,0,120);
        for(int i=0;i<9;++i)add(nine,0,0,pitches[i],i*480);
        auto bytes=serialize(nine);auto song=parseMidi(bytes);auto settings=defaultSettings(song);auto r=convert(song,settings);
        check(r.exact==9&&r.approximate==0,"nine exact notes");
        for(int i=0;i<9;++i) {check(r.notes[i].target==i,"nine keys order");check(close(r.notes[i].start,i*.5),"PPQ seconds");}
        for(auto [source,target]:{std::pair{54,2}, {61,6},{48,0},{50,1},{72,8},{36,0}}) check(nearestTarget(source)==target,"nearest/tie/endpoint");
        smf::MidiFile unsupported;unsupported.setTPQ(480);for(int i=0;i<6;++i)add(unsupported,0,0,std::array{54,61,63,56,58,66}[i],i*480);
        auto u=parseMidi(serialize(unsupported));auto us=defaultSettings(u);us.autoTranspose=false;check(convert(u,us).approximate==6,"nearest maps unsupported pitch classes");
        us.nearest=false;check(convert(u,us).skipped==6,"skip unsupported");check(u.notes[0].pitch==54,"source immutable");

        auto pirates=parseMidi(read(std::filesystem::path(ROCK_FIXTURES)/"pirates.mid"));
        auto popt=defaultSettings(pirates);auto adapted=convert(pirates,popt);
        check(pirates.notes.size()==241&&adapted.transpose==-12,"uploaded MIDI selects one octave down");
        check(adapted.exact==241&&adapted.approximate==0&&adapted.skipped==0,"uploaded melody fits without quantization");
        std::vector<MappedNote> ordered=adapted.notes;
        std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.startTick<b.startTick;});
        std::string actual,expected;
        for(const auto& n:ordered)actual+=static_cast<char>(keys[n.target]-'A'+'a');
        for(char c:read(std::filesystem::path(ROCK_FIXTURES)/"pirates-keys.txt"))
            if(std::string("bfghjktyu").find(c)!=std::string::npos)expected+=c;
        check(actual==expected,"all uploaded notes match user supplied key sequence");
        const std::array<int,9> expectedCounts{0,4,9,16,69,34,39,35,35};
        std::array<int,9> counts{};for(const auto& n:adapted.notes)++counts[n.target];
        check(counts==expectedCounts,"uploaded song distributes across eight keys");
        popt.autoTranspose=false;auto noTranspose=convert(pirates,popt);
        check(noTranspose.exact==241&&noTranspose.octaveFolded>0,"zero transpose still folds matching pitch classes");
        for(size_t i=0;i<adapted.notes.size();++i) {
            check(pitches[noTranspose.notes[i].target]%12==pirates.notes[i].pitch%12,"folding preserves the note name rather than clamping to U");
            check(close(adapted.notes[i].start,noTranspose.notes[i].start)&&close(adapted.notes[i].duration,noTranspose.notes[i].duration),"adaptation preserves MIDI timing");
        }
        popt.autoTranspose=true;popt.nearest=false;
        check(convert(pirates,popt).exact==241,"skip policy runs after octave adaptation");
        NoteEdits pirateEdits{{0,{0,100,0,false}}};
        auto manual=convert(pirates,popt,pirateEdits);
        check(manual.transpose==-12&&manual.notes[0].target==0&&manual.edited==1,"octave changes preserve manual targets");
        check(pirates.notes[0].pitch==69,"octave adaptation preserves original MIDI pitches");
        auto raised=song;for(auto& n:raised.notes)n.pitch+=24;
        auto raisedResult=convert(raised,defaultSettings(raised));
        check(raisedResult.transpose>=-12&&raisedResult.transpose<=12&&raisedResult.exact==9&&raisedResult.octaveFolded>0,"high range uses bounded transpose plus octave folding");
        auto lowered=song;for(auto& n:lowered.notes)n.pitch-=24;
        auto loweredResult=convert(lowered,defaultSettings(lowered));
        check(loweredResult.transpose>=-12&&loweredResult.transpose<=12&&loweredResult.exact==9&&loweredResult.octaveFolded>0,"low range uses bounded transpose plus octave folding");
        check(convert(song,settings).transpose==0,"playable originals retain octave");
        auto filtered=pirates;filtered.tracks.push_back({"low",1,0,1});filtered.notes.push_back({1,45,100,0,100});
        auto fs=defaultSettings(filtered);fs.solo[1]=true;
        check(convert(filtered,fs).transpose==0,"solo selection determines adaptation");
        fs.solo[1]=false;fs.enabled[1]=false;
        check(convert(filtered,fs).transpose==-12,"muted tracks do not affect adaptation");
        fs.enabled[0]=false;check(convert(filtered,fs).transpose==0,"no active notes leaves zero shift");
        Song wide=song;wide.notes={{0,0,100,0,100},{0,127,100,480,580}};
        auto wideSettings=defaultSettings(wide);auto wideNearest=convert(wide,wideSettings);
        wideSettings.nearest=false;auto wideSkip=convert(wide,wideSettings);
        check(wideNearest.transpose==wideSkip.transpose&&wideSkip.exact==2&&wideSkip.octaveFolded==2,"wide-range same-name notes survive even in skip mode");
        // Confirm octave-first selection throughout MIDI's range, including the two E/A pads.
        Song sweep=song;sweep.notes.clear();sweep.endTick=128*480;
        for(int p=0;p<128;++p)sweep.notes.push_back({0,p,100,p*480,p*480+100});
        auto sweepSettings=defaultSettings(sweep);sweepSettings.autoTranspose=false;
        auto sweepNearest=convert(sweep,sweepSettings);sweepSettings.nearest=false;auto sweepSkip=convert(sweep,sweepSettings);
        const std::array<int,12> sameName{6,-1,7,-1,1,2,-1,3,-1,0,-1,5};
        for(int p=0;p<128;++p){int expected=sameName[p%12];
            if(p%12==4&&p>=64)expected=8;if(p%12==9&&p>=57)expected=4;
            check(sweepSkip.notes[p].target==expected,"all 128 pitches: octave folding and unsupported-class skipping");
            check(sweepNearest.notes[p].target==(expected<0?nearestTarget(p):expected),"all 128 pitches: same-name priority before substitution");
            check(sweepNearest.notes[p].mapping==(expected<0?Mapping::Approximate:Mapping::Exact),"folding is not chromatic substitution");}
        check(sweepNearest.notes[48].target==6&&sweepNearest.notes[50].target==7,"C3/D3 fold to T/Y, not B/F");
        check(sweepNearest.notes[61].target==6,"C sharp midpoint substitutes downward to C");
        Song sharp=song;sharp.notes.clear();
        for(int p: {61,63,65,66,68,70,72})sharp.notes.push_back({0,p,100,static_cast<int>(sharp.notes.size())*480,static_cast<int>(sharp.notes.size())*480+100});
        auto sharpSettings=defaultSettings(sharp);auto sharpResult=convert(sharp,sharpSettings);
        check(sharpResult.transpose==-1&&sharpResult.exact==7&&sharpResult.approximate==0,"chromatic -1 transpose retains a C-sharp-major scale");
        sharpSettings.nearest=false;check(convert(sharp,sharpSettings).transpose==-1,"recommendation is independent of substitute/skip policy");
        sharp.notes={{0,61,100,0,100}};check(convert(sharp,sharpSettings).transpose==-1,"equal retention/movement/absolute transpose chooses lower shift");
        sharp.notes.clear();check(convert(sharp,sharpSettings).transpose==0,"empty selection keeps zero transpose");
        std::cout<<"Pirates: 241/241 keys match, shift -12, U 35/241 (was 241/241)\n";

        smf::MidiFile tempo;tempo.setTPQ(480);tempo.addTrack();tempo.addTempo(0,0,120);tempo.addTempo(0,480,60);add(tempo,1,0,60,240,480);
        auto ts=parseMidi(serialize(tempo));auto opt=defaultSettings(ts);auto timed=convert(ts,opt);
        check(close(timed.notes[0].start,.25)&&close(timed.notes[0].duration,.75),"tempo across note");
        opt.fixedTempo=true;check(close(convert(ts,opt).notes[0].duration,.5),"fixed BPM override");
        opt.fixedTempo=false;opt.speed=2;check(close(convert(ts,opt).notes[0].duration,.375),"speed scales duration");

        auto editSettings=defaultSettings(ts);
        for(int tick:{0,240,480,720,960,2000})
            check(tickAtSeconds(ts,editSettings,ts.secondsAt(tick))==tick,"tempo-map inverse");
        NoteEdits edits{{0,{480,960,7,false}}};
        auto edited=convert(ts,editSettings,edits);
        check(edited.edited==1&&edited.exact==0&&edited.notes[0].target==7,"manual target override");
        check(close(edited.notes[0].start,.5)&&close(edited.notes[0].duration,1)&&close(edited.duration,1.5),"edited time and extended song length");
        editSettings.speed=2;edited=convert(ts,editSettings,edits);
        check(close(edited.notes[0].start,.25)&&close(edited.notes[0].duration,.5),"manual edit follows speed");
        check(tickAtSeconds(ts,editSettings,.25)==480,"inverse includes speed");
        editSettings.fixedTempo=true;editSettings.bpm=60;
        check(tickAtSeconds(ts,editSettings,.5)==480,"fixed BPM inverse includes speed");
        edited=convert(ts,editSettings,edits);check(close(edited.notes[0].duration,.5),"manual edit follows fixed BPM");
        editSettings.enabled[0]=false;check(convert(ts,editSettings,edits).excluded==1,"manual edit respects track mute");
        edits[0].deleted=true;edited=convert(ts,editSettings,edits);
        check(edited.deleted==1&&edited.excluded==0&&edited.notes[0].target==-1,"deleted notes excluded from triggers");
        check(ts.notes[0].start==240&&ts.notes[0].end==720&&ts.notes[0].pitch==60,"editing preserves original source");
        NoteEdits rescued{{0,{0,240,2,false}}};
        check(convert(u,us,rescued).edited==1&&convert(u,us,rescued).skipped==5,"manual selection persists when strategy skips");
        NoteEdits mergedEdit{{1,{0,240,0,false}}};
        check(convert(song,settings,mergedEdit).merged==1,"merging uses edited start and key");
        mergedEdit[1].startTick=30;
        check(convert(song,settings,mergedEdit).conflicts==1,"conflict detection uses edited start and key");
        mergedEdit[0]={0,240,0,true};
        check(convert(song,settings,mergedEdit).conflicts==0,"deletion removes conflict");
        auto invalidEdit=rescued;invalidEdit[0].endTick=0;
        bool badEdit=false;try{convert(u,us,invalidEdit);}catch(const std::invalid_argument&){badEdit=true;}
        check(badEdit,"zero duration edit rejected");

        Song additions;additions.ppq=480;additions.endTick=480;additions.tempos={{0,500000,0}};
        additions.tracks={{"original",0,0,1}};additions.notes={{0,81,100,0,480}};
        auto additionSettings=defaultSettings(additions);auto beforeAdd=convert(additions,additionSettings);
        NoteEdits additionEdits;
        for(int i=1;i<=12;++i) {
            additions.notes.push_back({0,45,100,i*480,(i+1)*480,true});
            additionEdits[i]={i*480,(i+1)*480,0,false};
        }
        auto withAdd=convert(additions,additionSettings,additionEdits);
        check(withAdd.transpose==beforeAdd.transpose&&withAdd.notes[0].target==beforeAdd.notes[0].target,"new notes do not change original octave fitting");
        check(withAdd.edited==12&&withAdd.notes[12].target==0&&close(withAdd.duration,6.5),"additions retain requested key and extend duration");
        auto undoAdd=convert(additions,additionSettings);
        check(undoAdd.edited==0&&undoAdd.deleted==0&&undoAdd.excluded==0&&close(undoAdd.duration,.5),"undone additions are dormant, with original statistics and duration");
        check(undoAdd.notes[12].target==-1&&undoAdd.notes[12].mapping==Mapping::Excluded,"undo retains stable source IDs without playable notes");
        additionSettings.speed=2;withAdd=convert(additions,additionSettings,additionEdits);
        check(close(withAdd.notes[1].start,.25)&&close(withAdd.notes[1].duration,.25),"added one-beat notes follow tempo and speed");
        additionSettings.enabled[0]=false;
        check(convert(additions,additionSettings,additionEdits).excluded==13,"additions follow track mute");

        smf::MidiFile poly;poly.setTPQ(1000);poly.addTempo(0,0,60);
        for(int pitch:pitches)add(poly,0,0,pitch,0,30);
        add(poly,0,0,61,0,30);add(poly,0,0,62,10,30);add(poly,0,0,60,40,10);add(poly,0,0,60,90,10);
        auto ps=parseMidi(serialize(poly));auto pr=convert(ps,defaultSettings(ps));
        check(pr.chords==1,"nine-key chord allowed");check(pr.merged==1,"duplicate target merged");
        check(pr.conflicts==2,"only same-key short repeat conflicts");
        smf::MidiFile cross;cross.setTPQ(1000);cross.addTempo(0,0,60);add(cross,0,0,60,0,30);add(cross,0,0,62,10,30);
        auto cs=parseMidi(serialize(cross));check(convert(cs,defaultSettings(cs)).conflicts==0,"different overlapping keys are valid");

        smf::MidiFile multi;multi.setTPQ(480);multi.addTrack();add(multi,0,0,60,0);add(multi,1,9,45,0);add(multi,1,2,62,480);
        auto ms=parseMidi(serialize(multi));auto mopt=defaultSettings(ms);check(ms.tracks.size()==3,"track/channel split");
        check(convert(ms,mopt).excluded==1,"channel 10 initially excluded");mopt.solo[0]=true;
        check(convert(ms,mopt).exact==1,"solo filter");mopt.enabled[0]=false;check(convert(ms,mopt).exact==1,"muted solo is ignored");

        smf::MidiFile pairing;pairing.setTPQ(480);pairing.addNoteOn(0,0,0,60,100);pairing.addNoteOn(0,120,0,60,110);
        pairing.addNoteOn(0,240,0,60,0);pairing.addNoteOff(0,480,0,60);pairing.addNoteOff(0,500,0,64);
        auto paired=parseMidi(serialize(pairing));check(paired.notes.size()==2,"velocity zero is off");
        check(paired.notes[0].start==0&&paired.notes[0].end==240&&paired.notes[1].start==120&&paired.notes[1].end==480,"FIFO pairing");
        check(paired.warnings.size()==2,"orphan and overlap diagnostics");
        smf::MidiFile missing;missing.setTPQ(480);missing.addNoteOn(0,0,0,60,100);missing.addText(0,960,"end");
        auto missingSong=parseMidi(serialize(missing));check(missingSong.notes[0].end==960&&!missingSong.warnings.empty(),"missing off truncation");

        auto malformed=bytes;malformed[9]=2;check(rejects(malformed),"reject type 2");malformed=bytes;malformed[12]=char(0xe7);check(rejects(malformed),"reject SMPTE");
        check(rejects("not midi"),"reject non MIDI");
        for(size_t n=0;n<bytes.size();++n)check(rejects(bytes.substr(0,n)),"reject truncated bytes");
        auto invalidBpm=serialize(tempo);auto where=invalidBpm.find(std::string("\xff\x51\x03",3));
        invalidBpm[where+3]=invalidBpm[where+4]=invalidBpm[where+5]=0;check(rejects(invalidBpm),"reject zero tempo");
        smf::MidiFile empty;empty.setTPQ(480);auto emptySong=parseMidi(serialize(empty));check(emptySong.notes.empty(),"empty MIDI");
        std::atomic_bool cancel{true};bool cancelled=false;try{parseMidi(bytes,&cancel);}catch(...){cancelled=true;}check(cancelled,"cancellation");

        Song large;large.ppq=480;large.tempos={{0,500000,0}};large.tracks={{"stress",0,0,100000}};
        for(int i=0;i<100000;++i)large.notes.push_back({0,pitches[i%9],100,i*120,i*120+100});large.endTick=12000000;
        const auto start=std::chrono::steady_clock::now();auto lr=convert(large,defaultSettings(large));
        check(lr.exact==100000&&lr.conflicts==0,"100k conversion");
        std::cout<<"100k-note conversion: "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<" ms\n";

        if(argc>1) {
            std::filesystem::path dir(argv[1]);std::filesystem::create_directories(dir);
            save(dir/"nine-keys.mid",bytes);save(dir/"tempo-change.mid",serialize(tempo));save(dir/"empty.mid",serialize(empty));save(dir/"invalid.mid","not a MIDI file");
            smf::MidiFile demo;demo.setTPQ(480);demo.addTrack(2);demo.addTempo(0,0,112);demo.addTempo(0,15360,128);
            demo.addTrackName(1,0,"Melody");demo.addTrackName(2,0,"Harmony");
            const int melody[]{60,62,64,62,60,59,57,55,53,55,57,59,60,61,62,64};
            for(int phrase=0;phrase<4;++phrase)for(int i=0;i<16;++i)add(demo,1,0,melody[i],phrase*7680+i*480, i%4==3?360:210);
            for(int i=0;i<16;++i){add(demo,2,1,i%2?52:45,i*1920,1200);add(demo,2,1,i%2?57:55,i*1920,1200);}
            add(demo,2,9,36,0,120);save(dir/"studio-demo.mid",serialize(demo));
        }
        std::cout<<"PASS: "<<checks<<" core checks\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
