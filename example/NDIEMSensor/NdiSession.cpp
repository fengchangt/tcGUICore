#include "NdiSession.h"

#include "CombinedApi.h"
#include "PortHandleInfo.h"
#include "ToolData.h"
#include "Transform.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <filesystem>
#endif

namespace {

CommBaudRateEnum::value baudFromIndex(int index)
{
    static const CommBaudRateEnum::value kRates[] = {
        CommBaudRateEnum::Baud9600,
        CommBaudRateEnum::Baud14400,
        CommBaudRateEnum::Baud19200,
        CommBaudRateEnum::Baud38400,
        CommBaudRateEnum::Baud57600,
        CommBaudRateEnum::Baud115200,
        CommBaudRateEnum::Baud921600,
        CommBaudRateEnum::Baud1228739,
    };
    if (index < 0 || index >= static_cast<int>(sizeof(kRates) / sizeof(kRates[0]))) {
        return CommBaudRateEnum::Baud921600;
    }
    return kRates[index];
}

std::string ndiErrorText(int code)
{
    return CombinedApi::errorToString(code) + " (" + std::to_string(code) + ")";
}

} // namespace

struct NdiSession::Worker {
    mutable std::mutex mu;
    std::condition_variable cv;
    std::thread thread;
    bool stop = false;
    bool wantConnect = false;
    bool wantDisconnect = false;
    std::string com;
    int baudIndex = 6;
    NdiLinkState state = NdiLinkState::Idle;
    std::string message = "未连接";
    std::array<NdiChannelPose, NdiSession::kChannels> poses{};

    Worker()
    {
        thread = std::thread([this] { loop(); });
    }

    ~Worker()
    {
        {
            std::lock_guard<std::mutex> lock(mu);
            stop = true;
            wantDisconnect = true;
        }
        cv.notify_all();
        if (thread.joinable()) {
            thread.join();
        }
    }

    void setState(NdiLinkState next, std::string text)
    {
        std::lock_guard<std::mutex> lock(mu);
        state = next;
        message = std::move(text);
    }

    void clearPoses()
    {
        std::lock_guard<std::mutex> lock(mu);
        poses = {};
    }

    bool cancelled()
    {
        std::lock_guard<std::mutex> lock(mu);
        if (stop || wantDisconnect) {
            wantDisconnect = false;
            return true;
        }
        return false;
    }

    std::unique_ptr<CombinedApi> openDevice(const std::string& port, int baud)
    {
        setState(NdiLinkState::Busy, "正在连接 " + port);
        auto api = std::make_unique<CombinedApi>();
        int err = api->connect(port);
        if (cancelled()) {
            setState(NdiLinkState::Idle, "已取消");
            return nullptr;
        }
        if (err != 0) {
            setState(NdiLinkState::Error, ndiErrorText(err));
            return nullptr;
        }

        const CommBaudRateEnum::value rate = baudFromIndex(baud);
        if (rate != CommBaudRateEnum::Baud921600) {
            err = api->setCommParams(rate);
            if (cancelled()) {
                setState(NdiLinkState::Idle, "已取消");
                return nullptr;
            }
            if (err != 0) {
                setState(NdiLinkState::Error, ndiErrorText(err));
                return nullptr;
            }
        }

        err = api->initialize();
        if (cancelled()) {
            setState(NdiLinkState::Idle, "已取消");
            return nullptr;
        }
        if (err != 0) {
            setState(NdiLinkState::Error, ndiErrorText(err));
            return nullptr;
        }

        const auto notInit = api->portHandleSearchRequest(PortHandleSearchRequestOption::NotInit);
        for (const PortHandleInfo& handle : notInit) {
            api->portHandleInitialize(handle.getPortHandle());
            if (cancelled()) {
                setState(NdiLinkState::Idle, "已取消");
                return nullptr;
            }
        }

        int enabled = 0;
        const auto notEnabled = api->portHandleSearchRequest(PortHandleSearchRequestOption::NotEnabled);
        for (const PortHandleInfo& handle : notEnabled) {
            if (api->portHandleEnable(handle.getPortHandle()) == 0) {
                ++enabled;
            }
            if (cancelled()) {
                setState(NdiLinkState::Idle, "已取消");
                return nullptr;
            }
        }

        err = api->startTracking();
        if (cancelled()) {
            setState(NdiLinkState::Idle, "已取消");
            return nullptr;
        }
        if (err != 0) {
            setState(NdiLinkState::Error, ndiErrorText(err));
            return nullptr;
        }

        setState(NdiLinkState::Tracking, "跟踪中，已启用端口 " + std::to_string(enabled));
        return api;
    }

