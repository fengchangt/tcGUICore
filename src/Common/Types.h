#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tcGUICore {

inline constexpr uint16_t kDefaultPlcAdsPort = 851;
inline constexpr int kDefaultWindowWidth = 1920;
inline constexpr int kDefaultWindowHeight = 1080;

enum class Language { Zh, En };

enum class ConnectionState {
    Disconnected = 0,
    Connecting,
    Connected,
    Error
};

enum class ValueType {
    Bool,
    Int8,
    Int16,
    Int32,
    Int64,
    UInt8,
    UInt16,
    UInt32,
    Float,
    Double
};

// 相对 C++ 平台：Input = 从 PLC 读入；Output = 向 PLC 写出。
// 必须在 Ads 连接前确定。PLC 侧只需符号名与类型和 JSON 一致。
enum class IoDirection { Input, Output };

struct LocalizedText {
    std::string zh;
    std::string en;

    const std::string& get(Language lang) const
    {
        if (lang == Language::En && !en.empty()) {
            return en;
        }
        if (!zh.empty()) {
            return zh;
        }
        return en;
    }
};

struct WindowConfig {
    int width = kDefaultWindowWidth;
    int height = kDefaultWindowHeight;
    Language language = Language::Zh;
};

struct PlcConfig {
    std::string id;
    LocalizedText displayName;
    std::string ip;
    std::string amsNetId;       // 界面可改，用于本次 AdsDevice 连接
    std::string configAmsNetId; // JSON 里冻结的 NetId，变量 IO 必须与此一致
    uint16_t adsPort = kDefaultPlcAdsPort;
    uint32_t timeoutMs = 2000;
};

struct SymbolConfig {
    std::string plcId;
    std::string name;           // UI 绑定键，如 bEnable；完整键为 plcId.name
    std::string path;           // TwinCAT 符号，如 MAIN.bEnable
    std::string amsNetId;       // 来自 JSON，连接会话 NetId 对不上则禁止读写
    ValueType type = ValueType::Float;
    IoDirection direction = IoDirection::Input;
    LocalizedText label;
};

struct SymbolSnapshot {
    SymbolConfig config;
    std::string text;
    bool ok = false;
    bool netIdOk = false;
};

inline std::size_t valueTypeSize(ValueType type)
{
    switch (type) {
    case ValueType::Bool:
    case ValueType::Int8:
    case ValueType::UInt8:
        return 1;
    case ValueType::Int16:
    case ValueType::UInt16:
        return 2;
    case ValueType::Int32:
    case ValueType::UInt32:
    case ValueType::Float:
        return 4;
    case ValueType::Int64:
    case ValueType::Double:
        return 8;
    }
    return 0;
}

inline std::string variableKey(const std::string& plcId, const std::string& name)
{
    return plcId + "." + name;
}

// TwinCAT 默认 AMS NetId = IPv4 + ".1.1"
inline std::string amsNetIdFromIp(const std::string& ip)
{
    if (ip.empty()) {
        return {};
    }
    return ip + ".1.1";
}

using FrameCallback = std::function<void()>;

} // namespace tcGUICore
