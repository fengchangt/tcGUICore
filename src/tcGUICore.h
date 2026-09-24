#pragma once

// 产品软件只需包含本头文件。
// 启动时三件事：1) loadConfig(JSON)  2) 可选 onFrame 绑 UI  3) run()

#include "Common/Types.h"
#include "AdsManager/AdsHub.h"
#include "Viewer/UiBind.h"

#include <functional>
#include <string>

namespace tcGUICore {

class Application {
public:
    explicit Application(std::string title = {});
    ~Application();

    static Application* instance() { return instance_; }

    Application& setTitle(std::string title);
    Application& setWindowSize(int width, int height);
    Application& setLanguage(Language lang);

    // JSON 与 PLC 侧共用同一套符号名/类型/direction。默认 plc_symbols.json。
    bool loadConfig(const std::string& path = "plc_symbols.json");

    Application& addPlc(PlcConfig config);
    Application& addSymbol(SymbolConfig symbol);
    Application& onFrame(FrameCallback callback);

    AdsHub& ads() { return hub_; }
    const AdsHub& ads() const { return hub_; }
    WindowConfig window() const { return window_; }

    int run();

private:
    static Application* instance_;
    std::string title_;
    WindowConfig window_;
    AdsHub hub_;
    FrameCallback onFrame_;
};

} // namespace tcGUICore
