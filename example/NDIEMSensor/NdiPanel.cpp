#include "NdiPanel.h"

#include "NdiSession.h"

#include "AdsManager/AdsHub.h"
#include "AdsManager/AdsScan.h"
#include "AdsManager/PlcClient.h"
#include "Common/I18n.h"
#include "tcGUICore.h"

#include "imgui.h"

#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kPlcId = "plc1";

const char* kBaudLabels[] = {
    "9600", "14400", "19200", "38400", "57600", "115200", "921600", "1228739"
};
const char* kAdsPorts[] = {"851", "852", "853", "801", "10000"};

struct PlcScan {
    ~PlcScan()
    {
        if (worker.joinable()) {
            worker.join();
        }
    }

    std::mutex mu;
    std::thread worker;
    std::vector<tcGUICore::AdsEndpoint> found;
    std::atomic<bool> scanning{false};

    void start()
    {
        if (scanning.load()) {
            return;
        }
        if (worker.joinable()) {
            worker.join();
        }
        scanning = true;
        worker = std::thread([this] {
            std::vector<tcGUICore::AdsEndpoint> endpoints = tcGUICore::scanAdsIpsOnce(700);
            {
                std::lock_guard<std::mutex> lock(mu);
                found = std::move(endpoints);
            }
            scanning = false;
        });
    }
};

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

void drawPlcRow()
{
    static char ip[64] = "172.13.158.17";
    static int portIndex = 0;
    static std::string scannedIp;
    static std::string scannedNetId;
    static PlcScan scan;

    tcGUICore::PlcClient* plc = plcClient();
    const bool linked = plc && plc->state() == tcGUICore::ConnectionState::Connected;
    const bool connecting = plc && plc->state() == tcGUICore::ConnectionState::Connecting;

    ImGui::SeparatorText("PLC");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("IP:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("##plc_ip", ip, sizeof(ip));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    const char* scanPreview = scan.scanning.load()
        ? tcGUICore::tr("扫描中", "Scanning")
        : tcGUICore::tr("扫描地址", "Scanned");
    if (ImGui::BeginCombo("##plc_scan", scanPreview)) {
        if (ImGui::IsWindowAppearing()) {
            scan.start();
        }
        std::vector<tcGUICore::AdsEndpoint> found;
        {
            std::lock_guard<std::mutex> lock(scan.mu);
            found = scan.found;
        }
        if (found.empty()) {
            ImGui::TextUnformatted(scan.scanning.load()
                ? tcGUICore::tr("正在扫描", "Scanning")
                : tcGUICore::tr("无结果", "No PLC"));
        }
        for (const tcGUICore::AdsEndpoint& endpoint : found) {
            if (ImGui::Selectable(endpoint.ip.c_str(), endpoint.ip == ip)) {
                std::snprintf(ip, sizeof(ip), "%s", endpoint.ip.c_str());
                scannedIp = endpoint.ip;
                scannedNetId = endpoint.amsNetId;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(tcGUICore::tr("端口:", "Port:"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::Combo("##plc_port", &portIndex, kAdsPorts, IM_ARRAYSIZE(kAdsPorts));

    ImGui::SameLine();
    drawLed(linked);
    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    const char* label = linked ? tcGUICore::tr("断开", "Disconnect") : tcGUICore::tr("连接", "Connect");
    if (ImGui::Button(label, ImVec2(108.0f, 0.0f)) && plc && !connecting) {
        if (linked) {
            plc->disconnect();
        } else if (ip[0] != '\0') {
            const int slot = portIndex >= 0 && portIndex < IM_ARRAYSIZE(kAdsPorts) ? portIndex : 0;
            const auto adsPort = static_cast<uint16_t>(std::atoi(kAdsPorts[slot]));
            const std::string address = ip;
            const std::string netId = (address == scannedIp && !scannedNetId.empty())
                ? scannedNetId
                : tcGUICore::amsNetIdFromIp(address);
            tcGUICore::Application::instance()->ads().applyConnectionSettings(kPlcId, address, adsPort, netId);
            plc->connectAsync();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tcGUICore::currentLanguage() == tcGUICore::Language::Zh ? "中文" : "EN", ImVec2(88.0f, 0.0f))) {
        tcGUICore::toggleLanguage();
    }
    ImGui::PopStyleVar();

    if (plc) {
        const std::string error = plc->lastError();
        const std::string device = plc->deviceName();
        if (!error.empty() && !linked) {
            ImGui::SameLine();
            ImGui::TextUnformatted(error.c_str());
        } else if (linked && !device.empty()) {
            ImGui::SameLine();
            ImGui::TextUnformatted(device.c_str());
        }
    }
}

void drawPoseTable(const NdiSession& session)
{
    ImGui::SeparatorText(tcGUICore::tr("四通道位姿", "Four channels"));
    ImGui::TextUnformatted(tcGUICore::tr(
        "四元数 q0 qx qy qz，位置 X Y Z，单位 mm。ADS 透传等数据协议对齐后再写入 PLC。",
        "Quaternion q0 qx qy qz and position X Y Z in mm. ADS write waits for the PLC protocol."));

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

    const float footer = ImGui::GetFrameHeightWithSpacing();
    ImVec2 body = ImGui::GetContentRegionAvail();
    body.y -= footer;
    if (body.y < 1.0f) {
        body.y = 1.0f;
    }
    ImGui::BeginChild("##ndi_body", body, ImGuiChildFlags_None);
    drawNdiRow(session);
    drawPlcRow();
    drawPoseTable(session);
    ImGui::EndChild();
}
