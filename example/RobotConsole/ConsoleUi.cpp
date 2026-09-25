#include "ConsoleUi.h"

#include "AdsManager/PlcClient.h"
#include "Common/I18n.h"
#include "tcGUICore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr const char* kMasterId = "master";
constexpr float kBootSeconds = 3.6f;
constexpr float kPlcRetrySec = 1.5f;

enum class Phase { Boot, Login, Home };
enum class HomePage { Instruments, Plc, Status };

struct LoginForm {
    char user[64] = "admin";
    char password[64] = "";
    bool failed = false;
};

struct ToolSlot {
    bool toolPresent = false;      // 器械在位
    bool isolatorPresent = false;  // 隔离板在位（器械在位时必为 true）
    tcGUICore::LocalizedText typeName;
};

struct Endpoint {
    char ip[64];
    int port = 851;
};

Phase gPhase = Phase::Boot;
HomePage gHomePage = HomePage::Instruments;
bool gAuthed = false;
LoginForm gLogin;
Endpoint gMasterEp{"127.0.0.1", 851};
double gBootStart = -1.0;
double gLastPlcTry = -1.0;

// 舱位顺序：左器械滚筒 | 内镜滚筒 | 右器械滚筒。演示默认：夹钳 / 内镜 / 电刀。
// 顺序：安装 隔离板→器械；拆卸 器械→隔离板。
ToolSlot gTools[3] = {
    {true, true, {"夹钳", "Clamp"}},
    {true, true, {"内镜", "Endoscope"}},
    {true, true, {"电刀", "Electrosurgery"}},
};

// 舱底色块填充高度动画：0 → 隔离板 1/6 → 器械满槽。
float gBayFillAnim[3] = {1.0f, 1.0f, 1.0f};

void enforceToolRules(ToolSlot& slot)
{
    if (slot.toolPresent) {
        slot.isolatorPresent = true;
    }
}

constexpr const char* kToolNames[3] = {"tool0", "tool1", "tool2"};
constexpr const char* kIsoNames[3] = {"isolator0", "isolator1", "isolator2"};
constexpr int16_t kMsReleased = 0;
constexpr int16_t kMsPaired = 1;

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

bool masterReady()
{
    return linked(kMasterId);
}

tcGUICore::RuntimeVariable* plcVar(const char* name)
{
    tcGUICore::Application* app = tcGUICore::Application::instance();
    if (!app || !name) {
        return nullptr;
    }
    return app->ads().variable(kMasterId, name);
}

bool writePlcBool(const char* name, bool value)
{
    tcGUICore::RuntimeVariable* var = plcVar(name);
    if (!var) {
        return false;
    }
    const uint8_t b = value ? 1 : 0;
    return tcGUICore::Application::instance()->ads().writeFromUi(*var, &b, 1);
}

bool readPlcBool(const char* name, bool fallback)
{
    tcGUICore::RuntimeVariable* var = plcVar(name);
    if (!var || !var->ok) {
        return fallback;
    }
    return var->data[0] != 0;
}

int16_t readMsMode()
{
    tcGUICore::RuntimeVariable* var = plcVar("msMode");
    if (!var || !var->ok) {
        return kMsReleased;
    }
    int16_t mode = 0;
    std::memcpy(&mode, var->data.data(), sizeof(mode));
    return mode;
}

bool msScreenLocked()
{
    return masterReady() && readMsMode() == kMsPaired;
}

void syncToolsFromPlc()
{
    if (!masterReady()) {
        return;
    }
    for (int i = 0; i < 3; ++i) {
        gTools[i].toolPresent = readPlcBool(kToolNames[i], gTools[i].toolPresent);
        gTools[i].isolatorPresent = readPlcBool(kIsoNames[i], gTools[i].isolatorPresent);
        enforceToolRules(gTools[i]);
    }
}

void applyToolPresent(int index, bool present)
{
    if (index < 0 || index > 2) {
        return;
    }
    // 顺序：先装隔离板，再装器械；拆卸器械不依赖其它。
    if (present && !gTools[index].isolatorPresent) {
        return;
    }
    gTools[index].toolPresent = present;
    enforceToolRules(gTools[index]);
    writePlcBool(kToolNames[index], gTools[index].toolPresent);
    writePlcBool(kIsoNames[index], gTools[index].isolatorPresent);
}

