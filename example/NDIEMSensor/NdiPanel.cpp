#include "NdiPanel.h"

#include "NdiSession.h"

#include "AdsManager/PlcClient.h"
#include "Common/I18n.h"
#include "Common/Log.h"
#include "tcGUICore.h"

#include "imgui.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace {

const char* kBaudLabels[] = {
    "9600", "14400", "19200", "38400", "57600", "115200", "921600", "1228739"
};

// pack_mode := 1：按 1 字节对齐，LREAL 之间无填充。
// S_EMSensorPose = 4*8 + 3*8 = 56。ARRAY[1..4] 共 224，[1] 在偏移 0，[2] 在 56。
#pragma pack(push, 1)
struct EmSensorPosePlc {
    double lQuaternion[4];
    double lPos[3];
};
#pragma pack(pop)
static_assert(sizeof(EmSensorPosePlc) == 56, "S_EMSensorPose pack_mode 1");
static_assert(sizeof(EmSensorPosePlc) * NdiSession::kChannels == 224, "sEMSensorPose[1..4]");

constexpr char kPoseSymbol[] = "EMSensor.stAdsInput.sEMSensorPose";
constexpr char kBeatSymbol[] = "EMSensor.stAdsInput.bHeatBeat";
double gPoseWriteHz = 0;

void drawLed(bool on)
{
    const float h = ImGui::GetFrameHeight();
    const float r = 11.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 c(p.x + r + 1.0f, p.y + h * 0.5f);
    const ImU32 col = on ? IM_COL32(46, 204, 96, 255) : ImGui::GetColorU32(ImGuiCol_Text);
    ImGui::GetWindowDrawList()->AddCircleFilled(c, r, col);
    ImGui::Dummy(ImVec2(r * 2.0f + 6.0f, h));
}

void drawNumber(bool show, double value, const char* format)
{
    if (!show) {
        ImGui::TextUnformatted("--");
        return;
    }
    ImGui::Text(format, value);
}

tcGUICore::PlcClient* plcClient()
{
    tcGUICore::Application* app = tcGUICore::Application::instance();
    if (!app) {
        return nullptr;
    }
    const std::vector<tcGUICore::PlcClient*> clients = app->ads().clients();
    if (clients.empty()) {
        return nullptr;
    }
    return clients.front();
}

