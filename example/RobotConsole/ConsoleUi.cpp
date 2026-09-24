#include "ConsoleUi.h"

#include "AdsManager/PlcClient.h"
#include "Common/I18n.h"
#include "tcGUICore.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr const char* kMasterId = "master";
constexpr const char* kSlaveId = "slave";

enum class Page { Gallery, Plc, Status, Master, Slave };

struct RobotSetup {
    int mode = 0;
    float speed = 20.0f;
    bool enable = false;
    char tool[64] = "Tool-A";
};

struct LoginForm {
    char user[64] = "admin";
    char password[64] = "";
    bool failed = false;
};

Page gPage = Page::Gallery;
bool gAuthed = false;
LoginForm gLogin;
RobotSetup gMasterSetup{0, 20.0f, false, "Tool-A"};
RobotSetup gSlaveSetup{1, 35.0f, false, "Tool-B"};
float gLinkHistory[90]{};
int gLinkCount = 0;
double gLinkStamp = 0.0;

struct GalleryState {
    bool ledOn = true;
    bool toggleOn = true;
    float slider = 42.0f;
    int chip = 0;
    int combo = 1;
    bool primaryDown = false;
    bool secondaryDown = false;
    bool iconOn = true;
    bool checked = true;
    int radio = 0;
    int port = 851;
    char text[64] = "Tool-A";
    float wave[48]{};
};

GalleryState gGallery;

struct Endpoint {
    char ip[64];
    int port = 851;
};

Endpoint gMasterEp{"172.13.158.13", 851};
Endpoint gSlaveEp{"172.13.158.17", 851};

tcGUICore::PlcClient* clientOf(const char* id)
{
    tcGUICore::Application* app = tcGUICore::Application::instance();
    if (!app) {
        return nullptr;
    }
    return app->ads().find(id);
}

bool linked(const char* id)
{
    tcGUICore::PlcClient* plc = clientOf(id);
    return plc && plc->state() == tcGUICore::ConnectionState::Connected;
}

bool connecting(const char* id)
{
    tcGUICore::PlcClient* plc = clientOf(id);
    return plc && plc->state() == tcGUICore::ConnectionState::Connecting;
}

void connectPlc(const char* id, const Endpoint& ep)
{
    tcGUICore::Application* app = tcGUICore::Application::instance();
    tcGUICore::PlcClient* plc = clientOf(id);
    if (!app || !plc || plc->state() == tcGUICore::ConnectionState::Connecting) {
        return;
    }
    if (plc->state() == tcGUICore::ConnectionState::Connected) {
        plc->disconnect();
        return;
    }
    const auto port = static_cast<uint16_t>(ep.port > 0 && ep.port < 65536 ? ep.port : 851);
    app->ads().applyConnectionSettings(id, ep.ip, port, tcGUICore::amsNetIdFromIp(ep.ip));
    plc->connectAsync();
}

void sampleLink()
{
    const double now = ImGui::GetTime();
    if (now - gLinkStamp < 0.5) {
        return;
    }
    gLinkStamp = now;
    const float sample = (linked(kMasterId) ? 0.55f : 0.08f) + (linked(kSlaveId) ? 0.45f : 0.0f);
    if (gLinkCount < IM_ARRAYSIZE(gLinkHistory)) {
        gLinkHistory[gLinkCount++] = sample;
        return;
    }
    std::memmove(gLinkHistory, gLinkHistory + 1, sizeof(gLinkHistory) - sizeof(float));
    gLinkHistory[IM_ARRAYSIZE(gLinkHistory) - 1] = sample;
}