    bool poll(CombinedApi& api)
    {
        std::vector<ToolData> tools = api.getTrackingDataBX();
        std::sort(tools.begin(), tools.end(), [](const ToolData& a, const ToolData& b) {
            return a.transform.toolHandle < b.transform.toolHandle;
        });

        std::array<NdiChannelPose, NdiSession::kChannels> next{};
        const int count = std::min(NdiSession::kChannels, static_cast<int>(tools.size()));
        for (int i = 0; i < count; ++i) {
            const Transform& tf = tools[static_cast<std::size_t>(i)].transform;
            NdiChannelPose& pose = next[static_cast<std::size_t>(i)];
            pose.present = true;
            pose.valid = !tf.isMissing();
            pose.q0 = tf.q0;
            pose.qx = tf.qx;
            pose.qy = tf.qy;
            pose.qz = tf.qz;
            pose.tx = tf.tx;
            pose.ty = tf.ty;
            pose.tz = tf.tz;
            pose.error = tf.error;
            pose.status = TransformStatus::toString(tf.getErrorCode());
        }

        std::lock_guard<std::mutex> lock(mu);
        if (state == NdiLinkState::Tracking) {
            poses = std::move(next);
        }
        return true;
    }

    void loop()
    {
        std::unique_ptr<CombinedApi> api;
        while (true) {
            std::string port;
            int baud = 6;
            bool doConnect = false;
            bool doDisconnect = false;
            bool doStop = false;
            {
                std::unique_lock<std::mutex> lock(mu);
                if (!api) {
                    cv.wait(lock, [&] { return stop || wantConnect || wantDisconnect; });
                } else {
                    cv.wait_for(lock, std::chrono::milliseconds(20), [&] {
                        return stop || wantConnect || wantDisconnect;
                    });
                }
                doStop = stop;
                if (wantDisconnect || stop) {
                    wantDisconnect = false;
                    doDisconnect = true;
                }
                if (wantConnect && !doStop) {
                    wantConnect = false;
                    doConnect = true;
                    port = com;
                    baud = baudIndex;
                    doDisconnect = true;
                }
            }

            if (doDisconnect && api) {
                api->stopTracking();
                api.reset();
                clearPoses();
                if (!doConnect) {
                    setState(NdiLinkState::Idle, "已断开");
                }
            }
            if (doStop) {
                break;
            }
            if (doConnect) {
                api = openDevice(port, baud);
            }
            if (api) {
                poll(*api);
            }
        }
    }
};

NdiSession::NdiSession()
    : worker_(new Worker())
{
}

NdiSession::~NdiSession()
{
    delete worker_;
    worker_ = nullptr;
}

std::vector<std::string> NdiSession::listComPorts()
{
    std::vector<std::string> ports;
#ifdef _WIN32
    std::vector<char> buffer(65536, '\0');
    const DWORD written = QueryDosDeviceA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (written == 0) {
        return ports;
    }
    for (const char* name = buffer.data(); *name != '\0'; name += std::strlen(name) + 1) {
        if (std::strncmp(name, "COM", 3) != 0 || name[3] == '\0') {
            continue;
        }
        bool digits = true;
        for (const char* cursor = name + 3; *cursor != '\0'; ++cursor) {
            if (*cursor < '0' || *cursor > '9') {
                digits = false;
                break;
            }
        }
        if (digits) {
            ports.emplace_back(name);
        }
    }
#else
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator("/dev", ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("ttyUSB", 0) == 0 || name.rfind("ttyACM", 0) == 0) {
            ports.push_back(entry.path().string());
        }
    }
#endif
    std::sort(ports.begin(), ports.end(), [](const std::string& a, const std::string& b) {
        const char* as = a.c_str();
        const char* bs = b.c_str();
        while (*as != '\0' && (*as < '0' || *as > '9')) {
            ++as;
        }
        while (*bs != '\0' && (*bs < '0' || *bs > '9')) {
            ++bs;
        }
        return std::atoi(as) < std::atoi(bs);
    });
    return ports;
}

void NdiSession::requestConnect(std::string comPort, int baudIndex)
{
    std::lock_guard<std::mutex> lock(worker_->mu);
    worker_->com = std::move(comPort);
    worker_->baudIndex = baudIndex;
    worker_->wantDisconnect = false;
    worker_->wantConnect = true;
    worker_->cv.notify_all();
}

void NdiSession::requestDisconnect()
{
    std::lock_guard<std::mutex> lock(worker_->mu);
    worker_->wantConnect = false;
    worker_->wantDisconnect = true;
    worker_->cv.notify_all();
}

NdiLinkState NdiSession::state() const
{
    std::lock_guard<std::mutex> lock(worker_->mu);
    return worker_->state;
}

std::string NdiSession::message() const
{
    std::lock_guard<std::mutex> lock(worker_->mu);
    return worker_->message;
}

std::array<NdiChannelPose, NdiSession::kChannels> NdiSession::poses() const
{
    std::lock_guard<std::mutex> lock(worker_->mu);
    return worker_->poses;
}
