#include "practice_metronome.h"
#include "audio_player.h"
#include "handpan_mixer.h"
#include <miniaudio.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace rock {
namespace {
constexpr size_t maxBeats=500000,maxTimingEvents=2000000;
constexpr double maxSeconds=1e12,maxLiveBpm=2400;
constexpr uint64_t periodPrecision=1000000;
void validateTiming(const Song& song,const Settings& settings) {
    if(song.ppq<=0||!std::isfinite(settings.bpm)||settings.bpm<=0||
       !std::isfinite(settings.speed)||settings.speed<=0)
        throw std::invalid_argument("节拍器的速度或 MIDI 时间参数无效。");
    if(song.tempos.size()>maxTimingEvents||song.timeSignatures.size()>maxTimingEvents)
        throw std::invalid_argument("节拍器的速度 / 拍号事件过多。");
}
bool validSignature(const TimeSignature& signature) {
    return signature.tick>=0&&signature.numerator>=1&&signature.numerator<=255&&
        signature.denominator>0&&(signature.denominator&(signature.denominator-1))==0;
}
double fractionalSeconds(const Song& song,const Settings& settings,double tick) {
    if(settings.fixedTempo)return tick/song.ppq*60/settings.bpm/settings.speed;
    auto tempo=std::upper_bound(song.tempos.begin(),song.tempos.end(),tick,
        [](double value,const Tempo& event){return value<event.tick;});
    if(tempo==song.tempos.begin())return tick*.5/song.ppq/settings.speed;
    --tempo;
    return (tempo->seconds+(tick-tempo->tick)*tempo->micros/(song.ppq*1000000.0))/settings.speed;
}
double fractionalTick(const Song& song,const Settings& settings,double seconds) {
    const double original=seconds*settings.speed;
    if(settings.fixedTempo)return original*song.ppq*settings.bpm/60;
    auto tempo=std::upper_bound(song.tempos.begin(),song.tempos.end(),original,
        [](double value,const Tempo& event){return value<event.seconds;});
    if(tempo==song.tempos.begin())return original*song.ppq*2;
    --tempo;
    if(tempo->tick<0||tempo->micros<=0||!std::isfinite(tempo->seconds)||tempo->seconds<0)
        throw std::invalid_argument("节拍器的 MIDI 速度事件无效。");
    return tempo->tick+(original-tempo->seconds)*song.ppq*1000000.0/tempo->micros;
}
uint64_t packedRhythm(double bpm,int beatsPerBar) {
    if(!std::isfinite(bpm)||bpm<1||bpm>maxLiveBpm||beatsPerBar<1||beatsPerBar>255)
        throw std::invalid_argument("节拍器速度需在 1～2400 BPM 之间，每小节需有 1～255 拍。");
    const auto period=static_cast<uint64_t>(std::llround(audioRate*60.0/bpm*periodPrecision));
    return (period<<8)|static_cast<unsigned>(beatsPerBar);
}
}