void applyIsolatorPresent(int index, bool present)
{
    if (index < 0 || index > 2) {
        return;
    }
    // 顺序：器械未拆卸则不能拆隔离板。
    if (!present && gTools[index].toolPresent) {
        return;
    }
    gTools[index].isolatorPresent = present;
    enforceToolRules(gTools[index]);
    writePlcBool(kToolNames[index], gTools[index].toolPresent);
    writePlcBool(kIsoNames[index], gTools[index].isolatorPresent);
}

void requestUserUnlock()
{
    writePlcBool("userUnlock", true);
}

void tryConnectMaster()
{
    tcGUICore::Application* app = tcGUICore::Application::instance();
    tcGUICore::PlcClient* plc = clientOf(kMasterId);
    if (!app || !plc) {
        return;
    }
    const auto state = plc->state();
    if (state == tcGUICore::ConnectionState::Connected ||
        state == tcGUICore::ConnectionState::Connecting) {
        return;
    }
    // Error / Disconnected：清掉上次失败会话后再连，登录页持续自动重试。
    if (state == tcGUICore::ConnectionState::Error) {
        plc->disconnect();
    }
    const auto port = static_cast<uint16_t>(gMasterEp.port > 0 && gMasterEp.port < 65536 ? gMasterEp.port : 851);
    app->ads().applyConnectionSettings(kMasterId, gMasterEp.ip, port, tcGUICore::amsNetIdFromIp(gMasterEp.ip));
    plc->connectAsync();
}

void pollMasterLink()
{
    const double now = ImGui::GetTime();
    if (gLastPlcTry < 0.0 || now - gLastPlcTry >= kPlcRetrySec) {
        gLastPlcTry = now;
        tryConnectMaster();
    }
}

ImU32 pack(const ImVec4& c)
{
    return ImGui::ColorConvertFloat4ToU32(c);
}

