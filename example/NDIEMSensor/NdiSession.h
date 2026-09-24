#pragma once

#include <array>
#include <string>
#include <vector>

// NDI Combined API 会话。串口命令只在工作线程里调用。
// 四个通道的位姿由界面按 S_EMSensorPose 写入 EMSensor.stAdsInput.sEMSensorPose。

enum class NdiLinkState {
    Idle = 0,
    Busy,
    Tracking,
    Error
};

struct NdiChannelPose {
    bool present = false;
    bool valid = false;
    double q0 = 0;
    double qx = 0;
    double qy = 0;
    double qz = 0;
    double tx = 0;
    double ty = 0;
    double tz = 0;
    double error = 0;
    std::string status;
};

class NdiSession {
public:
    static constexpr int kChannels = 4;

    NdiSession();
    ~NdiSession();

    NdiSession(const NdiSession&) = delete;
    NdiSession& operator=(const NdiSession&) = delete;

    static std::vector<std::string> listComPorts();

    // NDI 端口号 → PLC 下标。通道 2、3 写入 sEMSensorPose[2]、[3]，空通道不前移。
    static int channelNumber(unsigned handle);

    // baudIndex 与界面下拉一致：0=9600 ... 6=921600 ... 7=1228739。
    void requestConnect(std::string comPort, int baudIndex);
    void requestDisconnect();

    NdiLinkState state() const;
    std::string message() const;
    std::array<NdiChannelPose, kChannels> poses() const;
    double sampleHz() const;

private:
    struct Worker;
    Worker* worker_ = nullptr;
};
