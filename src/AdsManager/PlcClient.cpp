#include "PlcClient.h"

#include "Common/I18n.h"
#include "Common/Log.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

#include "AdsDef.h"
#include "AdsDevice.h"
#include "AdsException.h"
#include "AdsLib.h"

#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace tcGUICore {

namespace {

#ifdef _WIN32
std::string localIpv4List()
{
    std::string out;
    SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe == INVALID_SOCKET) {
        return out;
    }
    INTERFACE_INFO list[32]{};
    DWORD bytes = 0;
    if (WSAIoctl(probe, SIO_GET_INTERFACE_LIST, nullptr, 0, list, sizeof(list), &bytes, nullptr, nullptr) == 0) {
        const int count = static_cast<int>(bytes / sizeof(INTERFACE_INFO));
        for (int i = 0; i < count; ++i) {
            if (!(list[i].iiFlags & IFF_UP) || (list[i].iiFlags & IFF_LOOPBACK)) {
                continue;
            }
            const auto& addr = list[i].iiAddress.AddressIn;
            if (addr.sin_family != AF_INET) {
                continue;
            }
            char text[INET_ADDRSTRLEN] = {};
            if (!inet_ntop(AF_INET, &addr.sin_addr, text, sizeof(text)) || std::strcmp(text, "0.0.0.0") == 0) {
                continue;
            }
            if (!out.empty()) {
                out += ", ";
            }
            out += text;
        }
    }
    closesocket(probe);
    return out;
}

// AdsLib 的 TCP 连接没有超时，目标不可达时会卡住约 20 秒，并且只打印 “connect failed”。
bool adsTcpReachable(const std::string& ip, int timeoutMs, std::string& error)
{
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(48898);
    if (inet_pton(AF_INET, ip.c_str(), &dest.sin_addr) != 1) {
        return true;
    }

    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        error = tr("Winsock 初始化失败", "Winsock init failed");
        return false;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        error = tr("无法创建套接字", "Unable to create socket");
        WSACleanup();
        return false;
    }

    u_long nonBlock = 1;
    ioctlsocket(s, FIONBIO, &nonBlock);
    const int started = connect(s, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    int soError = 0;
    if (started != 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
        soError = WSAGetLastError();
    } else if (started != 0) {
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(s, &writeSet);
        timeval wait{};
        wait.tv_sec = timeoutMs / 1000;
        wait.tv_usec = (timeoutMs % 1000) * 1000;
        const int selected = select(0, nullptr, &writeSet, nullptr, &wait);
        if (selected <= 0) {
            soError = WSAETIMEDOUT;
        } else {
            int length = sizeof(soError);
            getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &length);
        }
    }
    const std::string local = soError == 0 ? std::string() : localIpv4List();
    closesocket(s);
    WSACleanup();
    if (soError == 0) {
        return true;
    }

    const std::string where = ip + ":48898";
    if (soError == WSAETIMEDOUT || soError == WSAEHOSTUNREACH || soError == WSAENETUNREACH) {
        error = tr("连不上 ", "Cannot reach ") + where +
            tr("。本机网卡 ", ". Local address ") + (local.empty() ? std::string("-") : local) +
            tr(" 到该 PLC 没有可用路由，请把电脑接到 PLC 所在网段后再连接。",
               " has no route to this PLC. Join the PLC subnet, then connect.");
    } else if (soError == WSAECONNREFUSED) {
        error = tr("PLC 拒绝了 ", "PLC refused ") + where +
            tr("。请确认 TwinCAT 路由正在运行，且防火墙放行 48898。",
               ". Check that the TwinCAT router is running and port 48898 is allowed.");
    } else {
        error = tr("连接 ", "Connect ") + where + tr(" 失败，错误码 ", " failed, error ") + std::to_string(soError);
    }
    return false;
}

#pragma pack(push, 1)
struct TcAmsNetId {
    unsigned char b[6];
};
struct TcAmsAddr {
    TcAmsNetId netId;
    unsigned short port;
};
struct TcAdsVersion {
    unsigned char version;
    unsigned char revision;
    unsigned short build;
};
#pragma pack(pop)

