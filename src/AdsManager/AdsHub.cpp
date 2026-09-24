#include "AdsHub.h"

#include "Common/I18n.h"
#include "Common/Log.h"

#include <cstdint>
#include <cstring>
#include <sstream>

namespace tcGUICore {

namespace {

std::string formatValue(ValueType type, const uint8_t* raw)
{
    std::ostringstream oss;
    switch (type) {
    case ValueType::Bool:
        oss << (raw[0] ? "TRUE" : "FALSE");
        break;
    case ValueType::Int8:
        oss << static_cast<int>(static_cast<int8_t>(raw[0]));
        break;
    case ValueType::UInt8:
        oss << static_cast<unsigned>(raw[0]);
        break;
    case ValueType::Int16: {
        int16_t v = 0;
        std::memcpy(&v, raw, sizeof(v));
        oss << v;
        break;
    }
    case ValueType::UInt16: {
        uint16_t v = 0;
        std::memcpy(&v, raw, sizeof(v));
        oss << v;
        break;
    }
    case ValueType::Int32: {
        int32_t v = 0;
        std::memcpy(&v, raw, sizeof(v));
        oss << v;
        break;
    }
    case ValueType::UInt32: {
        uint32_t v = 0;
        std::memcpy(&v, raw, sizeof(v));
        oss << v;
        break;
    }
    case ValueType::Int64: {
        int64_t v = 0;
        std::memcpy(&v, raw, sizeof(v));
        oss << v;
        break;
    }
    case ValueType::Float: {
        float v = 0;
        std::memcpy(&v, raw, sizeof(v));
        oss.setf(std::ios::fixed);
        oss.precision(4);
        oss << v;
        break;
    }
    case ValueType::Double: {
        double v = 0;
        std::memcpy(&v, raw, sizeof(v));
        oss.setf(std::ios::fixed);
        oss.precision(6);
        oss << v;
        break;
    }
    }
    return oss.str();
}

} // namespace

PlcClient& AdsHub::addPlc(PlcConfig config)
{
    if (config.id.empty()) {
        config.id = "plc" + std::to_string(clients_.size() + 1);
    }
    if (config.configAmsNetId.empty()) {
        config.configAmsNetId = config.amsNetId;
    }
    auto client = std::make_unique<PlcClient>(std::move(config));
    PlcClient& ref = *client;
    if (selectedId_.empty()) {
        selectedId_ = ref.config().id;
    }
    clients_.push_back(std::move(client));
    return ref;
}

PlcClient* AdsHub::find(const std::string& id)
{
    for (auto& c : clients_) {
        if (c->config().id == id) {
            return c.get();
        }
    }
    return nullptr;
}

const PlcClient* AdsHub::find(const std::string& id) const
{
    for (const auto& c : clients_) {
        if (c->config().id == id) {
            return c.get();
        }
    }
    return nullptr;
}

PlcClient* AdsHub::selected()
{
    return find(selectedId_);
}

const PlcClient* AdsHub::selected() const
{
    return find(selectedId_);
}

void AdsHub::select(const std::string& id)
{
    if (find(id)) {
        selectedId_ = id;
    }
}

void AdsHub::applyConnectionSettings(const std::string& plcId, std::string ip, uint16_t adsPort, std::string amsNetId)
{
    PlcClient* plc = find(plcId);
    if (!plc) {
        return;
    }
    if (plc->state() == ConnectionState::Connected || plc->state() == ConnectionState::Connecting) {
        return;
    }
    PlcConfig cfg = plc->config();
    cfg.ip = std::move(ip);
    cfg.adsPort = adsPort;
    cfg.amsNetId = amsNetId.empty() ? amsNetIdFromIp(cfg.ip) : std::move(amsNetId);
    cfg.configAmsNetId = cfg.amsNetId;
    plc->setConfig(cfg);
    for (auto& s : symbols_) {
        if (s.plcId == plcId) {
            s.amsNetId = cfg.amsNetId;
        }
    }
    for (auto& v : runtime_) {
        if (v.config.plcId == plcId) {
            v.config.amsNetId = cfg.amsNetId;
        }
    }
}

std::vector<PlcClient*> AdsHub::clients()
{
    std::vector<PlcClient*> out;
    out.reserve(clients_.size());
    for (auto& c : clients_) {
        out.push_back(c.get());
    }
    return out;
}

std::vector<const PlcClient*> AdsHub::clients() const
{
    std::vector<const PlcClient*> out;
    out.reserve(clients_.size());
    for (const auto& c : clients_) {
        out.push_back(c.get());
    }
    return out;
}

void AdsHub::addSymbol(SymbolConfig symbol)
{
    if (symbol.plcId.empty() && !selectedId_.empty()) {
        symbol.plcId = selectedId_;
    }
    if (symbol.amsNetId.empty()) {
        if (PlcClient* plc = find(symbol.plcId)) {
            symbol.amsNetId = plc->config().configAmsNetId;
        }
    }
    if (symbol.name.empty()) {
        symbol.name = symbol.path;
    }
    symbols_.push_back(std::move(symbol));
    rebuildRuntime();
}

void AdsHub::rebuildRuntime()
{
    runtime_.clear();
    runtime_.reserve(symbols_.size());
    for (const auto& cfg : symbols_) {
        RuntimeVariable rv;
        rv.config = cfg;
        runtime_.push_back(std::move(rv));
    }
}

RuntimeVariable* AdsHub::variable(const std::string& key)
{
    for (auto& v : runtime_) {
        if (variableKey(v.config.plcId, v.config.name) == key) {
            return &v;
        }
    }
    return nullptr;
}

const RuntimeVariable* AdsHub::variable(const std::string& key) const
{
    for (const auto& v : runtime_) {
        if (variableKey(v.config.plcId, v.config.name) == key) {
            return &v;
        }
    }
    return nullptr;
}

RuntimeVariable* AdsHub::variable(const std::string& plcId, const std::string& name)
{
    return variable(variableKey(plcId, name));
}

bool AdsHub::writeFromUi(RuntimeVariable& var, const void* data, std::size_t size)
{
    if (var.config.direction != IoDirection::Output) {
        return false;
    }
    const std::size_t n = valueTypeSize(var.config.type);
    if (size != n || n > var.data.size()) {
        return false;
    }
    std::memcpy(var.data.data(), data, n);
    var.dirty = true;
    PlcClient* plc = find(var.config.plcId);
    if (!plc || !plc->netIdMatches(var.config.amsNetId)) {
        var.ok = false;
        var.netIdOk = false;
        return false;
    }
    var.netIdOk = true;
    var.ok = plc->writeSymbol(var.config.path, var.data.data(), n);
    if (var.ok) {
        var.dirty = false;
    }
    return var.ok;
}

void AdsHub::syncIo()
{
    for (auto& var : runtime_) {
        PlcClient* plc = find(var.config.plcId);
        if (!plc || !plc->netIdMatches(var.config.amsNetId)) {
            if (!var.mismatchWarned && plc && plc->state() == ConnectionState::Connected) {
                logWarn(std::string(tr("跳过变量 ", "Skip variable ")) +
                        variableKey(var.config.plcId, var.config.name) +
                        tr("：会话 NetId 与 JSON 配置不一致",
                           ": session NetId != JSON amsNetId"));
                var.mismatchWarned = true;
            }
            var.ok = false;
            var.netIdOk = false;
            continue;
        }
        var.mismatchWarned = false;
        var.netIdOk = true;
        const std::size_t n = valueTypeSize(var.config.type);
        if (n == 0 || n > var.data.size()) {
            var.ok = false;
            continue;
        }
        if (var.config.direction == IoDirection::Input) {
            var.ok = plc->readSymbol(var.config.path, var.data.data(), n);
        } else if (var.dirty) {
            var.ok = plc->writeSymbol(var.config.path, var.data.data(), n);
            if (var.ok) {
                var.dirty = false;
            }
        } else {
            var.ok = plc->readSymbol(var.config.path, var.data.data(), n);
        }
    }
}

std::vector<SymbolSnapshot> AdsHub::pollSymbols()
{
    std::vector<SymbolSnapshot> out;
    out.reserve(runtime_.size());
    for (const auto& var : runtime_) {
        SymbolSnapshot snap;
        snap.config = var.config;
        snap.netIdOk = var.netIdOk;
        snap.ok = var.ok;
        if (!var.netIdOk) {
            snap.text = tr("NetId 不匹配", "NetId mismatch");
        } else if (!var.ok) {
            snap.text = "—";
        } else {
            snap.text = formatValue(var.config.type, var.data.data());
        }
        out.push_back(std::move(snap));
    }
    return out;
}

void AdsHub::tick()
{
    bool anyConnected = false;
    for (const auto& c : clients_) {
        if (c->state() == ConnectionState::Connected) {
            anyConnected = true;
            break;
        }
    }
    if (!anyConnected) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (lastIoPoll_.time_since_epoch().count() == 0 ||
        now - lastIoPoll_ >= std::chrono::milliseconds(50)) {
        lastIoPoll_ = now;
        syncIo();
    }
    if (lastStatePoll_.time_since_epoch().count() != 0 &&
        now - lastStatePoll_ < std::chrono::milliseconds(800)) {
        return;
    }
    lastStatePoll_ = now;
    for (auto& c : clients_) {
        c->refreshState();
    }
}

} // namespace tcGUICore
