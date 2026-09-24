#pragma once

#include "Common/Types.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace tcGUICore {

class PlcClient {
public:
    explicit PlcClient(PlcConfig config);
    ~PlcClient();

    PlcClient(const PlcClient&) = delete;
    PlcClient& operator=(const PlcClient&) = delete;

    PlcConfig config() const;
    void setConfig(PlcConfig config);

    ConnectionState state() const { return state_.load(); }
    std::string statusText() const;
    std::string lastError() const;
    std::string deviceName() const;
    std::string adsStateText() const;
    std::string sessionAmsNetId() const;

    // JSON 中的 NetId 必须与本次连接会话一致，否则禁止符号读写。
    bool netIdMatches(const std::string& expectedAmsNetId) const;

    void connectAsync();
    void disconnect();

    bool readSymbol(const std::string& path, void* data, std::size_t size);
    bool writeSymbol(const std::string& path, const void* data, std::size_t size);
    bool readBytes(uint32_t indexGroup, uint32_t indexOffset, void* data, std::size_t size, std::size_t* bytesRead);
    bool writeBytes(uint32_t indexGroup, uint32_t indexOffset, const void* data, std::size_t size);
    void refreshState();

private:
    void joinWorker();
    void connectWorker();

    PlcConfig config_;
    mutable std::mutex mutex_;
    std::atomic<ConnectionState> state_{ConnectionState::Disconnected};
    std::string lastError_;
    std::string deviceName_;
    std::string adsStateText_;
    std::string sessionAmsNetId_;
    std::atomic<bool> cancel_{false};
    std::unique_ptr<std::thread> worker_;
    struct DeviceHolder;
    std::unique_ptr<DeviceHolder> device_;
};

} // namespace tcGUICore
