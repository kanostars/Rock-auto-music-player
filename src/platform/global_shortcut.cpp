#include "global_shortcut.h"
#include <QCoreApplication>
#include <algorithm>
#include <memory>
#include <vector>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace rock {
namespace {
std::vector<GlobalShortcut*> shortcuts;
#ifdef Q_OS_WIN
HHOOK functionKeyHook{};
bool functionKeyDown{},consumedPress{};
void queueActivation(GlobalShortcut* shortcut){
    const auto key=shortcut->key();
    QMetaObject::invokeMethod(shortcut,[shortcut,key]{if(shortcut->enabled()&&shortcut->key()==key)emit shortcut->activated();},Qt::QueuedConnection);
}
LRESULT CALLBACK functionKeyEvent(int code,WPARAM message,LPARAM data){
    if(code==HC_ACTION){
        const auto* event=reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        if(event->vkCode==VK_F12){
            if(message==WM_KEYUP||message==WM_SYSKEYUP){const bool consumed=consumedPress;functionKeyDown=consumedPress=false;if(consumed)return 1;}
            else if(message==WM_KEYDOWN||message==WM_SYSKEYDOWN){
                if(!functionKeyDown){
                    functionKeyDown=true;consumedPress=false;
                    const unsigned modifiers=(GetAsyncKeyState(VK_MENU)&0x8000?MOD_ALT:0)|(GetAsyncKeyState(VK_CONTROL)&0x8000?MOD_CONTROL:0)|(GetAsyncKeyState(VK_SHIFT)&0x8000?MOD_SHIFT:0)|(GetAsyncKeyState(VK_LWIN)&0x8000||GetAsyncKeyState(VK_RWIN)&0x8000?MOD_WIN:0);
                    for(auto* shortcut:shortcuts)if(shortcut->enabled()){
                        const auto native=nativeHotkey(shortcut->key());
                        if(native.key==VK_F12&&native.modifiers==modifiers){queueActivation(shortcut);consumedPress=true;break;}
                    }
                }
                if(consumedPress)return 1;
            }
        }
    }
    return CallNextHookEx(functionKeyHook,code,message,data);
}
#endif
}
NativeHotkey nativeHotkey(const QKeySequence& sequence){
    if(sequence.count()!=1)return {};
    const auto combination=sequence[0];const auto modifiers=combination.keyboardModifiers();const int key=combination.key();
    if(modifiers&~(Qt::ControlModifier|Qt::AltModifier|Qt::ShiftModifier))return {};
    unsigned nativeKey=0;if((key>=Qt::Key_A&&key<=Qt::Key_Z)||(key>=Qt::Key_0&&key<=Qt::Key_9)){
        if(!(modifiers&(Qt::ControlModifier|Qt::AltModifier)))return {};
        nativeKey=key;
    }else if(key>=Qt::Key_F1&&key<=Qt::Key_F24)nativeKey=0x70+key-Qt::Key_F1;
    if(!nativeKey)return {};
    return {unsigned((modifiers&Qt::AltModifier?1:0)|(modifiers&Qt::ControlModifier?2:0)|(modifiers&Qt::ShiftModifier?4:0)),nativeKey,true};
}
GlobalShortcut::GlobalShortcut(int id,QObject* parent):QObject(parent),id_(id){shortcuts.push_back(this);QCoreApplication::instance()->installNativeEventFilter(this);}
GlobalShortcut::~GlobalShortcut(){disable();QCoreApplication::instance()->removeNativeEventFilter(this);std::erase(shortcuts,this);}
bool GlobalShortcut::enable(const QKeySequence& key,QString& error){
    if(enabled_&&key_==key)return true;
    disable();key_=key;if(key.isEmpty())return true;
    const auto native=nativeHotkey(key);if(!native.valid){error="全局快捷键格式无效，请在设置中重新配置。";return false;}
#ifdef Q_OS_WIN
    // Windows reserves F12 for debuggers; observe it without RegisterHotKey.
    if(native.key==VK_F12){
        if(!functionKeyHook){functionKeyHook=SetWindowsHookExW(WH_KEYBOARD_LL,functionKeyEvent,GetModuleHandleW(nullptr),0);functionKeyDown=consumedPress=false;}
        if(!functionKeyHook){error="无法启用 F12 快捷键，请重启程序后重试。";return false;}
    }else if(!RegisterHotKey(nullptr,id_,native.modifiers|MOD_NOREPEAT,native.key)){
        error=QString("快捷键 %1 已被其他程序占用，请在设置中更换。").arg(key.toString(QKeySequence::NativeText));return false;
    }
    enabled_=true;return true;
#else
    error="全局快捷键仅支持 Windows。";return false;
#endif
}
void GlobalShortcut::disable(){
    if(!enabled_)return;enabled_=false;
#ifdef Q_OS_WIN
    if(nativeHotkey(key_).key!=VK_F12)UnregisterHotKey(nullptr,id_);
    else if(std::none_of(shortcuts.begin(),shortcuts.end(),[](auto* shortcut){return shortcut->enabled()&&nativeHotkey(shortcut->key()).key==VK_F12;})){
        UnhookWindowsHookEx(functionKeyHook);functionKeyHook=nullptr;functionKeyDown=consumedPress=false;
    }
#endif
}
bool GlobalShortcut::nativeEventFilter(const QByteArray&,void* message,qintptr* result){
#ifdef Q_OS_WIN
    const auto* event=static_cast<MSG*>(message);
    if(enabled_&&event&&event->message==WM_HOTKEY&&event->wParam==static_cast<WPARAM>(id_)){
        const auto native=nativeHotkey(key_);
        if(LOWORD(event->lParam)!=native.modifiers||HIWORD(event->lParam)!=native.key)return false;
        if(result)*result=0;queueActivation(this);return true;
    }
#endif
    return false;
}
bool checkGlobalShortcuts(const ShortcutBindings& bindings,QString& error){
    std::vector<GlobalShortcut*> enabled;for(auto* shortcut:shortcuts)if(shortcut->enabled())enabled.push_back(shortcut);
    for(auto* shortcut:enabled)shortcut->disable();
    bool ready=true;
    {
        std::vector<std::unique_ptr<GlobalShortcut>> probes;
        for(const auto& definition:shortcutDefinitions())if(definition.global){
            auto probe=std::make_unique<GlobalShortcut>(0x5280+static_cast<int>(definition.action));
            ready=probe->enable(bindings[static_cast<size_t>(definition.action)],error);probes.push_back(std::move(probe));if(!ready)break;
        }
    }
    for(auto* shortcut:enabled){QString restoreError;if(!shortcut->enable(shortcut->key(),restoreError)){if(ready)error=restoreError;ready=false;}}
    return ready;
}
}