void drawEndpointCard(const char* title, const char* role, const char* id, Endpoint& ep)
{
    tcGUICore::PlcClient* plc = clientOf(id);
    const bool on = linked(id);
    const bool busy = connecting(id);
    tcGUICore::dash::beginCard(id, ImVec2(0.0f, 292.0f));
    tcGUICore::dash::caption(role);
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 36.0f);
    tcGUICore::dash::led(on);
    tcGUICore::dash::title(title, 24.0f);
    ImGui::Spacing();
    tcGUICore::dash::caption("IP");
    ImGui::SetNextItemWidth(-1.0f);
    tcGUICore::dash::textField((std::string("##ip") + id).c_str(), ep.ip, sizeof(ep.ip));
    tcGUICore::dash::caption(tcGUICore::tr("端口", "Port"));
    ImGui::SetNextItemWidth(160.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, tcGUICore::dash::palette().field);
    ImGui::PushStyleColor(ImGuiCol_Text, tcGUICore::dash::palette().text);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    ImGui::InputInt((std::string("##port") + id).c_str(), &ep.port);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if (ep.port < 1) {
        ep.port = 851;
    }
    ImGui::Spacing();
    tcGUICore::dash::progress(busy ? -1.0f : (on ? 1.0f : 0.0f));
    ImGui::Spacing();
    const char* label = on ? tcGUICore::tr("断开", "Disconnect") : tcGUICore::tr("连接", "Connect");
    if (tcGUICore::dash::button(label, ImVec2(148.0f, 38.0f), !on) && !busy) {
        connectPlc(id, ep);
    }
    ImGui::SameLine();
    if (plc) {
        tcGUICore::dash::hint(on ? plc->adsStateText().c_str() : plc->statusText().c_str());
    }
    tcGUICore::dash::endCard();
}

void drawPlcPage()
{
    tcGUICore::dash::hint(tcGUICore::tr("两台控制器可以同时在线。", "Both controllers can stay online together."));
    ImGui::Spacing();
    const float gap = 16.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool wide = avail >= 760.0f;
    const float width = wide ? (avail - gap) * 0.5f : avail;
    ImGui::BeginGroup();
    ImGui::PushItemWidth(width);
    drawEndpointCard(tcGUICore::tr("主机器人", "Master"), "MASTER", kMasterId, gMasterEp);
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    if (wide) {
        ImGui::SameLine(0.0f, gap);
    } else {
        ImGui::Spacing();
    }
    drawEndpointCard(tcGUICore::tr("从机器人", "Slave"), "SLAVE", kSlaveId, gSlaveEp);
}

void drawMetric(const char* id, const char* label, const char* value, const char* note, float fraction, bool on)
{
    tcGUICore::dash::beginCard(id, ImVec2(0.0f, 138.0f));
    tcGUICore::dash::caption(label);
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 36.0f);
    tcGUICore::dash::led(on);
    tcGUICore::dash::title(value, 26.0f);
    tcGUICore::dash::progress(fraction);
    ImGui::Spacing();
    tcGUICore::dash::hint(note);
    tcGUICore::dash::endCard();
}

void drawStatusPage()
{
    const bool masterOn = linked(kMasterId);
    const bool slaveOn = linked(kSlaveId);
    tcGUICore::PlcClient* master = clientOf(kMasterId);
    tcGUICore::PlcClient* slave = clientOf(kSlaveId);
    const float gap = 16.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool wide = avail >= 760.0f;
    const float width = wide ? (avail - gap) * 0.5f : avail;

    auto place = [&](bool first) {
        if (!first && wide) {
            ImGui::SameLine(0.0f, gap);
        } else if (!first) {
            ImGui::Spacing();
        }
    };

    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(width, 0.0f));
    drawMetric("##st_m", tcGUICore::tr("主机器人", "Master"),
        master ? master->statusText().c_str() : "—",
        masterOn ? master->adsStateText().c_str() : gMasterEp.ip,
        masterOn ? 1.0f : 0.0f, masterOn);
    ImGui::EndGroup();
    place(false);
    drawMetric("##st_s", tcGUICore::tr("从机器人", "Slave"),
        slave ? slave->statusText().c_str() : "—",
        slaveOn ? slave->adsStateText().c_str() : gSlaveEp.ip,
        slaveOn ? 1.0f : 0.0f, slaveOn);

    ImGui::Spacing();
    tcGUICore::dash::beginCard("##trend", ImVec2(0.0f, 250.0f));
    tcGUICore::dash::caption(tcGUICore::tr("链路活动", "Link activity"));
    tcGUICore::dash::title(masterOn && slaveOn
            ? tcGUICore::tr("双机在线", "Both online")
            : (masterOn || slaveOn ? tcGUICore::tr("部分在线", "Partial") : tcGUICore::tr("未连接", "Offline")),
        22.0f);
    ImGui::Spacing();
    if (tcGUICore::dash::chip(tcGUICore::tr("主站", "Master"), masterOn, tcGUICore::dash::Accent::Blue)) {
    }
    ImGui::SameLine();
    tcGUICore::dash::chip(tcGUICore::tr("从站", "Slave"), slaveOn, tcGUICore::dash::Accent::Cyan);
    ImGui::SameLine();
    tcGUICore::dash::chip(tcGUICore::tr("运动", "Motion"), gMasterSetup.enable || gSlaveSetup.enable, tcGUICore::dash::Accent::Green);
    ImGui::Spacing();
    tcGUICore::dash::sparkline(gLinkHistory, gLinkCount, ImVec2(ImGui::GetContentRegionAvail().x, 110.0f));
    tcGUICore::dash::endCard();
}

