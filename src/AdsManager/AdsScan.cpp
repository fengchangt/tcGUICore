#include "AdsScan.h"

#include "Common/Log.h"
#include "Common/I18n.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace tcGUICore {
namespace {

constexpr uint32_t kUdpCookie = 0x71146603u;
constexpr uint32_t kServiceServerInfo = 1u;
constexpr uint16_t kAdsUdpPort = 48899;

void appendLe32(std::vector<uint8_t>& buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

std::vector<uint8_t> makeServerInfoRequest()
{
    // 与 AdsLib GetRemoteAddress / SendRecv 相同的 AMS UDP 发现包。
    std::vector<uint8_t> buf;
    buf.reserve(24);
    appendLe32(buf, kUdpCookie);
    appendLe32(buf, 0);
    appendLe32(buf, kServiceServerInfo);
    buf.insert(buf.end(), 8, 0); // AmsAddr { NetId 0, port 0 }
    appendLe32(buf, 0);          // tagCount
    return buf;
}

uint32_t readLe32(const unsigned char* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

std::string netIdFromServerInfo(const char* payload, int size)
{
    if (!payload || size < 20) {
        return {};
    }
    const auto* b = reinterpret_cast<const unsigned char*>(payload);
    if (readLe32(b) != kUdpCookie) {
        return {};
    }
    const uint32_t service = readLe32(b + 8);
    if ((service & 0x7fffffffu) != kServiceServerInfo) {
        return {};
    }
    if ((b[12] | b[13] | b[14] | b[15] | b[16] | b[17]) == 0) {
        return {};
    }
    char text[32];
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u.%u.%u", b[12], b[13], b[14], b[15], b[16], b[17]);
    return text;
}

void addEndpoint(std::vector<AdsEndpoint>& found, const std::string& ip, const std::string& amsNetId)
{
    if (ip.empty() || ip == "0.0.0.0") {
        return;
    }
    const auto it = std::find_if(found.begin(), found.end(), [&](const AdsEndpoint& item) { return item.ip == ip; });
    if (it == found.end()) {
        found.push_back(AdsEndpoint{ip, amsNetId});
        return;
    }
    if (it->amsNetId.empty() && !amsNetId.empty()) {
        it->amsNetId = amsNetId;
    }
}

#ifdef _WIN32

struct WinsockGuard {
    bool ok = false;
    WinsockGuard()
    {
        WSADATA data{};
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockGuard()
    {
        if (ok) {
            WSACleanup();
        }
    }
};

std::vector<sockaddr_in> broadcastTargets(SOCKET s)
{
    std::vector<sockaddr_in> out;
    auto add = [&](uint32_t addrBe) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(kAdsUdpPort);
        a.sin_addr.s_addr = addrBe;
        out.push_back(a);
    };
    add(htonl(INADDR_BROADCAST));
    add(inet_addr("127.0.0.1"));

    INTERFACE_INFO list[32]{};
    DWORD bytes = 0;
    if (WSAIoctl(s, SIO_GET_INTERFACE_LIST, nullptr, 0, &list, sizeof(list), &bytes, nullptr, nullptr) == 0) {
        const int n = static_cast<int>(bytes / sizeof(INTERFACE_INFO));
        for (int i = 0; i < n; ++i) {
            if (!(list[i].iiFlags & IFF_UP)) {
                continue;
            }
            if (list[i].iiFlags & IFF_BROADCAST) {
                add(list[i].iiBroadcastAddress.AddressIn.sin_addr.s_addr);
            }
        }
    }
    return out;
}

std::vector<AdsEndpoint> scanWindows(int timeoutMs)
{
    WinsockGuard wsa;
    if (!wsa.ok) {
        return {};
    }

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        return {};
    }

    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

    DWORD rcv = 120;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcv), sizeof(rcv));

    const auto pkt = makeServerInfoRequest();
    for (const sockaddr_in& dst : broadcastTargets(s)) {
        sendto(s, reinterpret_cast<const char*>(pkt.data()), static_cast<int>(pkt.size()), 0,
            reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
    }

    std::vector<AdsEndpoint> found;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        char payload[512];
        sockaddr_in from{};
        int fromLen = sizeof(from);
        const int n = recvfrom(s, payload, sizeof(payload), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) {
            continue;
        }
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        addEndpoint(found, ip, netIdFromServerInfo(payload, n));
    }

    closesocket(s);
    return found;
}

#else

std::vector<AdsEndpoint> scanPosix(int timeoutMs)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        return {};
    }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    timeval tv{0, 120000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    const auto pkt = makeServerInfoRequest();
    auto sendTo = [&](in_addr_t addr) {
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(kAdsUdpPort);
        dst.sin_addr.s_addr = addr;
        sendto(s, pkt.data(), static_cast<int>(pkt.size()), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    };
    sendTo(htonl(INADDR_BROADCAST));
    sendTo(htonl(INADDR_LOOPBACK));

    ifaddrs* ifa = nullptr;
    if (getifaddrs(&ifa) == 0) {
        for (ifaddrs* p = ifa; p; p = p->ifa_next) {
            if (!p->ifa_broadaddr || p->ifa_broadaddr->sa_family != AF_INET) {
                continue;
            }
            sendTo(reinterpret_cast<sockaddr_in*>(p->ifa_broadaddr)->sin_addr.s_addr);
        }
        freeifaddrs(ifa);
    }

    std::vector<AdsEndpoint> found;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        char payload[512];
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        const int n = recvfrom(s, payload, sizeof(payload), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) {
            continue;
        }
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        addEndpoint(found, ip, netIdFromServerInfo(payload, n));
    }
    close(s);
    return found;
}

#endif

} // namespace

std::vector<AdsEndpoint> scanAdsIpsOnce(int timeoutMs)
{
#ifdef _WIN32
    auto found = scanWindows(timeoutMs);
#else
    auto found = scanPosix(timeoutMs);
#endif
    logInfo(std::string(tr("ADS 扫描完成，发现 ", "ADS scan done, found ")) +
            std::to_string(found.size()) + tr(" 台", " device(s)"));
    for (const auto& item : found) {
        logInfo(item.ip + "  AMS " + (item.amsNetId.empty() ? amsNetIdFromIp(item.ip) : item.amsNetId));
    }
    return found;
}

} // namespace tcGUICore