using TcOpen = long(__stdcall*)();
using TcClose = long(__stdcall*)(long);
using TcTimeout = long(__stdcall*)(long, long);
using TcInfo = long(__stdcall*)(long, TcAmsAddr*, char*, TcAdsVersion*);
using TcState = long(__stdcall*)(long, TcAmsAddr*, unsigned short*, unsigned short*);
using TcRead = long(__stdcall*)(long, TcAmsAddr*, unsigned long, unsigned long, unsigned long, void*, unsigned long*);
using TcWrite = long(__stdcall*)(long, TcAmsAddr*, unsigned long, unsigned long, unsigned long, void*);
using TcReadWrite = long(__stdcall*)(long, TcAmsAddr*, unsigned long, unsigned long, unsigned long, void*, unsigned long, void*, unsigned long*);
using TcControl = long(__stdcall*)(long, TcAmsAddr*, unsigned short, unsigned short, unsigned long, void*);

struct TwinCatApi {
    HMODULE dll = nullptr;
    TcOpen open = nullptr;
    TcClose close = nullptr;
    TcTimeout setTimeout = nullptr;
    TcInfo info = nullptr;
    TcState state = nullptr;
    TcRead read = nullptr;
    TcWrite write = nullptr;
    TcReadWrite readWrite = nullptr;
    TcControl control = nullptr;

    bool load()
    {
        if (dll) {
            return open && info && read && write && readWrite && state && close;
        }
        const wchar_t* paths[] = {
            L"C:\\TwinCAT\\AdsApi\\TcAdsDll\\x64\\TcAdsDll.dll",
            L"TcAdsDll.dll",
        };
        for (const wchar_t* path : paths) {
            dll = LoadLibraryW(path);
            if (dll) {
                break;
            }
        }
        if (!dll) {
            return false;
        }
        open = reinterpret_cast<TcOpen>(GetProcAddress(dll, "AdsPortOpenEx"));
        close = reinterpret_cast<TcClose>(GetProcAddress(dll, "AdsPortCloseEx"));
        setTimeout = reinterpret_cast<TcTimeout>(GetProcAddress(dll, "AdsSyncSetTimeoutEx"));
        info = reinterpret_cast<TcInfo>(GetProcAddress(dll, "AdsSyncReadDeviceInfoReqEx"));
        state = reinterpret_cast<TcState>(GetProcAddress(dll, "AdsSyncReadStateReqEx"));
        read = reinterpret_cast<TcRead>(GetProcAddress(dll, "AdsSyncReadReqEx2"));
        write = reinterpret_cast<TcWrite>(GetProcAddress(dll, "AdsSyncWriteReqEx"));
        readWrite = reinterpret_cast<TcReadWrite>(GetProcAddress(dll, "AdsSyncReadWriteReqEx2"));
        control = reinterpret_cast<TcControl>(GetProcAddress(dll, "AdsSyncWriteControlReqEx"));
        return open && close && info && state && read && write && readWrite;
    }
};

TwinCatApi& twinCatApi()
{
    static TwinCatApi api;
    return api;
}

std::string routerErrorText(long code, const std::string& netId, uint16_t port)
{
    if (code == 6) {
        return netId + tr(" 端口未打开 ", " port not open ") + std::to_string(port);
    }
    if (code == 7) {
        return tr("本机 TwinCAT 路由里没有 ", "No TwinCAT route for ") + netId;
    }
    return tr("TwinCAT 路由返回 ", "TwinCAT router returned ") + std::to_string(code);
}

struct RouterLink {
    long port = 0;
    TcAmsAddr addr{};
    std::unordered_map<std::string, unsigned long> handles;
    std::unordered_set<std::string> missing;

    ~RouterLink()
    {
        if (port > 0) {
            if (TcClose close = twinCatApi().close) {
                close(port);
            }
        }
    }