void drawModeChips(const char* id, RobotSetup& setup)
{
    const char* modes[] = {
        tcGUICore::tr("手动", "Manual"),
        tcGUICore::tr("自动", "Auto"),
        tcGUICore::tr("维护", "Service"),
    };
    const tcGUICore::dash::Accent accents[] = {
        tcGUICore::dash::Accent::Blue,
        tcGUICore::dash::Accent::Green,
        tcGUICore::dash::Accent::Amber,
    };
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(id);
        ImGui::PushID(i);
        if (tcGUICore::dash::chip(modes[i], setup.mode == i, accents[i])) {
            setup.mode = i;
        }
        ImGui::PopID();
        ImGui::PopID();
        if (i < 2) {
            ImGui::SameLine();
        }
    }
}

void drawRobotConfig(const char* id, const char* title, RobotSetup& setup, const Endpoint& ep, const char* plcId)
{
    const bool on = linked(plcId);
    const char* modes[] = {
        tcGUICore::tr("手动", "Manual"),
        tcGUICore::tr("自动", "Auto"),
        tcGUICore::tr("维护", "Service"),
    };
    const int mode = setup.mode >= 0 && setup.mode < 3 ? setup.mode : 0;
    const float gap = 16.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool wide = avail >= 860.0f;
    const float formWidth = wide ? avail * 0.62f : avail;

    ImGui::BeginGroup();
    tcGUICore::dash::beginCard(id, ImVec2(formWidth, 0.0f));
    tcGUICore::dash::caption(on ? "ONLINE" : "OFFLINE");
    ImGui::SameLine();
    tcGUICore::dash::led(on);
    tcGUICore::dash::title(title, 26.0f);
    tcGUICore::dash::hint((std::string(ep.ip) + ":" + std::to_string(ep.port)).c_str());
    ImGui::Spacing();
    tcGUICore::dash::caption(tcGUICore::tr("工作模式", "Mode"));
    drawModeChips(id, setup);
    ImGui::Spacing();
    char speedText[32];
    std::snprintf(speedText, sizeof(speedText), "%.0f%%", setup.speed);
    tcGUICore::dash::caption(tcGUICore::tr("速度上限", "Speed limit"));
    tcGUICore::dash::title(speedText, 28.0f);
    ImGui::SetNextItemWidth(-1.0f);
    tcGUICore::dash::slider((std::string("##spd") + id).c_str(), &setup.speed, 1.0f, 100.0f);
    ImGui::Spacing();
    tcGUICore::dash::caption(tcGUICore::tr("工具名", "Tool"));
    ImGui::SetNextItemWidth(-1.0f);
    tcGUICore::dash::textField((std::string("##tool") + id).c_str(), setup.tool, sizeof(setup.tool));
    ImGui::Spacing();
    tcGUICore::dash::toggleRow((std::string("##en") + id).c_str(), tcGUICore::tr("允许运动", "Enable motion"), &setup.enable);
    ImGui::Spacing();
    tcGUICore::dash::hint(tcGUICore::tr(
        "这些是控制台本地配置。PLC 符号对齐后，再从这里下发。",
        "Local console settings. They are sent after the PLC symbols are mapped."));
    tcGUICore::dash::endCard();
    ImGui::EndGroup();

    if (!wide) {
        return;
    }
    ImGui::SameLine(0.0f, gap);
    tcGUICore::dash::beginCard((std::string(id) + "_sum").c_str(), ImVec2(0.0f, 280.0f));
    tcGUICore::dash::caption(tcGUICore::tr("当前配置", "Current"));
    tcGUICore::dash::title(modes[mode], 28.0f);
    tcGUICore::dash::title(speedText, 36.0f);
    tcGUICore::dash::progress(setup.speed / 100.0f);
    ImGui::Spacing();
    tcGUICore::dash::caption(setup.tool);
    ImGui::Spacing();
    tcGUICore::dash::led(setup.enable);
    ImGui::SameLine();
    tcGUICore::dash::hint(setup.enable ? tcGUICore::tr("运动已允许", "Motion enabled") : tcGUICore::tr("运动已锁", "Motion locked"));
    tcGUICore::dash::endCard();
}

