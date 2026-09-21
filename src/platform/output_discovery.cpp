#include "output_discovery.h"
#include <algorithm>
#include <set>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <dwmapi.h>

namespace rock {
namespace {
QString failure(const QString& operation,DWORD code) {
    return operation+QString("失败（系统错误 %1），请重试。").arg(code);
}
struct DeviceList {
    HDEVINFO handle;
    ~DeviceList(){if(handle!=INVALID_HANDLE_VALUE)SetupDiDestroyDeviceInfoList(handle);}
};
QString property(HDEVINFO devices,SP_DEVINFO_DATA& device,DWORD key) {
    DWORD size=0,type=0;
    SetupDiGetDeviceRegistryPropertyW(devices,&device,key,&type,nullptr,0,&size);
    if(!size||size>65536)return {};
    std::vector<wchar_t> buffer(size/sizeof(wchar_t)+1,0);
    if(!SetupDiGetDeviceRegistryPropertyW(devices,&device,key,&type,reinterpret_cast<PBYTE>(buffer.data()),size,nullptr)||type!=REG_SZ)return {};
    return QString::fromWCharArray(buffer.data()).trimmed();
}
BOOL CALLBACK collectWindow(HWND window,LPARAM param) {
    if(!IsWindowVisible(window)||window==GetShellWindow()||window==GetDesktopWindow())return TRUE;
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(!pid||pid==GetCurrentProcessId())return TRUE;
    if(GetWindowLongPtrW(window,GWL_EXSTYLE)&(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE))return TRUE;
    DWORD cloaked=0;
    if(SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)))&&cloaked)return TRUE;
    int length=GetWindowTextLengthW(window);if(length<=0||length>32767)return TRUE;
    std::vector<wchar_t> title(length+1,0);
    int copied=GetWindowTextW(window,title.data(),static_cast<int>(title.size()));if(copied<=0)return TRUE;
    QString name=QString::fromWCharArray(title.data(),copied).trimmed();if(name.isEmpty())return TRUE;
    // A window can disappear while enumeration is running. Retain a current HWND/PID pair only.
    DWORD currentPid=0;GetWindowThreadProcessId(window,&currentPid);if(currentPid!=pid)return TRUE;
    const quint64 handle=reinterpret_cast<quintptr>(window);
    static_cast<DiscoveryResult*>(reinterpret_cast<void*>(param))->choices.push_back({
        name+QString(" · PID %1").arg(pid),QVariant::fromValue(handle),
        name+QString("\nPID %1 · HWND 0x%2").arg(pid).arg(handle,0,16),pid});
    return TRUE;
}
}
DiscoveryResult discoverKeyboards() {
    // GUID_DEVCLASS_KEYBOARD; enumerate currently present keyboard-class PnP devices.
    constexpr GUID keyboardClass{0x4d36e96b,0xe325,0x11ce,{0xbf,0xc1,0x08,0x00,0x2b,0xe1,0x03,0x18}};
    DeviceList devices{SetupDiGetClassDevsW(&keyboardClass,nullptr,nullptr,DIGCF_PRESENT)};
    if(devices.handle==INVALID_HANDLE_VALUE)return {{},failure("查找键盘",GetLastError())};
    DiscoveryResult result;std::set<QString> ids;
    for(DWORD index=0;;++index) {
        SP_DEVINFO_DATA device{};device.cbSize=sizeof(device);
        if(!SetupDiEnumDeviceInfo(devices.handle,index,&device)) {
            const DWORD error=GetLastError();
            if(error!=ERROR_NO_MORE_ITEMS)return {{},failure("读取键盘列表",error)};
            break;
        }
        ULONG status=0,problem=0;
        if(CM_Get_DevNode_Status(&status,&problem,device.DevInst,0)==CR_SUCCESS&&(!(status&DN_STARTED)||problem))continue;
        DWORD size=0;SetupDiGetDeviceInstanceIdW(devices.handle,&device,nullptr,0,&size);
        if(!size||size>32768)continue;
        std::vector<wchar_t> buffer(size+1,0);
        if(!SetupDiGetDeviceInstanceIdW(devices.handle,&device,buffer.data(),static_cast<DWORD>(buffer.size()),nullptr))continue;
        QString id=QString::fromWCharArray(buffer.data());if(id.isEmpty()||!ids.insert(id.toUpper()).second)continue;
        QString name=property(devices.handle,device,SPDRP_FRIENDLYNAME);
        if(name.isEmpty())name=property(devices.handle,device,SPDRP_DEVICEDESC);
        if(name.isEmpty())name="键盘设备";
        result.choices.push_back({name,id,name+"\n"+id});
    }
    std::sort(result.choices.begin(),result.choices.end(),[](const auto& a,const auto& b){return a.id.toString()<b.id.toString();});
    for(size_t i=0;i<result.choices.size();++i)result.choices[i].label+=QString(" · %1").arg(i+1);
    return result;
}
DiscoveryResult discoverWindows() {
    DiscoveryResult result;SetLastError(ERROR_SUCCESS);
    if(!EnumWindows(collectWindow,reinterpret_cast<LPARAM>(&result)))return {{},failure("查找窗口",GetLastError())};
    std::sort(result.choices.begin(),result.choices.end(),[](const auto& a,const auto& b){
        const int compared=QString::compare(a.label,b.label,Qt::CaseInsensitive);
        return compared?compared<0:a.id.toULongLong()<b.id.toULongLong();
    });
    return result;
}
}
#else
namespace rock {
DiscoveryResult discoverKeyboards(){return {{},"键盘查找仅支持 Windows。"};}
DiscoveryResult discoverWindows(){return {{},"窗口查找仅支持 Windows。"};}
}
#endif