    bool symbolHandle(const std::string& path, unsigned long& handle, std::string& error)
    {
        const auto it = handles.find(path);
        if (it != handles.end()) {
            handle = it->second;
            return true;
        }
        if (missing.find(path) != missing.end()) {
            error = std::string(tr("读符号失败 ", "Read failed ")) + path;
            return false;
        }
        unsigned long bytes = 0;
        unsigned long value = 0;
        const long err = twinCatApi().readWrite(
            port, &addr, ADSIGRP_SYM_HNDBYNAME, 0, sizeof(value), &value,
            static_cast<unsigned long>(path.size() + 1), const_cast<char*>(path.c_str()), &bytes);
        if (err) {
            missing.insert(path);
            error = std::string(tr("读符号失败 ", "Read failed ")) + path;
            return false;
        }
        handles.emplace(path, value);
        handle = value;
        return true;
    }
};
#endif

} // namespace

struct PlcClient::DeviceHolder {
    std::unique_ptr<AdsDevice> device;
    std::unordered_map<std::string, AdsHandle> handles;
    std::unordered_set<std::string> missingSymbols;
#ifdef _WIN32
    std::unique_ptr<RouterLink> router;
    std::string routerName;
    unsigned short routerAdsState = 0;
#endif
};

PlcClient::PlcClient(PlcConfig config)
    : config_(std::move(config))
    , adsStateText_("—")
{
    if (config_.configAmsNetId.empty()) {
        config_.configAmsNetId = config_.amsNetId;
    }
}

PlcClient::~PlcClient()
{
    disconnect();
}

PlcConfig PlcClient::config() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void PlcClient::setConfig(PlcConfig config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == ConnectionState::Connected || state_ == ConnectionState::Connecting) {
        return;
    }
    if (config.amsNetId.empty()) {
        config.amsNetId = amsNetIdFromIp(config.ip);
    }
    config.configAmsNetId = config.amsNetId;
    config_ = std::move(config);
}

std::string PlcClient::statusText() const
{
    return connectionStateText(state_.load());
}

std::string PlcClient::lastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

std::string PlcClient::deviceName() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return deviceName_;
}

std::string PlcClient::adsStateText() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return adsStateText_;
}

std::string PlcClient::sessionAmsNetId() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sessionAmsNetId_;
}

bool PlcClient::netIdMatches(const std::string& expectedAmsNetId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == ConnectionState::Connected && !expectedAmsNetId.empty() &&
        sessionAmsNetId_ == expectedAmsNetId;
}

void PlcClient::joinWorker()
{
    if (worker_ && worker_->joinable()) {
        worker_->join();
    }
    worker_.reset();
}

void PlcClient::connectAsync()
{
    if (state_ == ConnectionState::Connecting || state_ == ConnectionState::Connected) {
        return;
    }
    joinWorker();
    cancel_ = false;
    state_ = ConnectionState::Connecting;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_.clear();
        deviceName_.clear();
        sessionAmsNetId_.clear();
        adsStateText_ = tr("连接中", "Connecting");
    }
    logInfo(std::string(tr("正在连接 ", "Connecting ")) + displayNameText(config_.displayName) +
            "  " + config_.ip + " / " + config_.amsNetId);
    worker_ = std::make_unique<std::thread>([this] { connectWorker(); });
}