void drawGallery()
{
    const double now = ImGui::GetTime();
    for (int i = 0; i < IM_ARRAYSIZE(gGallery.wave); ++i) {
        gGallery.wave[i] = 0.5f + 0.4f * std::sin(static_cast<float>(now) * 1.4f + i * 0.28f);
    }

    tcGUICore::dash::hint(tcGUICore::tr(
        "左侧图标进入本页。下面每个控件都可以点，用来看亮灭、按下和拖动。",
        "Open this page from the left rail. Every control below can be clicked."));
    ImGui::Spacing();

    const float gap = 16.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool wide = avail >= 820.0f;
    const float col = wide ? (avail - gap) * 0.5f : avail;

    ImGui::BeginGroup();
    tcGUICore::dash::beginCard("##gal_led", ImVec2(col, 168.0f));
    tcGUICore::dash::caption(tcGUICore::tr("指示灯", "LED"));
    tcGUICore::dash::title(gGallery.ledOn ? tcGUICore::tr("亮", "On") : tcGUICore::tr("灭", "Off"), 26.0f);
    tcGUICore::dash::led(true);
    ImGui::SameLine();
    tcGUICore::dash::hint(tcGUICore::tr("常亮，带绿色光晕", "Steady on, with a green halo"));
    tcGUICore::dash::led(false);
    ImGui::SameLine();
    tcGUICore::dash::hint(tcGUICore::tr("熄灭，灰色圆点", "Off, gray dot"));
    if (tcGUICore::dash::button(gGallery.ledOn ? tcGUICore::tr("熄灭示例灯", "Turn sample off")
                                               : tcGUICore::tr("点亮示例灯", "Turn sample on"),
            ImVec2(160.0f, 34.0f), false)) {
        gGallery.ledOn = !gGallery.ledOn;
    }
    ImGui::SameLine();
    tcGUICore::dash::led(gGallery.ledOn);
    tcGUICore::dash::endCard();
    ImGui::EndGroup();
    if (wide) {
        ImGui::SameLine(0.0f, gap);
    } else {
        ImGui::Spacing();
    }

    tcGUICore::dash::beginCard("##gal_btn", ImVec2(wide ? 0.0f : col, 168.0f));
    tcGUICore::dash::caption(tcGUICore::tr("按键", "Button"));
    tcGUICore::dash::hint(tcGUICore::tr("主按键绿色，按下变深。次按键深色，悬停变亮。",
        "Primary is green and darkens when pressed. Secondary is dark and brightens on hover."));
    if (tcGUICore::dash::button(tcGUICore::tr("主按键", "Primary"), ImVec2(120.0f, 36.0f), true)) {
        gGallery.primaryDown = !gGallery.primaryDown;
    }
    ImGui::SameLine();
    if (tcGUICore::dash::button(tcGUICore::tr("次按键", "Secondary"), ImVec2(120.0f, 36.0f), false)) {
        gGallery.secondaryDown = !gGallery.secondaryDown;
    }
    tcGUICore::dash::hint(gGallery.primaryDown
            ? tcGUICore::tr("主按键已按下过", "Primary has been pressed")
            : tcGUICore::tr("主按键尚未按下", "Primary not pressed yet"));
    tcGUICore::dash::endCard();

    ImGui::Spacing();
    ImGui::BeginGroup();
    tcGUICore::dash::beginCard("##gal_sw", ImVec2(col, 210.0f));
    tcGUICore::dash::caption(tcGUICore::tr("开关", "Switch"));
    tcGUICore::dash::title(gGallery.toggleOn ? tcGUICore::tr("开", "On") : tcGUICore::tr("关", "Off"), 26.0f);
    tcGUICore::dash::hint(tcGUICore::tr("圆点滑到右侧为开，轨道变绿。", "Knob slides right when on and the track turns green."));
    tcGUICore::dash::toggle("##gal_toggle", &gGallery.toggleOn);
    ImGui::Spacing();
    tcGUICore::dash::toggleRow("##gal_row", tcGUICore::tr("允许运动", "Enable motion"), &gGallery.toggleOn);
    tcGUICore::dash::endCard();
    ImGui::EndGroup();
    if (wide) {
        ImGui::SameLine(0.0f, gap);
    } else {
        ImGui::Spacing();
    }

    tcGUICore::dash::beginCard("##gal_slider", ImVec2(wide ? 0.0f : col, 168.0f));
    char speed[32];
    std::snprintf(speed, sizeof(speed), "%.0f%%", gGallery.slider);
    tcGUICore::dash::caption(tcGUICore::tr("下滑槽", "Slider"));
    tcGUICore::dash::title(speed, 28.0f);
    tcGUICore::dash::hint(tcGUICore::tr("拖动圆点改变数值，左侧填成绿色。", "Drag the knob. The track fills green from the left."));
    ImGui::SetNextItemWidth(-1.0f);
    tcGUICore::dash::slider("##gal_spd", &gGallery.slider, 0.0f, 100.0f);
    tcGUICore::dash::endCard();

    ImGui::Spacing();
    tcGUICore::dash::beginCard("##gal_choice", ImVec2(0.0f, 230.0f));
    tcGUICore::dash::caption(tcGUICore::tr("勾选、单选、数字、徽标", "Check, radio, number, badge"));
    tcGUICore::dash::hint(tcGUICore::tr(
        "勾选是方框对勾。单选是圆点，一组里只亮一个。数字框限制范围。徽标只显示状态，不能点。",
        "Check is a box with a mark. Radio keeps one dot in a group. The number field stays in range. Badges only show status."));
    tcGUICore::dash::check("##gal_check", tcGUICore::tr("记住登录", "Remember login"), &gGallery.checked);
    ImGui::Spacing();
    tcGUICore::dash::radio("##gal_r0", tcGUICore::tr("主站", "Master"), &gGallery.radio, 0);
    ImGui::SameLine();
    tcGUICore::dash::radio("##gal_r1", tcGUICore::tr("从站", "Slave"), &gGallery.radio, 1);
    ImGui::Spacing();
    tcGUICore::dash::caption(tcGUICore::tr("端口", "Port"));
    ImGui::SetNextItemWidth(160.0f);
    tcGUICore::dash::numberField("##gal_port", &gGallery.port, 1, 65535);
    tcGUICore::dash::divider();
    tcGUICore::dash::badge(tcGUICore::tr("在线", "Online"), tcGUICore::dash::Accent::Green);
    ImGui::SameLine();
    tcGUICore::dash::badge(tcGUICore::tr("连接中", "Connecting"), tcGUICore::dash::Accent::Amber);
    ImGui::SameLine();
    tcGUICore::dash::badge(tcGUICore::tr("故障", "Fault"), tcGUICore::dash::Accent::Violet);
    tcGUICore::dash::endCard();

    ImGui::Spacing();
    tcGUICore::dash::beginCard("##gal_chip", ImVec2(0.0f, 150.0f));
    tcGUICore::dash::caption(tcGUICore::tr("标签", "Chip"));
    tcGUICore::dash::hint(tcGUICore::tr("选中时铺满对应颜色，未选中是深色胶囊。",
        "Selected chips fill with their color. Unselected chips stay dark."));
    const char* chips[] = {
        tcGUICore::tr("手动", "Manual"),
        tcGUICore::tr("自动", "Auto"),
        tcGUICore::tr("维护", "Service"),
        tcGUICore::tr("报警", "Alarm"),
    };
    const tcGUICore::dash::Accent accents[] = {
        tcGUICore::dash::Accent::Blue,
        tcGUICore::dash::Accent::Green,
        tcGUICore::dash::Accent::Amber,
        tcGUICore::dash::Accent::Violet,
    };
    for (int i = 0; i < 4; ++i) {
        if (tcGUICore::dash::chip(chips[i], gGallery.chip == i, accents[i])) {
            gGallery.chip = i;
        }
        if (i < 3) {
            ImGui::SameLine();
        }
    }
    tcGUICore::dash::endCard();

    ImGui::Spacing();
    ImGui::BeginGroup();
    tcGUICore::dash::beginCard("##gal_field", ImVec2(col, 230.0f));
    tcGUICore::dash::caption(tcGUICore::tr("输入与下拉", "Field and combo"));
    tcGUICore::dash::hint(tcGUICore::tr("圆角深色输入框。下拉打开后当前项用绿色高亮。",
        "Rounded dark field. The open list highlights the current item in green."));
    ImGui::SetNextItemWidth(-1.0f);
    tcGUICore::dash::textField("##gal_text", gGallery.text, sizeof(gGallery.text));
    const char* tools[] = {"Tool-A", "Tool-B", "Tool-C"};
    ImGui::SetNextItemWidth(-1.0f);
    tcGUICore::dash::combo("##gal_combo", &gGallery.combo, tools, IM_ARRAYSIZE(tools));
    tcGUICore::dash::endCard();
    ImGui::EndGroup();
    if (wide) {
        ImGui::SameLine(0.0f, gap);
    } else {
        ImGui::Spacing();
    }

    tcGUICore::dash::beginCard("##gal_icon", ImVec2(wide ? 0.0f : col, 230.0f));
    tcGUICore::dash::caption(tcGUICore::tr("图标按键", "Icon button"));
    tcGUICore::dash::hint(tcGUICore::tr("未选中是深色方块，选中铺绿底。悬停会略微变亮。",
        "Idle buttons are dark squares. The selected one turns green. Hover lightens them."));
    tcGUICore::dash::iconButton("##gal_i0", tcGUICore::dash::iconPlug, gGallery.iconOn);
    ImGui::SameLine();
    tcGUICore::dash::iconButton("##gal_i1", tcGUICore::dash::iconPulse, !gGallery.iconOn);
    ImGui::SameLine();
    tcGUICore::dash::iconButton("##gal_i2", tcGUICore::dash::iconArm, false);
    ImGui::SameLine();
    tcGUICore::dash::iconButton("##gal_i3", tcGUICore::dash::iconPower, false);
    if (tcGUICore::dash::button(tcGUICore::tr("切换选中", "Toggle selected"), ImVec2(140.0f, 34.0f), true)) {
        gGallery.iconOn = !gGallery.iconOn;
    }
    tcGUICore::dash::endCard();

    ImGui::Spacing();
    tcGUICore::dash::beginCard("##gal_bar", ImVec2(0.0f, 220.0f));
    tcGUICore::dash::caption(tcGUICore::tr("进度条与折线", "Progress and sparkline"));
    tcGUICore::dash::hint(tcGUICore::tr("上条跟随滑槽。中条表示连接中。下图是随时间起伏的示例曲线。",
        "Top bar follows the slider. Middle bar is the connecting animation. The plot is a live sample wave."));
    tcGUICore::dash::progress(gGallery.slider / 100.0f);
    ImGui::Spacing();
    tcGUICore::dash::progress(-1.0f);
    ImGui::Spacing();
    tcGUICore::dash::sparkline(gGallery.wave, IM_ARRAYSIZE(gGallery.wave), ImVec2(ImGui::GetContentRegionAvail().x, 110.0f));
    tcGUICore::dash::endCard();
    ImGui::Dummy(ImVec2(0.0f, 12.0f));
}