void drawBoot()
{
    if (gBootStart < 0.0) {
        gBootStart = ImGui::GetTime();
        gLastPlcTry = -1.0;
    }
    pollMasterLink();

    const double elapsed = ImGui::GetTime() - gBootStart;
    const float progress = std::clamp(static_cast<float>(elapsed / kBootSeconds), 0.0f, 1.0f);
    const bool done = progress >= 1.0f;
    const bool plcOn = masterReady();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto& pal = tcGUICore::dash::palette();

    dl->AddRectFilled(origin, origin + avail, pack(pal.window));

    const ImVec2 center(origin.x + avail.x * 0.5f, origin.y + avail.y * 0.42f);
    const float t = static_cast<float>(ImGui::GetTime());
    const float pulse = 0.5f + 0.5f * std::sin(t * 2.2f);

    for (int i = 0; i < 4; ++i) {
        const float radius = 48.0f + i * 34.0f + pulse * 6.0f;
        const float alpha = (0.18f - i * 0.035f) * (0.45f + progress * 0.55f);
        ImVec4 ring = pal.blue;
        if (i == 1) {
            ring = pal.cyan;
        } else if (i >= 2) {
            ring = pal.green;
        }
        ring.w = alpha;
        dl->AddCircle(center, radius, pack(ring), 64, 2.0f + (i == 0 ? 1.2f : 0.0f));
    }

    // 中心十字与弧线：手术机器人开机意象，不抢主文案。
    const float arm = 22.0f + progress * 10.0f;
    dl->AddCircleFilled(center, 10.0f, pack(ImVec4(pal.green.x, pal.green.y, pal.green.z, 0.35f + pulse * 0.25f)), 24);
    dl->AddLine(center - ImVec2(arm, 0.0f), center + ImVec2(arm, 0.0f), pack(pal.text), 2.0f);
    dl->AddLine(center - ImVec2(0.0f, arm), center + ImVec2(0.0f, arm), pack(pal.text), 2.0f);
    dl->PathClear();
    dl->PathArcTo(center, 38.0f, t * 1.6f, t * 1.6f + 2.2f, 32);
    dl->PathStroke(pack(ImVec4(pal.cyan.x, pal.cyan.y, pal.cyan.z, 0.75f)), 0, 2.4f);

    ImGui::SetCursorScreenPos(ImVec2(origin.x, center.y + 96.0f));
    ImGui::PushItemWidth(avail.x);
    ImGui::BeginGroup();
    {
        const char* brand = "Surgeon Console";
        ImGui::PushFont(ImGui::GetFont(), 28.0f);
        const ImVec2 brandSize = ImGui::CalcTextSize(brand);
        ImGui::SetCursorPosX((avail.x - brandSize.x) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, pal.text);
        ImGui::TextUnformatted(brand);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    {
        const char* line = tcGUICore::tr("灵窥手术机器人 · 医生控制台", "Lingkui Surgical Robot · Surgeon console");
        const ImVec2 lineSize = ImGui::CalcTextSize(line);
        ImGui::SetCursorPosX(std::max(0.0f, (avail.x - lineSize.x) * 0.5f));
        tcGUICore::dash::hint(line);
    }
    ImGui::Dummy(ImVec2(0.0f, 18.0f));

    const float barW = std::min(420.0f, avail.x * 0.55f);
    ImGui::SetCursorPosX((avail.x - barW) * 0.5f);
    ImGui::BeginChild("##boot_bar", ImVec2(barW, 64.0f), ImGuiChildFlags_None);
    tcGUICore::dash::progress(progress);
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    tcGUICore::dash::led(plcOn);
    ImGui::SameLine();
    tcGUICore::dash::hint(plcOn
            ? tcGUICore::tr("Control Core 已就绪", "Control Core ready")
            : (connecting(kMasterId)
                    ? tcGUICore::tr("正在连接 Control Core…", "Connecting Control Core…")
                    : tcGUICore::tr("等待 Control Core…", "Waiting for Control Core…")));
    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::PopItemWidth();

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(avail);

    if (done) {
        gPhase = Phase::Login;
    }
}

void drawLogin()
{
    pollMasterLink();
    const bool plcOn = masterReady();
    const bool busy = connecting(kMasterId);
    tcGUICore::PlcClient* plc = clientOf(kMasterId);
    const auto& pal = tcGUICore::dash::palette();

    // 首次进入：用 JSON 里的 Control Core 地址初始化编辑框。
    if (plc && gMasterEp.ip[0] != '\0') {
        static bool seeded = false;
        if (!seeded) {
            const auto cfg = plc->config();
            if (!cfg.ip.empty()) {
                std::snprintf(gMasterEp.ip, sizeof(gMasterEp.ip), "%s", cfg.ip.c_str());
            }
            if (cfg.adsPort > 0) {
                gMasterEp.port = static_cast<int>(cfg.adsPort);
            }
            seeded = true;
        }
    }

    // 标题栏下方剩余区域：用屏幕坐标居中，避免 SetCursorPos 相对窗口顶导致偏上。
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, origin + avail, pack(pal.window));

    const ImVec2 card(440.0f, plcOn ? 460.0f : 560.0f);
    const float padX = std::max(0.0f, (avail.x - card.x) * 0.5f);
    const float padY = std::max(0.0f, (avail.y - card.y) * 0.5f);
    ImGui::SetCursorScreenPos(origin + ImVec2(padX, padY));

    tcGUICore::dash::beginCard("##login", card);
    tcGUICore::dash::title("Surgeon Console", 28.0f);
    tcGUICore::dash::hint(tcGUICore::tr("医生控制台登录", "Surgeon console sign-in"));
    ImGui::Spacing();

    ImGui::BeginGroup();
    tcGUICore::dash::led(plcOn);
    ImGui::SameLine();
    if (plcOn) {
        tcGUICore::dash::badge("Control Core", tcGUICore::dash::Accent::Green);
    } else if (busy) {
        tcGUICore::dash::badge(tcGUICore::tr("连接中", "Connecting"), tcGUICore::dash::Accent::Amber);
    } else {
        tcGUICore::dash::badge(tcGUICore::tr("等待 Control Core", "Waiting Control Core"), tcGUICore::dash::Accent::Amber);
    }
    ImGui::EndGroup();
    ImGui::Spacing();
    tcGUICore::dash::hint(plcOn
            ? tcGUICore::tr("Control Core 已连接，可以登录。", "Control Core linked. You may sign in.")
            : tcGUICore::tr("正在等待 Control Core 并自动重试连接…",
                "Waiting for Control Core; auto-retrying connection…"));
    if (!plcOn && plc) {
        const std::string detail = plc->lastError().empty() ? plc->statusText() : plc->lastError();
        if (!detail.empty() && detail != "—") {
            ImGui::PushStyleColor(ImGuiCol_Text, pal.muted);
            ImGui::TextWrapped("%s", detail.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        tcGUICore::dash::caption("IP");
        ImGui::SetNextItemWidth(-1.0f);
        if (tcGUICore::dash::textField("##login_ip", gMasterEp.ip, sizeof(gMasterEp.ip))) {
            gLastPlcTry = -1.0;
        }
        tcGUICore::dash::caption(tcGUICore::tr("端口", "Port"));
        ImGui::SetNextItemWidth(160.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, pal.field);
        ImGui::PushStyleColor(ImGuiCol_Text, pal.text);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
        if (ImGui::InputInt("##login_port", &gMasterEp.port)) {
            gLastPlcTry = -1.0;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (gMasterEp.port < 1) {
            gMasterEp.port = 851;
        }
        ImGui::Spacing();
        if (tcGUICore::dash::button(tcGUICore::tr("重试连接", "Retry link"), ImVec2(-1.0f, 36.0f), false) &&
            !busy) {
            gLastPlcTry = -1.0;
            tryConnectMaster();
        }
    }
    ImGui::Spacing();

    // 等 Control Core 时可先填账号；仅登录键受门控。
    tcGUICore::dash::caption(tcGUICore::tr("用户", "User"));
    ImGui::SetNextItemWidth(-1.0f);
    tcGUICore::dash::textField("##user", gLogin.user, sizeof(gLogin.user));
    tcGUICore::dash::caption(tcGUICore::tr("密码", "Password"));
    ImGui::SetNextItemWidth(-1.0f);
    const bool enter = tcGUICore::dash::textField("##pass", gLogin.password, sizeof(gLogin.password),
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();

    auto submit = [&] {
        if (!plcOn) {
            return;
        }
        gLogin.failed = std::strcmp(gLogin.user, "admin") != 0 || std::strcmp(gLogin.password, "admin") != 0;
        gAuthed = !gLogin.failed;
        if (gAuthed) {
            gPhase = Phase::Home;
            gHomePage = HomePage::Instruments;
        }
    };

    ImGui::BeginDisabled(!plcOn);
    const bool clicked = tcGUICore::dash::button(tcGUICore::tr("登录", "Sign in"), ImVec2(-1.0f, 44.0f), true);
    ImGui::EndDisabled();
    if (plcOn && (clicked || enter)) {
        submit();
    }

    if (!plcOn) {
        ImGui::Spacing();
        tcGUICore::dash::progress(-1.0f);
    }
    if (gLogin.failed) {
        ImGui::PushStyleColor(ImGuiCol_Text, pal.danger);
        ImGui::TextUnformatted(tcGUICore::tr("用户名或密码错误", "Invalid user or password"));
        ImGui::PopStyleColor();
    }
    tcGUICore::dash::hint(tcGUICore::tr("演示账号 admin / admin", "Demo account admin / admin"));
    tcGUICore::dash::endCard();
}

float gVolume = 0.55f;
bool gVolumeOpen = false;

// 状态行 + 安装/拆卸按键；点击且允许时返回 true。
bool drawStatusRow(const char* toggleId, const char* label, bool on,
    const char* onText, const char* offText, bool actionEnabled)
{
    const float rowStartX = ImGui::GetCursorPosX();
    const float availW = ImGui::GetContentRegionAvail().x;
    const float btnW = 72.0f;
    const float btnH = 28.0f;

    tcGUICore::dash::led(on);
    ImGui::SameLine();
    ImGui::BeginGroup();
    tcGUICore::dash::caption(label);
    ImGui::PushStyleColor(ImGuiCol_Text, on ? tcGUICore::dash::palette().text : tcGUICore::dash::palette().muted);
    ImGui::TextUnformatted(on ? onText : offText);
    ImGui::PopStyleColor();
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::SetCursorPosX(rowStartX + std::max(0.0f, availW - btnW));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f);
    const char* action = on ? tcGUICore::tr("拆卸", "Remove") : tcGUICore::tr("安装", "Install");
    ImGui::PushID(toggleId);
    ImGui::BeginDisabled(!actionEnabled);
    const bool pressed = tcGUICore::dash::button(action, ImVec2(btnW, btnH), !on && actionEnabled);
    ImGui::EndDisabled();
    ImGui::PopID();
    return pressed && actionEnabled;
}

void drawToolCard(const char* id, const char* role, int bayIndex, ToolSlot& slot, const ImVec2& size,
    tcGUICore::dash::Accent accent)
{
    enforceToolRules(slot);
    const auto& pal = tcGUICore::dash::palette();
    const ImU32 accentCol = tcGUICore::dash::colorOf(accent);
    ImVec4 accentVec = ImGui::ColorConvertU32ToFloat4(accentCol);

    // 目标高度：未装=0，仅隔离板=1/6，器械安装=满槽。
    const float targetFill = slot.toolPresent ? 1.0f : (slot.isolatorPresent ? (1.0f / 6.0f) : 0.0f);
    const float dt = ImGui::GetIO().DeltaTime;
    const float ease = 1.0f - std::exp(-10.0f * std::max(dt, 0.0f));
    gBayFillAnim[bayIndex] += (targetFill - gBayFillAnim[bayIndex]) * ease;
    if (std::fabs(gBayFillAnim[bayIndex] - targetFill) < 0.001f) {
        gBayFillAnim[bayIndex] = targetFill;
    }
    const float fill = gBayFillAnim[bayIndex];
    // 色块已半透明，文字保持浅色即可。
    const bool textOnFill = false;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.card);
    ImGui::PushStyleColor(ImGuiCol_Border, pal.cardBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 18.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 18.0f));
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 win = ImGui::GetWindowPos();
    const ImVec2 winSize = ImGui::GetWindowSize();
    const float round = 18.0f;

    // 自下而上弹出的主题色块：倒角与器械槽一致，并带透明度。
    if (fill > 0.001f) {
        const float blockH = winSize.y * fill;
        const ImVec2 p0(win.x, win.y + winSize.y - blockH);
        const ImVec2 p1(win.x + winSize.x, win.y + winSize.y);
        ImVec4 blockCol = accentVec;
        blockCol.w = slot.toolPresent || fill > 0.9f ? 0.42f : 0.36f;
        const float r = std::min(round, blockH * 0.5f);
        dl->AddRectFilled(p0, p1, pack(blockCol), r);
    }

    if (textOnFill) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.06f, 0.08f, 0.10f, 0.78f));
        ImGui::TextUnformatted(role);
        ImGui::PopStyleColor();
    } else {
        tcGUICore::dash::caption(role);
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    const std::string type = tcGUICore::displayNameText(slot.typeName);
    if (slot.toolPresent && !type.empty()) {
        if (textOnFill) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.07f, 0.09f, 1.0f));
            ImGui::PushFont(ImGui::GetFont(), 30.0f);
            ImGui::TextUnformatted(type.c_str());
            ImGui::PopFont();
            ImGui::PopStyleColor();
        } else {
            tcGUICore::dash::title(type.c_str(), 30.0f);
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, textOnFill
                ? ImVec4(0.06f, 0.08f, 0.10f, 0.55f)
                : pal.muted);
        ImGui::PushFont(ImGui::GetFont(), 30.0f);
        ImGui::TextUnformatted("—");
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0.0f, 20.0f));
    char toolToggleId[32];
    char isoToggleId[32];
    std::snprintf(toolToggleId, sizeof(toolToggleId), "tool_tog_%d", bayIndex);
    std::snprintf(isoToggleId, sizeof(isoToggleId), "iso_tog_%d", bayIndex);

    // 安装顺序：隔离板 → 器械；拆卸顺序：器械 → 隔离板。
    const bool canInstallTool = slot.isolatorPresent;
    const bool canRemoveIsolator = !slot.toolPresent;
    const bool toolActionOk = slot.toolPresent ? true : canInstallTool;
    const bool isoActionOk = slot.isolatorPresent ? canRemoveIsolator : true;

    if (drawStatusRow(
            toolToggleId,
            tcGUICore::tr("器械", "Instrument"),
            slot.toolPresent,
            tcGUICore::tr("已安装", "Installed"),
            tcGUICore::tr("未安装", "Not installed"),
            toolActionOk)) {
        applyToolPresent(bayIndex, !slot.toolPresent);
    }
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    if (drawStatusRow(
            isoToggleId,
            tcGUICore::tr("隔离板", "Isolator"),
            slot.isolatorPresent,
            tcGUICore::tr("已安装", "Installed"),
            tcGUICore::tr("未安装", "Not installed"),
            isoActionOk)) {
        applyIsolatorPresent(bayIndex, !slot.isolatorPresent);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void drawMsLockOverlay()
{
    if (!msScreenLocked()) {
        return;
    }

    const ImVec2 origin = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();

    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.024f, 0.031f, 0.047f, 0.88f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##ms_lock_layer", size, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 panel(400.0f, 200.0f);
    ImGui::SetCursorPos(ImVec2((size.x - panel.x) * 0.5f, (size.y - panel.y) * 0.5f));
    tcGUICore::dash::beginCard("##ms_lock_card", panel);
    tcGUICore::dash::caption(tcGUICore::tr("主从状态", "Master-Slave"));
    tcGUICore::dash::title(tcGUICore::tr("屏幕已锁定", "Screen locked"), 26.0f);
    tcGUICore::dash::hint(tcGUICore::tr("确认安全可解锁", "Confirm safe to unlock"));
    ImGui::Spacing();
    if (tcGUICore::dash::button(tcGUICore::tr("解锁", "Unlock"), ImVec2(-1.0f, 44.0f), true)) {
        requestUserUnlock();
    }
    tcGUICore::dash::endCard();

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void drawVolumeStrip(const ImVec2& size)
{
    const auto& pal = tcGUICore::dash::palette();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.11f, 0.13f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, pal.cardBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 16.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
    ImGui::BeginChild("##volume_strip", size, ImGuiChildFlags_Borders);

    const float rowH = ImGui::GetContentRegionAvail().y;
    const float btn = std::min(48.0f, rowH);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowH - btn) * 0.5f);

    const ImVec2 btnPos = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##volume_btn", ImVec2(btn, btn))) {
        gVolumeOpen = !gVolumeOpen;
    }
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 c = btnPos + ImVec2(btn * 0.5f, btn * 0.5f);
        const ImU32 col = pack(gVolumeOpen ? pal.cyan : pal.muted);
        const ImU32 bg = pack(gVolumeOpen ? ImVec4(pal.cyan.x, pal.cyan.y, pal.cyan.z, 0.18f) : pal.field);
        dl->AddRectFilled(btnPos, btnPos + ImVec2(btn, btn), bg, 14.0f);
        dl->AddRectFilled(c + ImVec2(-12.0f, -5.0f), c + ImVec2(-4.0f, 5.0f), col, 2.0f);
        dl->AddTriangleFilled(c + ImVec2(-6.0f, -8.0f), c + ImVec2(-6.0f, 8.0f), c + ImVec2(6.0f, 0.0f), col);
        dl->PathClear();
        dl->PathArcTo(c + ImVec2(4.0f, 0.0f), 9.0f, -0.85f, 0.85f, 12);
        dl->PathStroke(col, 0, 2.0f);
        if (gVolumeOpen) {
            dl->PathClear();
            dl->PathArcTo(c + ImVec2(4.0f, 0.0f), 14.0f, -0.85f, 0.85f, 12);
            dl->PathStroke(col, 0, 1.6f);
        }
    }

    ImGui::SameLine(0.0f, 14.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (btn - ImGui::GetFrameHeight()) * 0.5f);

    if (gVolumeOpen) {
        const float barW = std::max(180.0f, ImGui::GetContentRegionAvail().x - 80.0f);
        ImGui::SetNextItemWidth(barW);
        tcGUICore::dash::slider("##volume", &gVolume, 0.0f, 1.0f);
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::AlignTextToFramePadding();
        char level[32];
        std::snprintf(level, sizeof(level), "%d%%", static_cast<int>(gVolume * 100.0f + 0.5f));
        ImGui::TextUnformatted(level);
    } else {
        ImGui::AlignTextToFramePadding();
        tcGUICore::dash::caption(tcGUICore::tr("点击声音键调节音量", "Tap sound to adjust volume"));
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void drawInstrumentsPage()
{
    syncToolsFromPlc();

    const auto& pal = tcGUICore::dash::palette();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(origin, origin + avail, pack(pal.window), 12.0f);

    const float gap = 18.0f;
    const float bottomH = avail.y * 0.25f;
    const float cardH = std::max(180.0f, avail.y - bottomH - gap);
    const float width = (avail.x - gap * 2.0f) / 3.0f;
    const ImVec2 cardSize(width, cardH);

    drawToolCard("##tool_l", tcGUICore::tr("左器械滚筒", "Left tool roller"),
        0, gTools[0], cardSize, tcGUICore::dash::Accent::Blue);
    ImGui::SameLine(0.0f, gap);
    drawToolCard("##tool_e", tcGUICore::tr("内镜滚筒", "Endoscope roller"),
        1, gTools[1], cardSize, tcGUICore::dash::Accent::Violet);
    ImGui::SameLine(0.0f, gap);
    drawToolCard("##tool_r", tcGUICore::tr("右器械滚筒", "Right tool roller"),
        2, gTools[2], cardSize, tcGUICore::dash::Accent::Cyan);

    ImGui::Dummy(ImVec2(0.0f, gap));
    drawVolumeStrip(ImVec2(avail.x, std::max(64.0f, bottomH - gap)));
}

void drawPlcPage()
{
    tcGUICore::PlcClient* plc = clientOf(kMasterId);
    const bool on = masterReady();
    const bool busy = connecting(kMasterId);
    tcGUICore::dash::beginCard("##plc_master", ImVec2(0.0f, 300.0f));
    tcGUICore::dash::caption("MASTER");
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 36.0f);
    tcGUICore::dash::led(on);
    tcGUICore::dash::title("Control Core", 24.0f);
    ImGui::Spacing();
    tcGUICore::dash::caption("IP");
    ImGui::SetNextItemWidth(-1.0f);
    tcGUICore::dash::textField("##mip", gMasterEp.ip, sizeof(gMasterEp.ip));
    tcGUICore::dash::caption(tcGUICore::tr("端口", "Port"));
    ImGui::SetNextItemWidth(160.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, tcGUICore::dash::palette().field);
    ImGui::PushStyleColor(ImGuiCol_Text, tcGUICore::dash::palette().text);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    ImGui::InputInt("##mport", &gMasterEp.port);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if (gMasterEp.port < 1) {
        gMasterEp.port = 851;
    }
    ImGui::Spacing();
    tcGUICore::dash::progress(busy ? -1.0f : (on ? 1.0f : 0.0f));
    ImGui::Spacing();
    if (tcGUICore::dash::button(on ? tcGUICore::tr("断开", "Disconnect") : tcGUICore::tr("连接", "Connect"),
            ImVec2(148.0f, 38.0f), !on) &&
        !busy) {
        if (on) {
            plc->disconnect();
        } else {
            gLastPlcTry = -1.0;
            tryConnectMaster();
        }
    }
    ImGui::SameLine();
    if (plc) {
        tcGUICore::dash::hint(on ? plc->adsStateText().c_str() : plc->statusText().c_str());
    }
    tcGUICore::dash::endCard();
}

