#include "ConsoleUi.h"

#include "tcGUICore.h"

// 医生控制台扶手触摸屏（树莓派 5 / Windows / Linux 同一工程）。
// 流程：开机动画 → Control Core 门控登录 → 器械舱 / 主从锁定。
// 可执行文件与映射：SurgeonConsole / SurgeonConsole.json（跨平台统一、无空格）。
int main()
{
    tcGUICore::Application app("Surgeon Console");
    if (!app.loadConfig("SurgeonConsole.json")) {
        return 1;
    }
    app.onFrame([] { drawRobotConsole(); });
    return app.run();
}