void drawLogin()
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 card(420.0f, 390.0f);
    ImGui::SetCursorPos(ImVec2((avail.x - card.x) * 0.5f, (avail.y - card.y) * 0.32f));
    tcGUICore::dash::beginCard("##login", card);
    tcGUICore::dash::title("Robot Console", 28.0f);
    tcGUICore::dash::hint(tcGUICore::tr("主从机器人控制台", "Master / slave console"));
    ImGui::Spacing();
    tcGUICore::dash::caption(tcGUICore::tr("用户", "User"));
    ImGui::SetNextItemWidth(-1.0f);
    tcGUICore::dash::textField("##user", gLogin.user, sizeof(gLogin.user));
    tcGUICore::dash::caption(tcGUICore::tr("密码", "Password"));
    ImGui::SetNextItemWidth(-1.0f);
    const bool enter = tcGUICore::dash::textField("##pass", gLogin.password, sizeof(gLogin.password),
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();
    auto submit = [&] {
        gLogin.failed = std::strcmp(gLogin.user, "admin") != 0 || std::strcmp(gLogin.password, "admin") != 0;
        gAuthed = !gLogin.failed;
    };
    if (tcGUICore::dash::button(tcGUICore::tr("登录", "Sign in"), ImVec2(-1.0f, 42.0f), true) || enter) {
        submit();
    }
    if (gLogin.failed) {
        ImGui::PushStyleColor(ImGuiCol_Text, tcGUICore::dash::palette().danger);
        ImGui::TextUnformatted(tcGUICore::tr("用户名或密码错误", "Invalid user or password"));
        ImGui::PopStyleColor();
    }
    tcGUICore::dash::hint(tcGUICore::tr("演示账号 admin / admin", "Demo account admin / admin"));
    tcGUICore::dash::endCard();
}

