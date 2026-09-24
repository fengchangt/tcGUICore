#pragma once

#include "Common/Types.h"

#include <string>
#include <vector>

namespace tcGUICore {

struct LoadedConfig {
    WindowConfig window;
    std::vector<PlcConfig> plcs;
    std::vector<SymbolConfig> symbols;
};

// 加载与 PLC 共用的 JSON 配置。direction 必须在连接前给出。
bool loadPlcConfigFile(const std::string& path, LoadedConfig& out, std::string& error);

} // namespace tcGUICore
