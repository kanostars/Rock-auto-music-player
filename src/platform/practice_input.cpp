#include "practice_input.h"
#include <algorithm>
#include <cstdint>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rock {
struct PracticeInput::Private {
    PracticeInput* owner;
    std::array<int,9> keys{};
    std::array<bool,9> held{},delivered{};
    std::uint64_t generation{};
    bool active{};
#ifdef Q_OS_WIN
    HHOOK hook{};
    static Private* observing;
    void queue(int target,bool down){
        const auto token=generation;
        QMetaObject::invokeMethod(owner,[this,token,target,down]{
            if(active&&token==generation)emit owner->keyChanged(target,down);
        },Qt::QueuedConnection);
    }
    static LRESULT CALLBACK keyEvent(int code,WPARAM message,LPARAM data){
        auto* self=observing;
        if(code==HC_ACTION&&self&&self->active){
            const auto* event=reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
            const bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN;
            const bool up=message==WM_KEYUP||message==WM_SYSKEYUP;
            if((down||up)&&!(event->flags&LLKHF_INJECTED)){
                const auto native=event->vkCode;
                const bool modifier=native==VK_CONTROL||native==VK_LCONTROL||native==VK_RCONTROL||
                    native==VK_MENU||native==VK_LMENU||native==VK_RMENU||native==VK_LWIN||native==VK_RWIN;
                if(down&&modifier){
                    for(int target=0;target<9;++target)if(self->delivered[target]){
                        self->delivered[target]=false;self->queue(target,false);
                    }
                }
                const auto found=std::find(self->keys.begin(),self->keys.end(),static_cast<int>(native));
                if(found!=self->keys.end()){
                    const int target=static_cast<int>(found-self->keys.begin());
                    if(up){
                        self->held[target]=false;
                        if(self->delivered[target]){self->delivered[target]=false;self->queue(target,false);}
                    }else if(!self->held[target]){
                        self->held[target]=true;
                        const bool modified=(GetAsyncKeyState(VK_CONTROL)&0x8000)||(GetAsyncKeyState(VK_MENU)&0x8000)||
                            (GetAsyncKeyState(VK_LWIN)&0x8000)||(GetAsyncKeyState(VK_RWIN)&0x8000)||(event->flags&LLKHF_ALTDOWN);
                        if(!modified){self->delivered[target]=true;self->queue(target,true);}
                    }
                }
            }
        }
        // This observer must never suppress a key or synthesize game input.
        return CallNextHookEx(self?self->hook:nullptr,code,message,data);
    }
#endif
};
#ifdef Q_OS_WIN
PracticeInput::Private* PracticeInput::Private::observing{};
#endif
PracticeInput::PracticeInput(QObject* parent):QObject(parent),d_(std::make_unique<Private>()){d_->owner=this;}
PracticeInput::~PracticeInput(){stop();}
bool PracticeInput::start(const std::array<int,9>& keys,QString& error){
    error.clear();
    if(d_->active&&d_->keys==keys)return true;
    stop();
    for(size_t index=0;index<keys.size();++index){
        const int key=keys[index];
        if(!((key>='A'&&key<='Z')||(key>='0'&&key<='9'))||
           std::find(keys.begin(),keys.begin()+index,key)!=keys.begin()+index){
            error="跟弹按键需要使用九个不同的英文字母或数字。";return false;
        }
    }
#ifdef Q_OS_WIN
    if(Private::observing&&Private::observing!=d_.get()){
        error="另一处跟弹正在接收按键，请先退出后重试。";return false;
    }
    d_->keys=keys;d_->held.fill(false);d_->delivered.fill(false);
    for(int target=0;target<9;++target)d_->held[target]=(GetAsyncKeyState(keys[target])&0x8000)!=0;
    d_->hook=SetWindowsHookExW(WH_KEYBOARD_LL,Private::keyEvent,GetModuleHandleW(nullptr),0);
    if(!d_->hook){error="无法接收游戏内跟弹按键，请重试或返回跟练页面。";return false;}
    d_->active=true;Private::observing=d_.get();return true;
#else
    error="游戏内小窗跟弹目前仅支持 Windows，请返回跟练页面练习。";return false;
#endif
}
void PracticeInput::stop(){
    d_->active=false;++d_->generation;
#ifdef Q_OS_WIN
    if(Private::observing==d_.get())Private::observing=nullptr;
    if(d_->hook){UnhookWindowsHookEx(d_->hook);d_->hook=nullptr;}
#endif
    d_->held.fill(false);d_->delivered.fill(false);
}
}