void drawStatusPage()
{
    syncToolsFromPlc();
    tcGUICore::PlcClient* plc = clientOf(kMasterId);
    const bool on = masterReady();
    for (ToolSlot& slot : gTools) {
        enforceToolRules(slot);
    }
    tcGUICore::dash::beginCard("##status", ImVec2(0.0f, 260.0f));
    tcGUICore::dash::caption(tcGUICore::tr("系统状态", "System status"));
    tcGUICore::dash::title(on ? tcGUICore::tr("Control Core 在线", "Control Core online")
                              : tcGUICore::tr("Control Core 离线", "Control Core offline"), 26.0f);
    tcGUICore::dash::progress(on ? 1.0f : 0.0f);
    ImGui::Spacing();
    tcGUICore::dash::hint(plc ? plc->statusText().c_str() : "—");
    ImGui::Spacing();
    tcGUICore::dash::chip(tcGUICore::tr("左·夹钳", "L·Clamp"), gTools[0].toolPresent, tcGUICore::dash::Accent::Blue);
    ImGui::SameLine();
    tcGUICore::dash::chip(tcGUICore::tr("中·内镜", "C·Scope"), gTools[1].toolPresent, tcGUICore::dash::Accent::Violet);
    ImGui::SameLine();
    tcGUICore::dash::chip(tcGUICore::tr("右·电刀", "R·Energy"), gTools[2].toolPresent, tcGUICore::dash::Accent::Cyan);
    ImGui::Spacing();
    tcGUICore::dash::chip(tcGUICore::tr("左隔离板", "L isolator"), gTools[0].isolatorPresent, tcGUICore::dash::Accent::Green);
    ImGui::SameLine();
    tcGUICore::dash::chip(tcGUICore::tr("镜隔离板", "Scope isolator"), gTools[1].isolatorPresent, tcGUICore::dash::Accent::Green);
    ImGui::SameLine();
    tcGUICore::dash::chip(tcGUICore::tr("右隔离板", "R isolator"), gTools[2].isolatorPresent, tcGUICore::dash::Accent::Green);
    tcGUICore::dash::endCard();
}

