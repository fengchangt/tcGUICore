#include "Platform.h"

#include "Log.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>
#endif

#include <filesystem>

namespace tcGUICore::platform {

std::string executableStem()
{
#if defined(_WIN32)
    char buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0) {
        return "tcGUICore";
    }
    return std::filesystem::path(buf).stem().string();
#else
    char buf[PATH_MAX] = {};
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return "tcGUICore";
    }
    buf[n] = '\0';
    return std::filesystem::path(buf).stem().string();
#endif
}

std::string executableDir()
{
#if defined(_WIN32)
    char buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0) {
        return ".";
    }
    return std::filesystem::path(buf).parent_path().string();
#else
    char buf[PATH_MAX] = {};
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return ".";
    }
    buf[n] = '\0';
    return std::filesystem::path(buf).parent_path().string();
#endif
}

std::string firstExistingFile(const std::vector<std::string>& candidates)
{
    for (const auto& p : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(p, ec)) {
            return p;
        }
    }
    return {};
}

std::string chineseFontPath()
{
    return firstExistingFile({
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttf",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/droid/DroidSansFallback.ttf",
        "/usr/share/fonts/truetype/arphic/uming.ttc",
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc",
    });
}

#if defined(__linux__)
bool touchscreenPresent()
{
    // Healthy path after re-enum: absolute mouse named TSTP MTouch (0416:c169).
    std::error_code ec;
    if (std::filesystem::exists("/dev/input/by-id/usb-TSTP_MTouch_CMTP_1.0-event-mouse", ec)) {
        return true;
    }
    std::ifstream devices("/proc/bus/input/devices");
    if (!devices) {
        return false;
    }
    std::string line;
    std::string name;
    while (std::getline(devices, line)) {
        if (line.rfind("N: Name=", 0) == 0) {
            name = line.substr(8);
            continue;
        }
        if (line.rfind("B: ABS=", 0) == 0 && name.find("MTouch") != std::string::npos
            && line != "B: ABS=0") {
            return true;
        }
        if (line.empty()) {
            name.clear();
        }
    }
    return false;
}

int runTouchEnsureScript(const std::string& script)
{
    // Prefer passwordless sudo so authorized/usbreset work; fall back to plain exec.
    const std::string cmd = "sudo -n " + script + " >/dev/null 2>&1 || " + script + " >/dev/null 2>&1";
    const int rc = std::system(cmd.c_str());
    if (rc < 0) {
        return -1;
    }
#if defined(WIFEXITED) && defined(WEXITSTATUS)
    if (WIFEXITED(rc)) {
        return WEXITSTATUS(rc);
    }
#endif
    return rc;
}
#endif

void ensureTouchscreen()
{
#if !defined(__linux__)
    return;
#else
    if (touchscreenPresent()) {
        logInfo("触摸屏已就绪 (TSTP MTouch)");
        return;
    }
    logWarn("未检测到触摸屏输入，尝试刷新 USB 驱动…");

    const std::string exeDir = executableDir();
    const std::string script = firstExistingFile({
        exeDir + "/ensure_touchscreen.sh",
        exeDir + "/../../scripts/ensure_touchscreen.sh",
        "/usr/local/sbin/surgeon-console-ensure-touch.sh",
        "/home/curvepi/github/tcGUICore/scripts/ensure_touchscreen.sh",
    });
    if (script.empty()) {
        logError("找不到 ensure_touchscreen.sh，无法自动刷新触摸驱动");
        return;
    }

    const int rc = runTouchEnsureScript(script);
    if (touchscreenPresent()) {
        logInfo(std::string("触摸屏已恢复 (") + script + ")");
        return;
    }
    logWarn(std::string("触摸屏刷新后仍不可用 (exit=") + std::to_string(rc)
        + ")，请检查 USB 线或重新插拔");
#endif
}

} // namespace tcGUICore::platform
