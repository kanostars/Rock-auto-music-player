#include "output_discovery.h"
#include "key_output.h"
#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>


#include <dwmapi.h>

namespace rock {
namespace {
QString failure(const QString& operation,DWORD code) {
    return operation+QString("失败（系统错误 %1），请重试。").arg(code);
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
    return discoverOutputKeyboards();
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
