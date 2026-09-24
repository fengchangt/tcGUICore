#include "tcGUICore.h"

#include "Common/ConfigFile.h"
#include "Common/I18n.h"
#include "Common/Log.h"
#include "Common/Platform.h"
#include "Viewer/ShellWindow.h"

#include <filesystem>
#include <vector>

namespace tcGUICore {

Application* Application::instance_ = nullptr;

Application::Application(std::string title)
    : title_(std::move(title))
{
    instance_ = this;
    if (title_.empty()) {
        title_ = platform::executableStem();
    }
}

Application::~Application()
{
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

Application& Application::setTitle(std::string title)
{
    title_ = std::move(title);
    return *this;
}

Application& Application::setWindowSize(int width, int height)
{
    window_.width = width;
    window_.height = height;
    return *this;
}

Application& Application::setLanguage(Language lang)
{
    window_.language = lang;
    tcGUICore::setLanguage(lang);
    return *this;
}

bool Application::loadConfig(const std::string& path)
{
    LoadedConfig loaded;
    std::string error;
    std::vector<std::string> candidates = {
        path,
        (std::filesystem::path(platform::executableDir()) / path).string(),
        (std::filesystem::current_path() / path).string(),
        "plc_symbols.json",
        "plc_symbols",
        (std::filesystem::path(platform::executableDir()) / "plc_symbols.json").string(),
        (std::filesystem::path(platform::executableDir()) / "plc_symbols").string(),
    };
    bool ok = false;
    std::string used;
    for (const auto& p : candidates) {
        loaded = {};
        if (loadPlcConfigFile(p, loaded, error)) {
            ok = true;
            used = p;
            break;
        }
    }
    if (!ok) {
        logError(std::string(tr("加载配置失败: ", "Failed to load config: ")) + error);
        return false;
    }

    window_ = loaded.window;
    tcGUICore::setLanguage(window_.language);
    for (auto& plc : loaded.plcs) {
        hub_.addPlc(std::move(plc));
    }
    for (auto& sym : loaded.symbols) {
        hub_.addSymbol(std::move(sym));
    }
    logInfo(std::string(tr("已加载配置 ", "Loaded config ")) + used + "  PLC=" +
            std::to_string(loaded.plcs.size()) + "  " + tr("变量", "vars") + "=" +
            std::to_string(loaded.symbols.size()));
    return true;
}

Application& Application::addPlc(PlcConfig config)
{
    hub_.addPlc(std::move(config));
    return *this;
}

Application& Application::addSymbol(SymbolConfig symbol)
{
    hub_.addSymbol(std::move(symbol));
    return *this;
}

Application& Application::onFrame(FrameCallback callback)
{
    onFrame_ = std::move(callback);
    return *this;
}

int Application::run()
{
    tcGUICore::setLanguage(window_.language);
    ShellWindow shell(title_, hub_, onFrame_, window_);
    return shell.run();
}

} // namespace tcGUICore