void drawNdiRow(NdiSession& session)
{
    static std::string com;
    static int baudIndex = 6;
    static std::vector<std::string> ports;

    const NdiLinkState link = session.state();
    const bool busy = link == NdiLinkState::Busy;
    const bool tracking = link == NdiLinkState::Tracking;

    ImGui::SeparatorText(tcGUICore::tr("NDI 磁导航", "NDI"));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tcGUICore::tr("串口:", "COM:"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    const char* comPreview = com.empty() ? tcGUICore::tr("选择 COM", "Select COM") : com.c_str();
    if (ImGui::BeginCombo("##ndi_com", comPreview)) {
        if (ImGui::IsWindowAppearing()) {
            ports = NdiSession::listComPorts();
        }
        if (ports.empty()) {
            ImGui::TextUnformatted(tcGUICore::tr("未发现串口", "No COM port"));
        }
        for (const std::string& port : ports) {
            if (ImGui::Selectable(port.c_str(), port == com)) {
                com = port;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(tcGUICore::tr("刷新", "Refresh"))) {
        ports = NdiSession::listComPorts();
        if (com.empty() && !ports.empty()) {
            com = ports.front();
        }
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(tcGUICore::tr("波特率:", "Baud:"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("##ndi_baud", &baudIndex, kBaudLabels, IM_ARRAYSIZE(kBaudLabels));

    ImGui::SameLine();
    drawLed(tracking);
    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    const char* label = tracking || busy ? tcGUICore::tr("断开", "Disconnect") : tcGUICore::tr("连接", "Connect");
    if (ImGui::Button(label, ImVec2(108.0f, 0.0f)) && !busy) {
        if (tracking) {
            session.requestDisconnect();
        } else if (!com.empty()) {
            session.requestConnect(com, baudIndex);
        }
    }
    ImGui::PopStyleVar();
    ImGui::SameLine();
    ImGui::TextUnformatted(session.message().c_str());
}

void publishLoop(NdiSession* session, const std::atomic<bool>* stop)
{
    int writeCount = 0;
    auto writeWindow = std::chrono::steady_clock::time_point{};
    auto lastBeat = std::chrono::steady_clock::time_point{};
    uint8_t beat = 0;
    bool poseWarned = false;
    while (!stop->load(std::memory_order_relaxed)) {
        tcGUICore::PlcClient* plc = plcClient();
        if (!session || !plc || plc->state() != tcGUICore::ConnectionState::Connected) {
            gPoseWriteHz = 0;
            writeCount = 0;
            writeWindow = {};
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        const auto poses = session->poses();
        EmSensorPosePlc blob[NdiSession::kChannels]{};
        for (int i = 0; i < NdiSession::kChannels; ++i) {
            const NdiChannelPose& pose = poses[static_cast<std::size_t>(i)];
            if (!pose.valid) {
                continue;
            }
            blob[i].lQuaternion[0] = pose.q0;
            blob[i].lQuaternion[1] = pose.qx;
            blob[i].lQuaternion[2] = pose.qy;
            blob[i].lQuaternion[3] = pose.qz;
            blob[i].lPos[0] = pose.tx;
            blob[i].lPos[1] = pose.ty;
            blob[i].lPos[2] = pose.tz;
        }
        const auto now = std::chrono::steady_clock::now();
        if (plc->writeSymbol(kPoseSymbol, blob, sizeof(blob))) {
            poseWarned = false;
            if (writeWindow.time_since_epoch().count() == 0) {
                writeWindow = now;
            }
            ++writeCount;
            const auto elapsed = now - writeWindow;
            if (elapsed >= std::chrono::milliseconds(500)) {
                const double seconds = std::chrono::duration<double>(elapsed).count();
                gPoseWriteHz = seconds > 0.0 ? static_cast<double>(writeCount) / seconds : 0.0;
                writeCount = 0;
                writeWindow = now;
            }
        } else if (!poseWarned) {
            poseWarned = true;
            tcGUICore::logWarn(std::string(tcGUICore::tr("写入位姿失败 ", "Pose write failed ")) + kPoseSymbol);
        }
        if (lastBeat.time_since_epoch().count() == 0 || now - lastBeat >= std::chrono::milliseconds(200)) {
            lastBeat = now;
            beat ^= 1;
            plc->writeSymbol(kBeatSymbol, &beat, 1);
        }
    }
}

struct PosePublisher {
    std::atomic<bool> stop{false};
    std::thread thread;

    explicit PosePublisher(NdiSession* session)
    {
        thread = std::thread(publishLoop, session, &stop);
    }

    ~PosePublisher()
    {
        stop.store(true, std::memory_order_relaxed);
        if (thread.joinable()) {
            thread.join();
        }
    }
};

double poseWriteHz()
{
    return gPoseWriteHz;
}

void drawPoseTable(const NdiSession& session)
{
    ImGui::SeparatorText(tcGUICore::tr("四通道位姿", "Four channels"));
    ImGui::TextUnformatted(tcGUICore::tr(
        "四元数 q0 qx qy qz，位置 X Y Z，单位 mm。通道号即 sEMSensorPose 下标，空通道写 0。",
        "Quaternion q0 qx qy qz and position X Y Z in mm. Channel number is the sEMSensorPose index; empty channels are written as 0."));
    ImGui::Text("NDI %.1f Hz    ADS %.1f Hz", session.sampleHz(), poseWriteHz());

    const auto poses = session.poses();
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##ndi_pose", 10, flags)) {
        return;
    }
    ImGui::TableSetupColumn(tcGUICore::tr("通道", "Ch"));
    ImGui::TableSetupColumn(tcGUICore::tr("状态", "Status"));
    ImGui::TableSetupColumn("q0");
    ImGui::TableSetupColumn("qx");
    ImGui::TableSetupColumn("qy");
    ImGui::TableSetupColumn("qz");
    ImGui::TableSetupColumn("X");
    ImGui::TableSetupColumn("Y");
    ImGui::TableSetupColumn("Z");
    ImGui::TableSetupColumn(tcGUICore::tr("误差", "Error"));
    ImGui::TableHeadersRow();

    for (int i = 0; i < NdiSession::kChannels; ++i) {
        const NdiChannelPose& pose = poses[static_cast<std::size_t>(i)];
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%d", i + 1);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(pose.present ? pose.status.c_str() : "--");
        ImGui::TableSetColumnIndex(2);
        drawNumber(pose.valid, pose.q0, "%.4f");
        ImGui::TableSetColumnIndex(3);
        drawNumber(pose.valid, pose.qx, "%.4f");
        ImGui::TableSetColumnIndex(4);
        drawNumber(pose.valid, pose.qy, "%.4f");
        ImGui::TableSetColumnIndex(5);
        drawNumber(pose.valid, pose.qz, "%.4f");
        ImGui::TableSetColumnIndex(6);
        drawNumber(pose.valid, pose.tx, "%.2f");
        ImGui::TableSetColumnIndex(7);
        drawNumber(pose.valid, pose.ty, "%.2f");
        ImGui::TableSetColumnIndex(8);
        drawNumber(pose.valid, pose.tz, "%.2f");
        ImGui::TableSetColumnIndex(9);
        drawNumber(pose.valid, pose.error, "%.3f");
    }
    ImGui::EndTable();
}

} // namespace

void drawNdiEmSensorUi()
{
    static NdiSession session;

    tcGUICore::drawPlcConnectionBar();
    ImGui::Separator();

    const float footer = ImGui::GetFrameHeightWithSpacing();
    ImVec2 body = ImGui::GetContentRegionAvail();
    body.y -= footer;
    if (body.y < 1.0f) {
        body.y = 1.0f;
    }
    ImGui::BeginChild("##ndi_body", body, ImGuiChildFlags_None);
    drawNdiRow(session);
    drawPoseTable(session);
    static PosePublisher publisher(&session);
    ImGui::EndChild();
}
