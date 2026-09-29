#include "live_handpan.h"
#include <miniaudio.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rock {
LiveHandpanMixer::LiveHandpanMixer(SampleBank bank):bank_(std::move(bank)) {
    for(const auto& variants:bank_)for(const auto& sample:variants)
        if(sample.empty()||sample.size()%2)throw std::invalid_argument("手碟采样不完整。");
}
bool LiveHandpanMixer::strike(int target) {
    if(target<0||target>=9)return false;
    const unsigned write=write_.load(std::memory_order_relaxed),next=(write+1)%queue_.size();
    if(next==read_.load(std::memory_order_acquire))return false;
    queue_[write]=target;write_.store(next,std::memory_order_release);return true;
}
void LiveHandpanMixer::render(float* stereo,size_t frames) {
    auto read=read_.load(std::memory_order_relaxed);const auto end=write_.load(std::memory_order_acquire);
    while(read!=end) {
        const int target=queue_[read];auto voice=std::find_if(voices_.begin(),voices_.end(),[](const auto& v){return !v.sample;});
        if(voice==voices_.end())voice=std::max_element(voices_.begin(),voices_.end(),[](const auto& a,const auto& b){return a.frame<b.frame;});
        *voice={&bank_[target][variants_[target]],0};variants_[target]=(variants_[target]+1)%sampleVariants;
        read=(read+1)%queue_.size();
    }
    read_.store(read,std::memory_order_release);std::fill_n(stereo,frames*2,0.f);
    for(auto& voice:voices_)if(voice.sample) {
        const size_t count=std::min(frames,voice.sample->size()/2-voice.frame);
        for(size_t i=0;i<count;++i)for(size_t channel=0;channel<2;++channel)
            stereo[i*2+channel]+=(*voice.sample)[(voice.frame+i)*2+channel]*(100.f/127.f)*.6f;
        voice.frame+=count;if(voice.frame>=voice.sample->size()/2)voice={};
    }
    for(size_t i=0;i<frames*2;++i)stereo[i]=std::tanh(stereo[i]);
}
void LiveHandpanMixer::clear(){voices_.fill({});read_.store(0);write_.store(0);}
struct LiveHandpanPlayer::Impl {
    ma_device device{};bool initialized{};
    std::unique_ptr<LiveHandpanMixer> mixer;
    static void callback(ma_device* device,void* output,const void*,ma_uint32 frames) {
        static_cast<Impl*>(device->pUserData)->mixer->render(static_cast<float*>(output),frames);
    }
    ~Impl(){if(initialized)ma_device_uninit(&device);}
};
LiveHandpanPlayer::LiveHandpanPlayer():impl_(std::make_unique<Impl>()){}
LiveHandpanPlayer::~LiveHandpanPlayer()=default;
bool LiveHandpanPlayer::start(QString& error) {
    error.clear();if(running())return true;auto& p=*impl_;
    try{if(!p.mixer)p.mixer=std::make_unique<LiveHandpanMixer>(loadHandpanBank());}
    catch(const std::exception& e){error=QString::fromUtf8(e.what());return false;}
    if(!p.initialized) {
        auto config=ma_device_config_init(ma_device_type_playback);config.playback.format=ma_format_f32;
        config.playback.channels=2;config.sampleRate=audioRate;config.periodSizeInMilliseconds=10;
        config.dataCallback=Impl::callback;config.pUserData=&p;
        const ma_backend system[]{ma_backend_wasapi,ma_backend_dsound,ma_backend_winmm};
        auto result=ma_device_init_ex(system,3,nullptr,&config,&p.device);
        if(result!=MA_SUCCESS){error=QString("无法打开扬声器 / 耳机（%1），请检查音频设备。重新聚焦窗口可重试。").arg(result);return false;}
        p.initialized=true;
    }
    auto result=ma_device_start(&p.device);
    if(result!=MA_SUCCESS){ma_device_uninit(&p.device);p.initialized=false;error=QString("音频启动失败（%1），重新聚焦窗口可重试。").arg(result);return false;}
    return true;
}
bool LiveHandpanPlayer::strike(int target){return running()&&impl_->mixer->strike(target);}
void LiveHandpanPlayer::stop(){if(impl_->initialized)ma_device_stop(&impl_->device);if(impl_->mixer)impl_->mixer->clear();}
bool LiveHandpanPlayer::running() const{return impl_->initialized&&ma_device_is_started(&impl_->device);}
}