void PlcClient::connectWorker()
{
    PlcConfig cfg;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cfg = config_;
    }

    const AmsNetId netId = make_AmsNetId(cfg.amsNetId);
    const bool netIdZero = (netId.b[0] | netId.b[1] | netId.b[2] | netId.b[3] | netId.b[4] | netId.b[5]) == 0;
    if (cfg.ip.empty() || netIdZero) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = tr("IP 或 AMS NetId 无效", "Invalid IP or AMS NetId");
        adsStateText_ = "—";
        device_.reset();
        state_ = ConnectionState::Error;
        logError(lastError_);
        return;
    }

    try {
#ifdef _WIN32
        if (twinCatApi().load()) {
            auto link = std::make_unique<RouterLink>();
            link->port = twinCatApi().open();
            if (link->port > 0) {
                if (twinCatApi().setTimeout) {
                    twinCatApi().setTimeout(link->port, static_cast<long>(cfg.timeoutMs > 0 ? cfg.timeoutMs : 2000));
                }
                const AmsNetId parsed = make_AmsNetId(cfg.amsNetId);
                std::memcpy(link->addr.netId.b, parsed.b, 6);
                link->addr.port = cfg.adsPort;
                char name[17] = {};
                TcAdsVersion version{};
                const long infoErr = twinCatApi().info(link->port, &link->addr, name, &version);
                if (infoErr == 0) {
                    unsigned short ads = 0;
                    unsigned short dev = 0;
                    twinCatApi().state(link->port, &link->addr, &ads, &dev);
                    name[16] = '\0';
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto holder = std::make_unique<DeviceHolder>();
                    holder->routerName = name;
                    holder->routerAdsState = ads;
                    holder->router = std::move(link);
                    device_ = std::move(holder);
                    sessionAmsNetId_ = cfg.amsNetId;
                    deviceName_ = name;
                    adsStateText_ = (ads == 5) ? "RUN" : ("ADS " + std::to_string(ads));
                    lastError_.clear();
                    state_ = ConnectionState::Connected;
                    logInfo(std::string(tr("已通过本机 TwinCAT 路由连接 ", "Connected via local TwinCAT router ")) +
                            cfg.amsNetId + "  " + deviceName_);
                    return;
                }
                if (twinCatApi().close) {
                    twinCatApi().close(link->port);
                }
                link->port = 0;
                if (infoErr != 7 && infoErr != 6) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    lastError_ = routerErrorText(infoErr, cfg.amsNetId, cfg.adsPort);
                    adsStateText_ = "—";
                    device_.reset();
                    state_ = ConnectionState::Error;
                    logError(std::string(tr("连接失败: ", "Connect failed: ")) + lastError_);
                    return;
                }
            }
        }
        std::string reachError;
        const int probeMs = cfg.timeoutMs > 0 && cfg.timeoutMs < 3000 ? static_cast<int>(cfg.timeoutMs) : 2500;
        if (!adsTcpReachable(cfg.ip, probeMs, reachError)) {
            std::lock_guard<std::mutex> lock(mutex_);
            lastError_ = reachError;
            adsStateText_ = "—";
            device_.reset();
            state_ = ConnectionState::Error;
            logError(std::string(tr("连接失败: ", "Connect failed: ")) + lastError_);
            return;
        }
#endif
        auto holder = std::make_unique<DeviceHolder>();
        holder->device = std::make_unique<AdsDevice>(cfg.ip, netId, cfg.adsPort);
        if (cancel_) {
            return;
        }
        holder->device->SetTimeout(cfg.timeoutMs);
        const DeviceInfo info = holder->device->GetDeviceInfo();
        const AdsDeviceState st = holder->device->GetState();
        holder->device->SetTimeout(80);

        std::lock_guard<std::mutex> lock(mutex_);
        device_ = std::move(holder);
        sessionAmsNetId_ = cfg.amsNetId;
        deviceName_ = info.name;
        adsStateText_ = (st.ads == ADSSTATE_RUN) ? "RUN" : ("ADS " + std::to_string(static_cast<int>(st.ads)));
        lastError_.clear();
        state_ = ConnectionState::Connected;
        logInfo(std::string(tr("已连接 ", "Connected ")) + displayNameText(cfg.displayName) +
                "  " + tr("设备", "device") + "=" + deviceName_);
        if (!cfg.configAmsNetId.empty() && cfg.amsNetId != cfg.configAmsNetId) {
            logWarn(tr("连接 NetId 与 JSON 配置不一致，已配置的变量将不会读写",
                       "Session NetId differs from JSON; configured variables will not be I/O"));
        }
    } catch (const AdsException& ex) {
        std::lock_guard<std::mutex> lock(mutex_);
        device_.reset();
        lastError_ = ex.what();
        adsStateText_ = "—";
        state_ = ConnectionState::Error;
        logError(std::string(tr("连接失败: ", "Connect failed: ")) + lastError_);
    } catch (const std::exception& ex) {
        std::lock_guard<std::mutex> lock(mutex_);
        device_.reset();
        lastError_ = ex.what();
        adsStateText_ = "—";
        state_ = ConnectionState::Error;
        logError(std::string(tr("连接失败: ", "Connect failed: ")) + lastError_);
    }
}

