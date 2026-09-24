#pragma once

#include "AdsManager/AdsHub.h"
#include "Common/Types.h"

#include <string>

namespace tcGUICore {

class ShellWindow {
public:
    ShellWindow(std::string title, AdsHub& hub, FrameCallback onFrame, WindowConfig window);
    int run();

private:
    std::string title_;
    AdsHub& hub_;
    FrameCallback onFrame_;
    WindowConfig window_;
};

} // namespace tcGUICore