void drawRail()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.075f, 0.085f, 1.0f));
    ImGui::BeginChild("##rail", ImVec2(76.0f, 0.0f));
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::SetCursorPosX(16.0f);
    if (tcGUICore::dash::iconButton("##nav_gal", tcGUICore::dash::iconTiles, gPage == Page::Gallery)) {
        gPage = Page::Gallery;
    }
    ImGui::SetCursorPosX(16.0f);
    if (tcGUICore::dash::iconButton("##nav_plc", tcGUICore::dash::iconPlug, gPage == Page::Plc)) {
        gPage = Page::Plc;
    }
    ImGui::SetCursorPosX(16.0f);
    if (tcGUICore::dash::iconButton("##nav_st", tcGUICore::dash::iconPulse, gPage == Page::Status)) {
        gPage = Page::Status;
    }
    ImGui::SetCursorPosX(16.0f);
    if (tcGUICore::dash::iconButton("##nav_m", tcGUICore::dash::iconArm, gPage == Page::Master)) {
        gPage = Page::Master;
    }
    ImGui::SetCursorPosX(16.0f);
    if (tcGUICore::dash::iconButton("##nav_s", tcGUICore::dash::iconArmPair, gPage == Page::Slave)) {
        gPage = Page::Slave;
    }
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 64.0f);
    ImGui::SetCursorPosX(16.0f);
    if (tcGUICore::dash::iconButton("##logout", tcGUICore::dash::iconPower, false)) {
        gAuthed = false;
        gLogin.password[0] = '\0';
        gLogin.failed = false;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void drawHeader()
{
    const bool masterOn = linked(kMasterId);
    const bool slaveOn = linked(kSlaveId);
    ImGui::BeginGroup();
    tcGUICore::dash::title("Robot Console", 22.0f);
    const char* sub = tcGUICore::tr("控件一览", "Widget gallery");
    if (gPage == Page::Plc) {
        sub = tcGUICore::tr("PLC 连接", "PLC link");
    } else if (gPage == Page::Status) {
        sub = tcGUICore::tr("系统状态", "System status");
    } else if (gPage == Page::Master) {
        sub = tcGUICore::tr("主机器人配置", "Master setup");
    } else if (gPage == Page::Slave) {
        sub = tcGUICore::tr("从机器人配置", "Slave setup");
    }
    tcGUICore::dash::hint(sub);
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 250.0f);
    tcGUICore::dash::led(masterOn);
    ImGui::SameLine();
    tcGUICore::dash::caption(tcGUICore::tr("主站", "Master"));
    ImGui::SameLine();
    tcGUICore::dash::led(slaveOn);
    ImGui::SameLine();
    tcGUICore::dash::caption(tcGUICore::tr("从站", "Slave"));
}

} // namespace

void drawRobotConsole()
{
    if (!gAuthed) {
        drawLogin();
        return;
    }
    sampleLink();
    const float footer = ImGui::GetFrameHeightWithSpacing();
    ImVec2 body = ImGui::GetContentRegionAvail();
    body.y -= footer;
    ImGui::BeginChild("##console", body);
    drawRail();
    ImGui::SameLine();
    ImGui::BeginChild("##pages", ImVec2(0.0f, 0.0f));
    drawHeader();
    ImGui::Spacing();
    switch (gPage) {
    case Page::Gallery:
        drawGallery();
        break;
    case Page::Plc:
        drawPlcPage();
        break;
    case Page::Status:
        drawStatusPage();
        break;
    case Page::Master:
        drawRobotConfig("##cfg_m", tcGUICore::tr("主机器人", "Master"), gMasterSetup, gMasterEp, kMasterId);
        break;
    case Page::Slave:
        drawRobotConfig("##cfg_s", tcGUICore::tr("从机器人", "Slave"), gSlaveSetup, gSlaveEp, kSlaveId);
        break;
    }
    ImGui::EndChild();
    ImGui::EndChild();
}