std::vector<MetronomeBeat> makeMetronomeBeats(const Song& song,const Settings& settings,double endSeconds) {
    validateTiming(song,settings);
    if(!std::isfinite(endSeconds)||endSeconds<0||endSeconds>maxSeconds)
        throw std::invalid_argument("节拍器时间超出支持范围。");
    int previousTick=-1;double previousSeconds=-1;
    for(const auto& tempo:song.tempos) {
        if(tempo.tick<0||tempo.tick<=previousTick||tempo.micros<=0||
           !std::isfinite(tempo.seconds)||tempo.seconds<0||tempo.seconds<previousSeconds)
            throw std::invalid_argument("节拍器的 MIDI 速度事件无效。");
        previousTick=tempo.tick;previousSeconds=tempo.seconds;
    }
    previousTick=-1;
    for(const auto& signature:song.timeSignatures) {
        if(!validSignature(signature)||signature.tick<=previousTick)
            throw std::invalid_argument("节拍器的 MIDI 拍号事件无效。");
        previousTick=signature.tick;
    }
    const long double endTick=fractionalTick(song,settings,endSeconds);
    if(!std::isfinite(endTick)||endTick<0)
        throw std::invalid_argument("节拍器时间超出支持范围。");
    std::vector<MetronomeBeat> result;
    TimeSignature signature{0,4,4};long double begin=0;
    auto append=[&](long double finish) {
        if(finish<=begin)return;
        const long double ticksPerBeat=static_cast<long double>(song.ppq)*4/signature.denominator;
        const long double count=std::ceil((finish-begin)/ticksPerBeat);
        if(!std::isfinite(count)||count>maxBeats-result.size())
            throw std::length_error("节拍器超过 50 万拍上限，请缩短歌曲或降低拍号密度。");
        const auto beats=static_cast<size_t>(count);
        for(size_t index=0;index<beats;++index) {
            const long double tick=begin+index*ticksPerBeat;
            if(tick>=finish)break;
            const double seconds=fractionalSeconds(song,settings,static_cast<double>(tick));
            if(!std::isfinite(seconds)||seconds<0)
                throw std::invalid_argument("节拍器时间超出支持范围。");
            if(seconds<endSeconds)result.push_back({seconds,index%signature.numerator==0});
        }
    };
    for(const auto& change:song.timeSignatures) {
        if(change.tick==0){signature=change;continue;}
        if(change.tick>=endTick)break;
        append(change.tick);begin=change.tick;signature=change;
    }
    append(endTick);return result;
}

std::pair<double,int> metronomeRhythmAt(const Song& song,const Settings& settings,double seconds) {
    validateTiming(song,settings);
    if(!std::isfinite(seconds)||seconds<0||seconds>maxSeconds)
        throw std::invalid_argument("节拍器时间超出支持范围。");
    const double tick=fractionalTick(song,settings,seconds);
    if(!std::isfinite(tick))throw std::invalid_argument("节拍器时间超出支持范围。");
    TimeSignature signature{0,4,4};
    auto meter=std::upper_bound(song.timeSignatures.begin(),song.timeSignatures.end(),tick,
        [](double value,const TimeSignature& event){return value<event.tick;});
    if(meter!=song.timeSignatures.begin())signature=*std::prev(meter);
    if(!validSignature(signature))throw std::invalid_argument("节拍器的 MIDI 拍号事件无效。");
    double bpm=settings.bpm;
    if(!settings.fixedTempo) {
        auto tempo=std::upper_bound(song.tempos.begin(),song.tempos.end(),tick,
            [](double value,const Tempo& event){return value<event.tick;});
        if(tempo==song.tempos.begin())bpm=120;
        else {
            --tempo;
            if(tempo->micros<=0)throw std::invalid_argument("节拍器的 MIDI 速度事件无效。");
            bpm=60000000.0/tempo->micros;
        }
    }
    bpm*=settings.speed*signature.denominator/4;
    if(!std::isfinite(bpm)||bpm<=0)throw std::invalid_argument("节拍器的 MIDI 速度超出支持范围。");
    return {bpm,signature.numerator};
}

