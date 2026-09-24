#pragma once

#include "AdsManager/PlcClient.h"

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace tcGUICore {

struct RuntimeVariable {
    SymbolConfig config;
    std::array<uint8_t, 8> data{};
    bool ok = false;
    bool netIdOk = false;
    bool dirty = false;
    bool mismatchWarned = false;
};

class AdsHub {
public:
    PlcClient& addPlc(PlcConfig config);
    PlcClient* find(const std::string& id);
    const PlcClient* find(const std::string& id) const;
    PlcClient* selected();
    const PlcClient* selected() const;

    void select(const std::string& id);
    const std::string& selectedId() const { return selectedId_; }
    void applyConnectionSettings(const std::string& plcId, std::string ip, uint16_t adsPort, std::string amsNetId = {});

    std::vector<PlcClient*> clients();
    std::vector<const PlcClient*> clients() const;

    void addSymbol(SymbolConfig symbol);
    const std::vector<SymbolConfig>& symbols() const { return symbols_; }

    RuntimeVariable* variable(const std::string& key);
    const RuntimeVariable* variable(const std::string& key) const;
    RuntimeVariable* variable(const std::string& plcId, const std::string& name);

    std::vector<RuntimeVariable>& variables() { return runtime_; }
    const std::vector<RuntimeVariable>& variables() const { return runtime_; }

    std::vector<SymbolSnapshot> pollSymbols();
    void tick();

    bool writeFromUi(RuntimeVariable& var, const void* data, std::size_t size);

private:
    void rebuildRuntime();
    void syncIo();

    std::vector<std::unique_ptr<PlcClient>> clients_;
    std::vector<SymbolConfig> symbols_;
    std::vector<RuntimeVariable> runtime_;
    std::string selectedId_;
    std::chrono::steady_clock::time_point lastStatePoll_{};
    std::chrono::steady_clock::time_point lastIoPoll_{};
};

} // namespace tcGUICore