void PlcClient::disconnect()
{
    cancel_ = true;
    joinWorker();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        device_.reset();
        deviceName_.clear();
        sessionAmsNetId_.clear();
        adsStateText_ = "—";
        if (state_ != ConnectionState::Error) {
            lastError_.clear();
        }
    }
    const ConnectionState prev = state_.exchange(ConnectionState::Disconnected);
    if (prev == ConnectionState::Connected) {
        logInfo(std::string(tr("已断开 ", "Disconnected ")) + displayNameText(config_.displayName));
    }
}

bool PlcClient::readBytes(uint32_t indexGroup, uint32_t indexOffset, void* data, std::size_t size, std::size_t* bytesRead)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (bytesRead) {
        *bytesRead = 0;
    }
    if (!device_ || !data || size == 0 || state_ != ConnectionState::Connected) {
        return false;
    }
#ifdef _WIN32
    if (device_->router) {
        unsigned long n = 0;
        long err = twinCatApi().read(
            device_->router->port, &device_->router->addr, indexGroup, indexOffset,
            static_cast<unsigned long>(size), data, &n);
        if (err == 1795 && twinCatApi().control) {
            twinCatApi().control(device_->router->port, &device_->router->addr, 5, 0, 0, nullptr);
            n = 0;
            err = twinCatApi().read(
                device_->router->port, &device_->router->addr, indexGroup, indexOffset,
                static_cast<unsigned long>(size), data, &n);
        }
        if (bytesRead) {
            *bytesRead = n;
        }
        if (err) {
            lastError_ = tr("读取失败 ", "Read failed ") + std::to_string(err);
            return false;
        }
        return true;
    }
#endif
    if (!device_->device) {
        return false;
    }
    uint32_t n = 0;
    const long err = device_->device->ReadReqEx2(indexGroup, indexOffset, static_cast<uint32_t>(size), data, &n);
    if (bytesRead) {
        *bytesRead = n;
    }
    if (err) {
        lastError_ = tr("读取失败 ", "Read failed ") + std::to_string(err);
        return false;
    }
    return true;
}

bool PlcClient::writeBytes(uint32_t indexGroup, uint32_t indexOffset, const void* data, std::size_t size)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!device_ || !data || size == 0 || state_ != ConnectionState::Connected) {
        return false;
    }
#ifdef _WIN32
    if (device_->router) {
        long err = twinCatApi().write(
            device_->router->port, &device_->router->addr, indexGroup, indexOffset,
            static_cast<unsigned long>(size), const_cast<void*>(data));
        if (err == 1795 && twinCatApi().control) {
            twinCatApi().control(device_->router->port, &device_->router->addr, 5, 0, 0, nullptr);
            err = twinCatApi().write(
                device_->router->port, &device_->router->addr, indexGroup, indexOffset,
                static_cast<unsigned long>(size), const_cast<void*>(data));
        }
        if (err) {
            lastError_ = tr("写入失败 ", "Write failed ") + std::to_string(err);
            return false;
        }
        return true;
    }
#endif
    if (!device_->device) {
        return false;
    }
    const long err = device_->device->WriteReqEx(indexGroup, indexOffset, static_cast<uint32_t>(size), data);
    if (err) {
        lastError_ = tr("写入失败 ", "Write failed ") + std::to_string(err);
        return false;
    }
    return true;
}

bool PlcClient::readSymbol(const std::string& path, void* data, std::size_t size)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!device_ || state_ != ConnectionState::Connected) {
        return false;
    }
#ifdef _WIN32
    if (device_->router) {
        unsigned long handle = 0;
        if (!device_->router->symbolHandle(path, handle, lastError_)) {
            return false;
        }
        unsigned long bytesRead = 0;
        const long err = twinCatApi().read(
            device_->router->port, &device_->router->addr, ADSIGRP_SYM_VALBYHND, handle,
            static_cast<unsigned long>(size), data, &bytesRead);
        if (err || bytesRead != size) {
            lastError_ = std::string(tr("读符号失败 ", "Read failed ")) + path;
            return false;
        }
        return true;
    }