struct MetronomePlayer::Impl {
    struct ScheduledBeat {uint64_t frame{};bool accent{};};
    struct Voice {const std::array<float,1764>* sound{};size_t frame{};};
    enum class Mode {Scheduled,Live};
    ma_device device{};bool initialized{};Mode mode{Mode::Scheduled};
    std::vector<ScheduledBeat> beats;
    std::array<std::array<float,1764>,2> sounds{};
    std::array<Voice,8> voices{};
    std::atomic<float> volume{.6f};
    std::atomic<uint64_t> rhythm{packedRhythm(120,4)};
    uint64_t cursor{},endFrame{},appliedRhythm{};size_t next{};
    const AudioPlayer* clock{};double sourceStart{},playSpeed{1};bool clockReady{};
    double nextLive{},livePeriod{};unsigned beatInBar{},liveBeats{4};
    Impl() {
        constexpr double pi=3.14159265358979323846;
        for(size_t accent=0;accent<sounds.size();++accent)for(size_t frame=0;frame<sounds[accent].size();++frame) {
            const double time=double(frame)/audioRate,attack=std::min(1.0,time/.0007);
            const double tone=std::sin(2*pi*(accent?1560:1040)*time)+.25*std::sin(2*pi*(accent?3120:2080)*time);
            sounds[accent][frame]=static_cast<float>(tone*attack*std::exp(-time*150)*.3);
        }
    }
    void activate(bool accent) {
        auto voice=std::find_if(voices.begin(),voices.end(),[](const auto& value){return !value.sound;});
        if(voice==voices.end())voice=std::max_element(voices.begin(),voices.end(),[](const auto& a,const auto& b){return a.frame<b.frame;});
        *voice={&sounds[accent?1:0],0};
    }
    void applyRhythm() {
        const auto current=rhythm.load(std::memory_order_acquire);
        if(current==appliedRhythm)return;
        const double period=double(current>>8)/periodPrecision;
        const unsigned count=static_cast<unsigned>(current&255);
        if(livePeriod>0)nextLive=cursor+std::clamp((nextLive-cursor)/livePeriod,0.0,1.0)*period;
        if(count!=liveBeats)beatInBar=0;
        livePeriod=period;liveBeats=count;appliedRhythm=current;
    }
    void synchronize(ma_uint32 frames) {
        if(!clock)return;
        const double position=std::clamp((clock->position()-sourceStart)/playSpeed*audioRate,0.0,double(endFrame));
        const auto target=static_cast<uint64_t>(std::llround(position));
        const uint64_t tolerance=uint64_t(frames)*2;
        if((!clockReady&&target>tolerance)||target>cursor+tolerance||target+tolerance<cursor) {
            next=static_cast<size_t>(std::lower_bound(beats.begin(),beats.end(),target,
                [](const auto& beat,uint64_t frame){return beat.frame<frame;})-beats.begin());
            voices.fill({});
        }
        cursor=target;clockReady=true;
    }
    static void callback(ma_device* device,void* output,const void*,ma_uint32 frames) {
        auto& self=*static_cast<Impl*>(device->pUserData);
        auto* stereo=static_cast<float*>(output);std::fill_n(stereo,size_t(frames)*2,0.f);
        const float gain=self.volume.load(std::memory_order_relaxed);
        if(self.mode==Mode::Live)self.applyRhythm();
        else self.synchronize(frames);
        for(ma_uint32 frame=0;frame<frames;++frame) {
            if(self.mode==Mode::Scheduled) {
                if(self.cursor>=self.endFrame){self.voices.fill({});break;}
                while(self.next<self.beats.size()&&self.beats[self.next].frame<=self.cursor)
                    self.activate(self.beats[self.next++].accent);
            } else if(double(self.cursor)>=self.nextLive) {
                self.activate(self.beatInBar==0);self.beatInBar=(self.beatInBar+1)%self.liveBeats;
                self.nextLive+=self.livePeriod;
            }
            float sample=0;
            for(auto& voice:self.voices)if(voice.sound) {
                sample+=(*voice.sound)[voice.frame++];
                if(voice.frame>=voice.sound->size())voice={};
            }
            sample=std::clamp(sample*gain,-1.f,1.f);
            stereo[size_t(frame)*2]=sample;stereo[size_t(frame)*2+1]=sample;++self.cursor;
        }
    }
    bool startDevice(QString& error) {
        if(!initialized) {
            auto config=ma_device_config_init(ma_device_type_playback);
            config.playback.format=ma_format_f32;config.playback.channels=2;config.sampleRate=audioRate;
            config.periodSizeInMilliseconds=10;config.dataCallback=callback;config.pUserData=this;
            const ma_backend backends[]{ma_backend_wasapi,ma_backend_dsound,ma_backend_winmm};
            const auto result=ma_device_init_ex(backends,3,nullptr,&config,&device);
            if(result!=MA_SUCCESS){error=QString("无法打开节拍器音频设备（%1），请检查扬声器 / 耳机。").arg(result);return false;}
            initialized=true;
        }
        const auto result=ma_device_start(&device);
        if(result!=MA_SUCCESS) {
            ma_device_uninit(&device);initialized=false;
            error=QString("节拍器启动失败（%1），请检查音频输出设备后重试。").arg(result);return false;
        }
        error.clear();return true;
    }
    ~Impl(){if(initialized)ma_device_uninit(&device);}
};
MetronomePlayer::MetronomePlayer():impl_(std::make_unique<Impl>()){}
MetronomePlayer::~MetronomePlayer()=default;
bool MetronomePlayer::play(const std::vector<MetronomeBeat>& beats,double seconds,double end,double speed,QString& error,const AudioPlayer* clock) {
    stop();auto& p=*impl_;
    if(!std::isfinite(speed)||speed<.25||speed>4) {
        error=QStringLiteral("节拍器播放速度需在 0.25～4 倍之间。");return false;
    }
    if(!std::isfinite(seconds)||!std::isfinite(end)||seconds<0||end<=seconds||end>maxSeconds) {
        error=QStringLiteral("节拍器播放区间无效。");return false;
    }
    try {
        if(beats.size()>maxBeats)throw std::length_error("节拍器超过 50 万拍上限。");
        double previous=-1;
        for(const auto& beat:beats) {
            if(!std::isfinite(beat.seconds)||beat.seconds<0||beat.seconds>maxSeconds||beat.seconds<=previous)
                throw std::invalid_argument("节拍器拍点时间无效或顺序错误。");
            previous=beat.seconds;
        }
        p.beats.clear();p.beats.reserve(beats.size());
        unsigned simultaneous=0;uint64_t lastFrame=std::numeric_limits<uint64_t>::max();
        for(const auto& beat:beats)if(beat.seconds>=seconds&&beat.seconds<end) {
            const double frame=(beat.seconds-seconds)/speed*audioRate;
            if(!std::isfinite(frame)||frame>double(std::numeric_limits<uint64_t>::max()/2))
                throw std::invalid_argument("节拍器时间超出支持范围。");
            const auto onset=static_cast<uint64_t>(std::llround(frame));
            simultaneous=onset==lastFrame?simultaneous+1:1;lastFrame=onset;
            if(simultaneous>8)throw std::invalid_argument("节拍器拍点过密，请降低歌曲速度或拍号密度。");
            p.beats.push_back({onset,beat.accent});
        }
        p.endFrame=static_cast<uint64_t>(std::ceil((end-seconds)/speed*audioRate));
    } catch(const std::exception& exception){p.beats.clear();error=QString::fromUtf8(exception.what());return false;}
    p.mode=Impl::Mode::Scheduled;p.cursor=0;p.next=0;
    p.clock=clock;p.clockReady=false;p.sourceStart=seconds;p.playSpeed=speed;
    return p.startDevice(error);
}
bool MetronomePlayer::start(double bpm,int beatsPerBar,QString& error) {
    stop();auto& p=*impl_;
    try{p.rhythm.store(packedRhythm(bpm,beatsPerBar),std::memory_order_release);}
    catch(const std::exception& exception){error=QString::fromUtf8(exception.what());return false;}
    p.mode=Impl::Mode::Live;p.clock=nullptr;p.beats.clear();p.cursor=0;p.nextLive=0;p.livePeriod=0;
    p.appliedRhythm=0;p.beatInBar=0;p.liveBeats=static_cast<unsigned>(beatsPerBar);
    return p.startDevice(error);
}
void MetronomePlayer::setRhythm(double bpm,int beatsPerBar) {
    impl_->rhythm.store(packedRhythm(bpm,beatsPerBar),std::memory_order_release);
}
void MetronomePlayer::setVolume(float volume) {
    impl_->volume.store(std::isfinite(volume)?std::clamp(volume,0.f,1.f):0.f,std::memory_order_relaxed);
}
void MetronomePlayer::stop() {
    if(impl_->initialized)ma_device_stop(&impl_->device);
    impl_->voices.fill({});
}
}