void drawRail()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.075f, 0.085f, 1.0f));
    ImGui::BeginChild("##rail", ImVec2(76.0f, 0.0f));
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::SetCursorPosX(16.0f);
    if (tcGUICore::dash::iconButton("##nav_tools", tcGUICore::dash::iconTiles, gHomePage == HomePage::Instruments)) {
        gHomePage = HomePage::Instruments;
    }
    ImGui::SetCursorPosX(16.0f);
    if (tcGUICore::dash::iconButton("##nav_plc", tcGUICore::dash::iconPlug, gHomePage == HomePage::Plc)) {
        gHomePage = HomePage::Plc;
    }
    ImGui::SetCursorPosX(16.0f);
    if (tcGUICore::dash::iconButton("##nav_st", tcGUICore::dash::iconPulse, gHomePage == HomePage::Status)) {
        gHomePage = HomePage::Status;
    }
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 64.0f);
    ImGui::SetCursorPosX(16.0f);
    if (tcGUICore::dash::iconButton("##logout", tcGUICore::dash::iconPower, false)) {
        gAuthed = false;
        gPhase = Phase::Login;
        gLogin.password[0] = '\0';
        gLogin.failed = false;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void drawHeader()
{
    const bool plcOn = masterReady();
    const float rowH = 32.0f;

    // 左：主标题
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    tcGUICore::dash::title("Main Page", 22.0f);
    const char* sub = tcGUICore::tr("器械舱", "Instrument bay");
    if (gHomePage == HomePage::Plc) {
        sub = tcGUICore::tr("Control Core 连接", "Control Core link");
    } else if (gHomePage == HomePage::Status) {
        sub = tcGUICore::tr("系统状态", "System status");
    }
    tcGUICore::dash::hint(sub);
    ImGui::EndGroup();

    // 右：灯 / Control Core / 语言 —— 同一行垂直居中对齐
    const char* ctrl = "Control Core";
    const char* langLabel = tcGUICore::currentLanguage() == tcGUICore::Language::Zh ? "中文" : "EN";
    const float langW = 72.0f;
    const float rightW = 14.0f + 8.0f + ImGui::CalcTextSize(ctrl).x + 16.0f + langW;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 16.0f, ImGui::GetWindowWidth() - rightW - 8.0f));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 0.0f));
    ImGui::AlignTextToFramePadding();
    tcGUICore::dash::led(plcOn, 6.0f);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, tcGUICore::dash::palette().muted);
    ImGui::TextUnformatted(ctrl);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (tcGUICore::dash::button(langLabel, ImVec2(langW, rowH), false)) {
        tcGUICore::toggleLanguage();
        if (tcGUICore::Application* app = tcGUICore::Application::instance()) {
            app->setLanguage(tcGUICore::currentLanguage());
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndGroup();
}

void drawHome()
{
    if (!masterReady()) {
        // 会话中 Control Core 掉线：退回登录并禁止操作。
        gAuthed = false;
        gPhase = Phase::Login;
        gLogin.failed = false;
        return;
    }
    pollMasterLink();
    syncToolsFromPlc();
    const float footer = ImGui::GetFrameHeightWithSpacing();
    ImVec2 body = ImGui::GetContentRegionAvail();
    body.y -= footer;
    ImGui::BeginChild("##console", body);
    drawRail();
    ImGui::SameLine();
    ImGui::BeginChild("##pages", ImVec2(0.0f, 0.0f));
    drawHeader();
    ImGui::Spacing();
    switch (gHomePage) {
    case HomePage::Instruments:
        drawInstrumentsPage();
        break;
    case HomePage::Plc:
        drawPlcPage();
        break;
    case HomePage::Status:
        drawStatusPage();
        break;
    }
    ImGui::EndChild();
    drawMsLockOverlay();
    ImGui::EndChild();
}

} // namespace

void drawRobotConsole()
{
    switch (gPhase) {
    case Phase::Boot:
        drawBoot();
        break;
    case Phase::Login:
        drawLogin();
        break;
    case Phase::Home:
        drawHome();
        break;
    }
}
