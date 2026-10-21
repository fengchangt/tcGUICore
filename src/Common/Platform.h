#pragma once

#include <string>
#include <vector>

namespace tcGUICore::platform {

std::string executableStem();
std::string executableDir();
std::string firstExistingFile(const std::vector<std::string>& candidates);
std::string chineseFontPath();

// Linux: detect USB TSTP MTouch; if missing, run ensure_touchscreen.sh to USB-reset.
// No-op on other platforms. Safe to call before GLFW init.
void ensureTouchscreen();

} // namespace tcGUICore::platform
