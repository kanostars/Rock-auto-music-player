#include "key_output.h"
#include <QCoreApplication>
#include <QLibrary>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "interception.h"
namespace rock {
namespace {
class Driver {
public:
    QLibrary library{QCoreApplication::applicationDirPath()+"/interception.dll"};
    decltype(&interception_create_context) create{};
    decltype(&interception_destroy_context) destroy{};
    decltype(&interception_get_hardware_id) hardware{};
    decltype(&interception_send) send{};
    InterceptionContext context{};
    ~Driver(){close();}
    void close(){if(context){destroy(context);context=nullptr;}}
    bool open(QString& error){
        if(context)return true;
        create=reinterpret_cast<decltype(create)>(library.resolve("interception_create_context"));
        destroy=reinterpret_cast<decltype(destroy)>(library.resolve("interception_destroy_context"));
        hardware=reinterpret_cast<decltype(hardware)>(library.resolve("interception_get_hardware_id"));
        send=reinterpret_cast<decltype(send)>(library.resolve("interception_send"));
        if(!create||!destroy||!hardware||!send){error="无法加载 interception.dll，请重新构建或补齐运行库。";return false;}
        context=create();if(!context){error="无法连接 Interception 驱动。请确认驱动已安装并重启电脑，且未被其他程序占用。";return false;}return true;
    }
    QString id(int slot){wchar_t buffer[2048]{};if(!hardware(context,slot,buffer,sizeof(buffer)-sizeof(wchar_t)))return {};return QString::fromWCharArray(buffer);}
};
class WindowsKeyOutput final:public KeyOutput {
    Driver driver_;int slot_{};QString id_;HWND window_{};DWORD process_{};ULONGLONG lastCheck_{};bool connected_{};
public:
    void close() override {driver_.close();}
    bool prepare(const OutputTarget& target,QString& error) override {
        if(!driver_.open(error))return false;
        const auto pieces=target.keyboard.split(':');slot_=pieces.value(1).toInt();id_=pieces.mid(2).join(':');
        if(pieces.value(0)!="interception"||slot_<1||slot_>10||id_.isEmpty()||driver_.id(slot_)!=id_){error="所选键盘已变化，请刷新键盘后重新选择。";return false;}
        window_=reinterpret_cast<HWND>(quintptr(target.window));process_=target.process;connected_=true;lastCheck_=0;
        if(targetStatus()==TargetStatus::Missing){error="目标窗口已关闭，请刷新窗口后重新选择。";return false;}return true;
    }
    bool activate() override {return activateOutputWindow(quintptr(window_),process_);}
    TargetStatus targetStatus() override {
        DWORD pid=0;GetWindowThreadProcessId(window_,&pid);
        if(!IsWindow(window_)||!process_||pid!=process_)return TargetStatus::Missing;
        const auto now=GetTickCount64();if(now-lastCheck_>=250){connected_=driver_.id(slot_)==id_;lastCheck_=now;}
        if(!connected_)return TargetStatus::KeyboardMissing;
        return IsWindowVisible(window_)&&!IsIconic(window_)&&GetForegroundWindow()==window_?TargetStatus::Ready:TargetStatus::NotForeground;
    }
    bool modifiersHeld() override {for(int key:{VK_CONTROL,VK_MENU,VK_SHIFT,VK_LWIN,VK_RWIN})if(GetAsyncKeyState(key)&0x8000)return true;return false;}
    bool send(KeyBatch batch) override {
        if(!driver_.context)return false;
        constexpr unsigned short scans[]{0x30,0x21,0x22,0x23,0x24,0x25,0x14,0x15,0x16};
        std::vector<InterceptionKeyStroke> strokes;
        for(int i=0;i<9;++i)if(batch.up&(1<<i))strokes.push_back({scans[i],INTERCEPTION_KEY_UP,0});
        for(int i=0;i<9;++i)if(batch.down&(1<<i))strokes.push_back({scans[i],INTERCEPTION_KEY_DOWN,0});
        return strokes.empty()||driver_.send(driver_.context,slot_,reinterpret_cast<const InterceptionStroke*>(strokes.data()),static_cast<unsigned>(strokes.size()))==static_cast<int>(strokes.size());
    }
};
}
bool activateOutputWindow(quint64 window,quint32 process) {
    const auto target=reinterpret_cast<HWND>(quintptr(window));DWORD pid=0;
    GetWindowThreadProcessId(target,&pid);
    if(!target||!IsWindow(target)||!process||pid!=process)return false;
    constexpr UINT flags=SMTO_ABORTIFHUNG|SMTO_BLOCK|SMTO_ERRORONEXIT;
    DWORD_PTR result=0;
    // Restore first, in order. ShowWindowAsync followed immediately by activation
    // races the target's restore processing. Do not unmaximize an existing window.
    if(IsIconic(target)&&!SendMessageTimeoutW(target,WM_SYSCOMMAND,SC_RESTORE,0,flags,500,&result))return false;
    if(!IsWindowVisible(target)||IsIconic(target))return false;
    SetForegroundWindow(target);
    // Cross-thread foreground activation is asynchronous. Wait for the target to
    // process it, with a bound for hung windows, then verify the actual foreground.
    if(!SendMessageTimeoutW(target,WM_NULL,0,0,flags,500,&result))return false;
    GetWindowThreadProcessId(target,&pid);
    return pid==process&&IsWindowVisible(target)&&!IsIconic(target)&&GetForegroundWindow()==target;
}
std::unique_ptr<KeyOutput> createKeyOutput(){return std::make_unique<WindowsKeyOutput>();}
DiscoveryResult discoverOutputKeyboards(){Driver driver;DiscoveryResult result;if(!driver.open(result.error))return result;
    for(int slot=1;slot<=10;++slot){auto id=driver.id(slot);if(id.isEmpty())continue;
        result.choices.push_back({QString("键盘 %1 · %2").arg(slot).arg(id),QString("interception:%1:%2").arg(slot).arg(id),QString("Interception 键盘通道 %1\n%2").arg(slot).arg(id)});}return result;}
}
#else
namespace rock {
bool activateOutputWindow(quint64,quint32){return false;}
std::unique_ptr<KeyOutput> createKeyOutput(){return {};}
DiscoveryResult discoverOutputKeyboards(){return {{},"自动演奏仅支持 Windows。"};}
}
#endif
