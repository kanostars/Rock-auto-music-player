#include "audio/audio_player.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace rock;
namespace {
int checks=0;
void check(bool b,const char* message){++checks;if(!b)throw std::runtime_error(message);}
std::vector<float> render(HandpanMixer& mixer,size_t frames,float volume=.6f) {
    std::vector<float> pcm(frames*2);
    for(size_t i=0;i<frames;i+=256)mixer.render(pcm.data()+i*2,std::min(size_t(256),frames-i),volume);
    return pcm;
}
double energy(const std::vector<float>& pcm) {double sum=0;for(float x:pcm)sum+=x*x;return sum/pcm.size();}
void writeWave(const QString& path,const std::vector<float>& pcm) {
    QByteArray bytes;
    auto u16=[&](uint16_t x){bytes+=char(x&255);bytes+=char(x>>8);};
    auto u32=[&](uint32_t x){u16(x&65535);u16(x>>16);};
    bytes+="RIFF";u32(36+pcm.size()*2);bytes+="WAVEfmt ";u32(16);u16(1);u16(2);u32(audioRate);u32(audioRate*4);u16(4);u16(16);bytes+="data";u32(pcm.size()*2);
    for(float x:pcm)u16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(std::clamp(x,-1.f,1.f)*32767))));
    QFile out(path);check(out.open(QIODevice::WriteOnly),"open rendered WAV");check(out.write(bytes)==bytes.size(),"write rendered WAV");
}
Song makeSong(){Song s;s.ppq=480;s.tempos={{0,500000,0}};s.tracks={{"test",0,0,1}};s.notes={{0,57,100,480,960}};s.endTick=960;return s;}
void awaitFrames(AudioPlayer& player,double start) {
    for(int i=0;i<100&&player.position()<=start+.05;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(player.position()>start+.05,"audio device advances callback sample clock");
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        auto bank=loadHandpanBank();
        QFile manifest(":/handpan/manifest.json");check(manifest.open(QIODevice::ReadOnly),"embedded sample provenance exists");
        auto records=QJsonDocument::fromJson(manifest.readAll()).object()["notes"].toArray();
        check(records.size()==9*sampleVariants,"all 36 source records embedded");
        std::array<std::array<bool,sampleVariants>,9> seen{};
        for(const auto& record:records) {
            auto r=record.toObject();auto key=r["key"].toString().at(0).toLatin1();
            auto keyIt=std::find(keys.begin(),keys.end(),key);check(keyIt!=keys.end(),"manifest key is supported");
            auto index=std::distance(keys.begin(),keyIt);int variant=r["variant"].toInt()-1;
            check(variant>=0&&variant<sampleVariants,"manifest variant in range");
            check(!seen[index][variant],"each key and variant has one resource");seen[index][variant]=true;
            QFile file(":/handpan/"+r["file"].toString());check(file.open(QIODevice::ReadOnly),"original sample embedded");
            auto bytes=file.readAll();
            check(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()==r["sha256"].toString().toLatin1(),"embedded WAV matches extracted source hash");
            const auto& sample=bank[index][variant];
            check(sample.size()==size_t(r["frames"].toInt())*2,"stereo decoding preserves original frame count and duration");
            check(*std::max_element(sample.begin(),sample.end())>.05f,"sample contains sound");
            for(float value:sample)check(std::isfinite(value)&&std::abs(value)<=1.f,"decoded PCM is finite and in range");
        }
        Song song=makeSong();auto result=convert(song,defaultSettings(song));
        if(app.arguments().contains("--device-smoke")) {
            AudioPlayer real;QString error;check(real.play(song,result,0,error),qPrintable(error));
            awaitFrames(real,0);std::this_thread::sleep_for(std::chrono::milliseconds(850));real.pause();
            std::cout<<"System playback device opened and rendered handpan audio successfully.\n";return 0;
        }
        HandpanMixer mixer(bank);mixer.prepare(song,result);
        auto silence=render(mixer,audioRate/2);check(energy(silence)==0,"no strike before MIDI onset");
        auto strike=render(mixer,audioRate/2);check(energy(strike)>.0001,"strike rendered at MIDI onset");
        auto tail=render(mixer,audioRate/2);check(energy(tail)>0,"note off retains release tail");
        auto after=render(mixer,1024);check(energy(after)==0&&mixer.cursor()==mixer.endFrame(),"end emits silence without clock drift");
        mixer.prepare(song,result);auto continuous=render(mixer,audioRate);
        mixer.seek(.75);auto sought=render(mixer,4410);
        for(size_t i=300;i<4410;++i)for(size_t channel=0;channel<2;++channel)
            check(std::abs(sought[i*2+channel]-continuous[(audioRate*3/4+i)*2+channel])<1e-6,"seek resumes both channels at correct sample offset");
        mixer.prepare(song,result);render(mixer,audioRate/2,0);check(energy(render(mixer,4800,0))<1e-10,"volume zero mutes sound");
        auto edited=convert(song,defaultSettings(song),{{0,{0,240,8,false}}});mixer.prepare(song,edited);
        check(mixer.events()[0].start==0&&mixer.events()[0].target==8,"manual timing and target drive audio");
        auto deleted=convert(song,defaultSettings(song),{{0,{0,240,8,true}}});mixer.prepare(song,deleted);
        check(mixer.events().empty()&&energy(render(mixer,4800))==0,"deleted notes are silent");
        auto muted=defaultSettings(song);muted.enabled[0]=false;mixer.prepare(song,convert(song,muted));check(mixer.events().empty(),"muted track is silent");
        auto faster=defaultSettings(song);faster.speed=2;mixer.prepare(song,convert(song,faster));
        check(mixer.events()[0].start==audioRate/4&&mixer.events()[0].release==audioRate/2,"tempo changes timing without resampling pitch");
        song.notes={{0,57,80,0,960},{0,57,100,0,480},{0,60,90,0,960},{0,57,100,60,960}};
        mixer.prepare(song,convert(song,defaultSettings(song)));check(mixer.events().size()==3,"same-key simultaneous duplicates merge, repeated strikes remain");
        check(mixer.events()[0].release==audioRate,"merged strike uses longest duration");
        auto chord=render(mixer,9600);check(energy(chord)>.001,"polyphonic chord and repeat have nonzero audio");
        for(float x:chord)check(std::isfinite(x)&&std::abs(x)<1,"mix is finite and limited");
        auto repeated=makeSong();repeated.notes.clear();
        for(int i=0;i<6;++i)repeated.notes.push_back({0,57,100,i*480,i*480+240});
        repeated.notes.push_back(repeated.notes.front()); // Chord duplicate must not consume a variant.
        repeated.endTick=6*480;
        auto repeatedResult=convert(repeated,defaultSettings(repeated));mixer.prepare(repeated,repeatedResult);
        check(mixer.events().size()==6,"duplicate merging precedes variant selection");
        for(size_t i=0;i<6;++i)check(mixer.events()[i].variant==int(i)%sampleVariants,"all four variants cycle without adjacent repeats");
        auto uninterrupted=render(mixer,audioRate*3);
        mixer.prepare(repeated,repeatedResult);mixer.seek(1.75);auto resumed=render(mixer,4410);
        const size_t resumeFrame=audioRate*7/4;
        for(size_t i=300;i<4410;++i)for(size_t channel=0;channel<2;++channel)
            check(std::abs(resumed[i*2+channel]-uninterrupted[(resumeFrame+i)*2+channel])<1e-6,"prepare then resume retains non-first variant and stereo tail");
        // Independent channel fixture catches accidental mono downmix or channel swapping.
        SampleBank stereoBank;
        for(auto& variants:stereoBank)for(auto& sample:variants) {
            sample.resize(audioRate*2);
            for(size_t i=0;i<sample.size();i+=2){sample[i]=.25f;sample[i+1]=-.125f;}
        }
        HandpanMixer stereoMixer(std::move(stereoBank));auto stereoSong=makeSong();
        stereoSong.notes={{0,57,127,0,480}};stereoMixer.prepare(stereoSong,convert(stereoSong,defaultSettings(stereoSong)));
        auto separated=render(stereoMixer,1000);
        check(separated[1800]>0&&separated[1801]<0,"left and right remain independent with correct polarity");
        auto sustained=makeSong();sustained.notes={{0,45,127,0,4800}};sustained.endTick=4800;
        mixer.prepare(sustained,convert(sustained,defaultSettings(sustained)));
        auto sustainedEvent=mixer.events().front();
        check(sustainedEvent.end-sustainedEvent.start==bank[sustainedEvent.target][0].size()/2,"long MIDI notes use the full original sample, without 2.6s truncation");
        AudioPlayer player(AudioBackend::NullTest);QString error;
        check(player.play(song,result,0,error),"test device start");awaitFrames(player,0);player.pause();
        auto paused=player.position();std::this_thread::sleep_for(std::chrono::milliseconds(40));check(player.position()==paused,"pause freezes sample clock");
        check(player.play(song,result,paused,error),"resume device");awaitFrames(player,paused);player.pause();
        auto conflictSong=makeSong();conflictSong.notes.push_back({0,57,100,490,960});
        auto conflictResult=convert(conflictSong,defaultSettings(conflictSong));check(conflictResult.conflicts==1,"conflict fixture has repeated same key");
        check(!player.play(conflictSong,conflictResult,0,error)&&!player.running(),"audio engine refuses conflicting conversion");
        check(error.contains("同键冲突"),"conflict playback returns actionable error");
        if(argc>2) {
            QDir().mkpath(QString::fromLocal8Bit(argv[1]));QFile midi(QString::fromLocal8Bit(argv[2])+"/pirates.mid");check(midi.open(QIODevice::ReadOnly),"read reference MIDI");
            auto pirates=parseMidi(midi.readAll().toStdString());auto converted=convert(pirates,defaultSettings(pirates));mixer.prepare(pirates,converted);
            auto demo=render(mixer,audioRate*20);check(energy(demo)>.0001,"reference melody audio not empty");
            writeWave(QString::fromLocal8Bit(argv[1])+"/pirates-handpan-preview.wav",demo);
        }
        std::cout<<"PASS: "<<checks<<" audio checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