#endif
    if (!device_->device) {
        return false;
    }
    if (device_->missingSymbols.find(path) != device_->missingSymbols.end()) {
        lastError_ = std::string(tr("读符号失败 ", "Read failed ")) + path;
        return false;
    }
    try {
        auto it = device_->handles.find(path);
        if (it == device_->handles.end()) {
            AdsHandle h = device_->device->GetHandle(path);
            it = device_->handles.emplace(path, std::move(h)).first;
        }
        uint32_t bytesRead = 0;
        const long err = device_->device->ReadReqEx2(ADSIGRP_SYM_VALBYHND, *it->second, size, data, &bytesRead);
        if (err || bytesRead != size) {
            lastError_ = std::string(tr("读符号失败 ", "Read failed ")) + path;
            return false;
        }
        return true;
    } catch (const AdsException& ex) {
        device_->missingSymbols.insert(path);
        lastError_ = ex.what();
        return false;
    }
}

bool PlcClient::writeSymbol(const std::string& path, const void* data, std::size_t size)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!device_ || state_ != ConnectionState::Connected) {
        return false;
    }
#ifdef _WIN32
    if (device_->router) {
        unsigned long handle = 0;
        if (!device_->router->symbolHandle(path, handle, lastError_)) {
            return false;
        }
        const long err = twinCatApi().write(
            device_->router->port, &device_->router->addr, ADSIGRP_SYM_VALBYHND, handle,
            static_cast<unsigned long>(size), const_cast<void*>(data));
        if (err) {
            lastError_ = std::string(tr("写符号失败 ", "Write failed ")) + path;
            return false;
        }
        return true;
    }
#endif
    if (!device_->device) {
        return false;
    }
    if (device_->missingSymbols.find(path) != device_->missingSymbols.end()) {
        lastError_ = std::string(tr("写符号失败 ", "Write failed ")) + path;
        return false;
    }
    try {
        auto it = device_->handles.find(path);
        if (it == device_->handles.end()) {
            AdsHandle h = device_->device->GetHandle(path);
            it = device_->handles.emplace(path, std::move(h)).first;
        }
        const long err = device_->device->WriteReqEx(ADSIGRP_SYM_VALBYHND, *it->second, size, data);
        if (err) {
            lastError_ = std::string(tr("写符号失败 ", "Write failed ")) + path;
            return false;
        }
        return true;
    } catch (const AdsException& ex) {
        device_->missingSymbols.insert(path);
        lastError_ = ex.what();
        return false;
    }
}

void PlcClient::refreshState()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!device_ || state_ != ConnectionState::Connected) {
        return;
    }
#ifdef _WIN32
    if (device_->router) {
        unsigned short ads = 0;
        unsigned short dev = 0;
        const long err = twinCatApi().state(device_->router->port, &device_->router->addr, &ads, &dev);
        if (err) {
            lastError_ = routerErrorText(err, sessionAmsNetId_, device_->router->addr.port);
            adsStateText_ = tr("超时", "Timeout");
            device_.reset();
            sessionAmsNetId_.clear();
            state_ = ConnectionState::Error;
            logError(std::string(tr("状态刷新失败: ", "State refresh failed: ")) + lastError_);
            return;
        }
        device_->routerAdsState = ads;
        adsStateText_ = (ads == 5) ? "RUN" : ("ADS " + std::to_string(ads));
        return;
    }
#endif
    if (!device_->device) {
        return;
    }
    try {
        const AdsDeviceState st = device_->device->GetState();
        adsStateText_ = (st.ads == ADSSTATE_RUN) ? "RUN" : ("ADS " + std::to_string(static_cast<int>(st.ads)));
    } catch (const AdsException& ex) {
        lastError_ = ex.what();
        adsStateText_ = tr("超时", "Timeout");
        device_.reset();
        sessionAmsNetId_.clear();
        state_ = ConnectionState::Error;
        logError(std::string(tr("状态刷新失败: ", "State refresh failed: ")) + lastError_);
    }
}

} // namespace tcGUICore
