#include "ConsoleUi.h"

#include "AdsManager/PlcClient.h"
#include "Common/I18n.h"
#include "Viewer/Theme.h"
#include "tcGUICore.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr const char* kMasterId = "master";
constexpr float kBootSeconds = 3.6f;
constexpr float kPlcRetrySec = 5.0f;

enum class Phase { Boot, Login, Home };
enum class HomePage { Instruments, Plc, Status };
enum class OskTarget { None, User, Password };

struct LoginForm {
    char user[64] = "admin";
    char password[64] = "admin";
    bool failed = false;
};

// 与 TwinCAT E_ToolType (UINT) 对齐。
enum class E_ToolType : uint16_t {
    None = 0,
    Volkmann = 1,      // 挂匙
    Clamp = 2,         // 夹钳
    ElectKnife = 3,    // 电刀
    ElectKnifeV2 = 4,  // 电刀V2
    Endoscopic = 99,   // 内镜
    OneMotor = 100,    // 单电机
};

struct ToolSlot {
    bool toolPresent = false;      // 器械在位
    bool isolatorPresent = false;  // 隔离板在位（器械在位时必为 true）
    E_ToolType toolType = E_ToolType::None;
    uint32_t toolLife = 0;         // 器械寿命（PLC UDINT）
    uint32_t toolVersion = 0;      // 器械版本（PLC UDINT）
};

struct Endpoint {
    char ip[64];
    int port = 851;
};

Phase gPhase = Phase::Boot;
HomePage gHomePage = HomePage::Instruments;
bool gAuthed = false;
LoginForm gLogin;
OskTarget gOsk = OskTarget::None;
bool gOskShift = false;
bool gOskDigits = false;
float gOskSlide = 0.0f;
bool gOskKeysArmed = false; // 键盘完全展开且手指抬起后才接受按键，避免幻触乱输入
double gOskArmAt = -1.0;    // 展开后再延迟一小段才允许按键
double gLastOskKeyAt = -1.0; // 屏幕键盘按键最小间隔，抑制连点/噪声
int gOskBurstKeys = 0;      // 武装后短时间内的按键数，用于识别幻触连打
double gLoginReadyAt = -1.0; // 进入登录页后短暂吞掉残留触摸/按键
bool gLoginWaitPointerUp = true; // 必须先看到指针抬起，才能接受点击
bool gLoginEditUnlocked = false; // 未确认手势前，每帧强制 admin/admin
Endpoint gMasterEp{"172.13.158.64", 851};
double gBootStart = -1.0;
double gLastPlcTry = -1.0;

// 舱位顺序：左器械滚筒 | 内镜滚筒 | 右器械滚筒。
// 器械类型由 PLC eToolType 决定；顺序：安装 隔离板→器械；拆卸 器械→隔离板。
ToolSlot gTools[3] = {};

// 舱底色块填充高度动画：0 → 隔离板 1/6 → 器械满槽。
float gBayFillAnim[3] = {1.0f, 1.0f, 1.0f};

float px(float v);

void enforceToolRules(ToolSlot& slot)
{
    if (slot.toolPresent) {
        slot.isolatorPresent = true;
    }
}

constexpr const char* kToolNames[3] = {"tool0", "tool1", "tool2"};
constexpr const char* kIsoNames[3] = {"isolator0", "isolator1", "isolator2"};
constexpr const char* kToolTypeNames[3] = {"toolType0", "toolType1", "toolType2"};
constexpr const char* kToolLifeNames[3] = {"toolLife0", "toolLife1", "toolLife2"};
constexpr const char* kToolVersionNames[3] = {"toolVersion0", "toolVersion1", "toolVersion2"};
constexpr int16_t kMsReleased = 0;
constexpr int16_t kMsPaired = 1;

tcGUICore::LocalizedText toolTypeLabel(E_ToolType type)
{
    switch (type) {
    case E_ToolType::None:
        return {"None", "None"};
    case E_ToolType::Volkmann:
        return {"挂匙", "Volkmann"};
    case E_ToolType::Clamp:
        return {"夹钳", "Clamp"};
    case E_ToolType::ElectKnife:
        return {"电刀", "ElectKnife"};
    case E_ToolType::ElectKnifeV2:
        return {"电刀V2", "ElectKnifeV2"};
    case E_ToolType::Endoscopic:
        return {"内镜", "Endoscope"};
    case E_ToolType::OneMotor:
        return {"单电机", "OneMotor"};
    }
    return {"未知", "Unknown"};
}

E_ToolType normalizeToolType(uint16_t raw)
{
    switch (static_cast<E_ToolType>(raw)) {
    case E_ToolType::None:
    case E_ToolType::Volkmann:
    case E_ToolType::Clamp:
    case E_ToolType::ElectKnife:
    case E_ToolType::ElectKnifeV2:
    case E_ToolType::Endoscopic:
    case E_ToolType::OneMotor:
        return static_cast<E_ToolType>(raw);
    }
    return E_ToolType::None;
}

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

uint16_t readPlcU16(const char* name, uint16_t fallback)
{
    tcGUICore::RuntimeVariable* var = plcVar(name);
    if (!var || !var->ok || var->data.size() < sizeof(uint16_t)) {
        return fallback;
    }
    uint16_t value = 0;
    std::memcpy(&value, var->data.data(), sizeof(value));
    return value;
}

uint32_t readPlcU32(const char* name, uint32_t fallback)
{
    tcGUICore::RuntimeVariable* var = plcVar(name);
    if (!var || !var->ok || var->data.size() < sizeof(uint32_t)) {
        return fallback;
    }
    uint32_t value = 0;
    std::memcpy(&value, var->data.data(), sizeof(value));
    return value;
}

float readPlcFloat(const char* name, float fallback)
{
    tcGUICore::RuntimeVariable* var = plcVar(name);
    if (!var || !var->ok || var->data.size() < sizeof(float)) {
        return fallback;
    }
    float value = 0.0f;
    std::memcpy(&value, var->data.data(), sizeof(value));
    return value;
}

bool writePlcFloat(const char* name, float value)
{
    tcGUICore::RuntimeVariable* var = plcVar(name);
    if (!var) {
        return false;
    }
    return tcGUICore::Application::instance()->ads().writeFromUi(*var, &value, sizeof(value));
}

bool msScreenLocked()
{
    return masterReady() && readMsMode() == kMsPaired;
}

double gLastToolSync = -1.0;
float gVolume = 0.55f;
bool gVolumeOpen = false;
bool gVolumeSeeded = false;
float gLastVolumeWrote = -1.0f;

void syncVolumeFromPlc()
{
    if (!masterReady()) {
        return;
    }
    // 仅首次从 PLC 拉取，避免拖动滑条时被周期读回覆盖。
    if (!gVolumeSeeded) {
        const float v = std::clamp(readPlcFloat("volume", gVolume), 0.0f, 1.0f);
        gVolume = v;
        gLastVolumeWrote = v;
        gVolumeSeeded = true;
    }
}

void applyVolumeToPlc(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    gVolume = value;
    if (!masterReady()) {
        return;
    }
    if (std::fabs(value - gLastVolumeWrote) < 0.005f) {
        return;
    }
    if (writePlcFloat("volume", value)) {
        gLastVolumeWrote = value;
    }
}

void syncToolsFromPlc()
{
    if (!masterReady()) {
        return;
    }
    // 触摸屏主循环勿每帧狂刷 PLC，约 10Hz 足够刷新类型/寿命。
    const double now = ImGui::GetTime();
    if (gLastToolSync >= 0.0 && (now - gLastToolSync) < 0.1) {
        return;
    }
    gLastToolSync = now;

    for (int i = 0; i < 3; ++i) {
        gTools[i].toolPresent = readPlcBool(kToolNames[i], gTools[i].toolPresent);
        gTools[i].isolatorPresent = readPlcBool(kIsoNames[i], gTools[i].isolatorPresent);
        gTools[i].toolType = normalizeToolType(
            readPlcU16(kToolTypeNames[i], static_cast<uint16_t>(gTools[i].toolType)));
        gTools[i].toolLife = readPlcU32(kToolLifeNames[i], gTools[i].toolLife);
        gTools[i].toolVersion = readPlcU32(kToolVersionNames[i], gTools[i].toolVersion);
        if (!gTools[i].toolPresent) {
            gTools[i].toolType = E_ToolType::None;
            gTools[i].toolLife = 0;
            gTools[i].toolVersion = 0;
        }
        enforceToolRules(gTools[i]);
    }
    syncVolumeFromPlc();
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
    if (!present) {
        gTools[index].toolType = E_ToolType::None;
        gTools[index].toolLife = 0;
        gTools[index].toolVersion = 0;
    }
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
    // Error / Disconnected：清掉上次失败会话后再连；NetId 由 AdsLib GetRemoteAddress 解析，不猜 IP.1.1。
    if (state == tcGUICore::ConnectionState::Error) {
        plc->disconnect();
    }
    const auto port = static_cast<uint16_t>(gMasterEp.port > 0 && gMasterEp.port < 65536 ? gMasterEp.port : 851);
    app->ads().applyConnectionSettings(kMasterId, gMasterEp.ip, port, {});
    plc->connectAsync();
}

void pollMasterLink()
{
    // 任意阶段（开机 / 登录 / 主页）未连上 Control Core 时，每 5 秒自动重试。
    if (masterReady() || connecting(kMasterId)) {
        return;
    }
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

void resetLoginCredentials()
{
    std::snprintf(gLogin.user, sizeof(gLogin.user), "admin");
    std::snprintf(gLogin.password, sizeof(gLogin.password), "admin");
}

void abortGhostLoginInput()
{
    gOsk = OskTarget::None;
    gOskSlide = 0.0f;
    gOskKeysArmed = false;
    gOskArmAt = -1.0;
    gLastOskKeyAt = -1.0;
    gOskBurstKeys = 0;
    gOskShift = false;
    gOskDigits = false;
    gLoginEditUnlocked = false;
    resetLoginCredentials();
    gLoginWaitPointerUp = true;
    gLoginReadyAt = ImGui::GetTime() + 0.6;
}

void suppressLoginPointerClicks(ImGuiIO& io)
{
    // 同时清掉新版 KeysData 与旧 Mouse* 状态，否则 IsMouseClicked 仍可能为真。
    for (ImGuiKey key = ImGuiKey_MouseLeft; key <= ImGuiKey_MouseWheelY; key = static_cast<ImGuiKey>(key + 1)) {
        ImGuiKeyData* kd = &io.KeysData[key - ImGuiKey_NamedKey_BEGIN];
        kd->Down = false;
        kd->DownDuration = -1.0f;
        kd->DownDurationPrev = -1.0f;
    }
    for (int i = 0; i < IM_COUNTOF(io.MouseDown); ++i) {
        io.MouseDown[i] = false;
        io.MouseClicked[i] = false;
        io.MouseDoubleClicked[i] = false;
        io.MouseReleased[i] = false;
        io.MouseDownDuration[i] = -1.0f;
        io.MouseDownDurationPrev[i] = -1.0f;
    }
}

void enterLoginPhase()
{
    gPhase = Phase::Login;
    gOsk = OskTarget::None;
    gOskSlide = 0.0f;
    gOskKeysArmed = false;
    gOskArmAt = -1.0;
    gLastOskKeyAt = -1.0;
    gOskBurstKeys = 0;
    gOskShift = false;
    gOskDigits = false;
    gLoginEditUnlocked = false;
    resetLoginCredentials();
    gLogin.failed = false;
    // 开机/USB 复位后常有残留触摸与 HDMI-CEC/HID 误报按键，先隔离一段时间。
    gLoginReadyAt = ImGui::GetTime() + 1.2;
    gLoginWaitPointerUp = true;
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

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto& pal = tcGUICore::dash::palette();

    dl->AddRectFilled(origin, origin + avail, pack(pal.window));

    // 简洁开机页：居中品牌 + 一行说明 + 进度条，避免环形装饰压住文字。
    const float brandSize = px(36.0f);
    const float lineSize = px(20.0f);
    const float barW = std::min(px(420.0f), avail.x * 0.55f);
    const float blockH = brandSize + px(10.0f) + lineSize + px(28.0f) + px(10.0f);
    const float top = origin.y + (avail.y - blockH) * 0.42f;

    ImGui::SetCursorScreenPos(ImVec2(origin.x, top));
    {
        const char* brand = "Surgeon Console";
        ImGui::PushFont(ImGui::GetFont(), brandSize);
        const ImVec2 sz = ImGui::CalcTextSize(brand);
        ImGui::SetCursorPosX((avail.x - sz.x) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, pal.text);
        ImGui::TextUnformatted(brand);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(0.0f, px(10.0f)));
    {
        const char* line = tcGUICore::tr("灵窥手术机器人 · 医生控制台", "Lingkui Surgical Robot · Surgeon console");
        ImGui::PushFont(ImGui::GetFont(), lineSize);
        const ImVec2 sz = ImGui::CalcTextSize(line);
        ImGui::SetCursorPosX(std::max(0.0f, (avail.x - sz.x) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, pal.muted);
        ImGui::TextUnformatted(line);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(0.0f, px(28.0f)));
    ImGui::SetCursorPosX((avail.x - barW) * 0.5f);
    ImGui::BeginChild("##boot_bar", ImVec2(barW, px(16.0f)), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    tcGUICore::dash::progress(progress);
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(avail);

    if (done) {
        enterLoginPhase();
    }
}

float px(float v)
{
    return v * tcGUICore::uiScale();
}

struct OskMetrics {
    float width = 0.0f;
    float height = 0.0f;
    float keyH = 0.0f;
    float gap = 0.0f;
    float pad = 0.0f;
};

OskMetrics makeOskMetrics(const ImVec2& avail)
{
    OskMetrics m;
    m.gap = px(8.0f);
    m.pad = px(12.0f);
    m.keyH = std::clamp(avail.y * 0.088f, px(64.0f), px(86.0f));
    m.height = m.pad * 2.0f + m.keyH * 4.0f + m.gap * 3.0f + 2.0f;
    m.width = std::min(std::max(px(640.0f), avail.x * 0.88f), std::max(px(320.0f), avail.x - px(20.0f)));
    m.width = std::min(std::max(px(640.0f), avail.x * 0.88f), std::max(px(320.0f), avail.x - px(20.0f)));
    return m;
}

void oskAppend(char* buf, std::size_t cap, char ch)
{
    if (!buf || cap < 2) {
        return;
    }
    const std::size_t n = std::strlen(buf);
    if (n + 1 >= cap) {
        return;
    }
    buf[n] = ch;
    buf[n + 1] = '\0';
}

void oskBackspace(char* buf)
{
    if (!buf) {
        return;
    }
    const std::size_t n = std::strlen(buf);
    if (n > 0) {
        buf[n - 1] = '\0';
    }
}

// 触摸屏屏幕键盘：四行正好铺满，无滚动条，自底部滑入。
void drawOnscreenKeyboard(const ImVec2& origin, const ImVec2& avail, char* buf, std::size_t cap, float slide)
{
    const auto& pal = tcGUICore::dash::palette();
    const OskMetrics m = makeOskMetrics(avail);
    const float kGap = m.gap;
    const float y = origin.y + avail.y - m.height * slide;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + (avail.x - m.width) * 0.5f, y));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.13f, 0.15f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, pal.cardBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m.pad, m.pad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kGap, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
    ImGui::BeginChild("##osk", ImVec2(m.width, m.height), ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);

    const float inner = ImGui::GetContentRegionAvail().x;
    const float unit = (inner - m.gap * 9.0f) / 10.0f;
    const float keyH = m.keyH;

    auto keyBtn = [&](const char* id, const char* label, float w, bool primary = false) -> bool {
        ImGui::PushID(id);
        // 键盘未完全展开、或手指仍按着滑动经过时，不接受按键，防止幻触乱输入。
        if (!gOskKeysArmed || !gLoginEditUnlocked) {
            ImGui::BeginDisabled(true);
            tcGUICore::dash::button(label, ImVec2(w, keyH), primary);
            ImGui::EndDisabled();
            ImGui::PopID();
            return false;
        }
        const bool hit = tcGUICore::dash::button(label, ImVec2(w, keyH), primary);
        ImGui::PopID();
        if (!hit) {
            return false;
        }
        // 只要“干净抬起”：过短=噪声，过长=粘滞/重复，都丢弃。
        const float held = ImGui::GetIO().MouseDownDurationPrev[0];
        if (held < 0.05f || held > 0.85f) {
            return false;
        }
        const double now = ImGui::GetTime();
        if (gLastOskKeyAt >= 0.0 && (now - gLastOskKeyAt) < 0.22) {
            return false;
        }
        // 武装后 0.55s 内连续 ≥3 次按键，视为幻触连打（如 aaaaaaa）。
        if (gOskArmAt >= 0.0 && (now - gOskArmAt) < 0.55) {
            ++gOskBurstKeys;
            if (gOskBurstKeys >= 3) {
                abortGhostLoginInput();
                return false;
            }
        } else {
            gOskBurstKeys = 0;
        }
        gLastOskKeyAt = now;
        return true;
    };

    auto letterKey = [&](char base, bool shifted) {
        char ch = base;
        if (shifted && ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
        char id[4] = {base, 0, 0, 0};
        char lab[4] = {ch, 0, 0, 0};
        if (keyBtn(id, lab, unit) && buf) {
            oskAppend(buf, cap, ch);
        }
    };

    auto typeUnits = [&](const char* keys, float indent) {
        if (indent > 0.5f) {
            ImGui::Dummy(ImVec2(indent, keyH));
            ImGui::SameLine(0.0f, 0.0f);
        }
        const int n = static_cast<int>(std::strlen(keys));
        for (int i = 0; i < n; ++i) {
            if (i > 0) {
                ImGui::SameLine(0.0f, kGap);
            }
            letterKey(keys[i], !gOskDigits && gOskShift);
        }
    };

    if (gOskDigits) {
        typeUnits("1234567890", 0.0f);
        ImGui::Dummy(ImVec2(0.0f, kGap));
        typeUnits("-_@.#/=+", unit * 1.0f + kGap * 0.5f);
        ImGui::Dummy(ImVec2(0.0f, kGap));
        ImGui::Dummy(ImVec2(unit * 1.5f + kGap * 0.5f, keyH));
        ImGui::SameLine(0.0f, 0.0f);
        typeUnits("!?*", 0.0f);
    } else {
        typeUnits("qwertyuiop", 0.0f);
        ImGui::Dummy(ImVec2(0.0f, kGap));
        typeUnits("asdfghjkl", unit * 0.5f + kGap * 0.5f);
        ImGui::Dummy(ImVec2(0.0f, kGap));
        if (keyBtn("shift", gOskShift ? "SHIFT" : "Shift", unit * 1.5f + kGap * 0.5f, gOskShift)) {
            gOskShift = !gOskShift;
        }
        ImGui::SameLine(0.0f, kGap);
        typeUnits("zxcvbnm", 0.0f);
        ImGui::SameLine(0.0f, kGap);
        if (keyBtn("bs", tcGUICore::tr("删除", "Del"), unit * 1.5f + kGap * 0.5f) && buf) {
            oskBackspace(buf);
        }
    }

    ImGui::Dummy(ImVec2(0.0f, kGap));
    const float hideW = std::max(unit * 2.0f + kGap * 0.5f,
        ImGui::CalcTextSize(tcGUICore::tr("收起", "Hide")).x + px(28.0f));
    const float leftW = unit * 1.5f + kGap * 0.5f;
    if (gOskDigits) {
        const float spaceW = std::max(unit * 2.5f, inner - leftW * 2.0f - hideW - kGap * 3.0f);
        if (keyBtn("abc", "ABC", leftW)) {
            gOskDigits = false;
        }
        ImGui::SameLine(0.0f, kGap);
        if (keyBtn("bs2", tcGUICore::tr("删除", "Del"), leftW) && buf) {
            oskBackspace(buf);
        }
        ImGui::SameLine(0.0f, kGap);
        if (keyBtn("space", tcGUICore::tr("空格", "Space"), spaceW) && buf) {
            oskAppend(buf, cap, ' ');
        }
        ImGui::SameLine(0.0f, kGap);
        if (keyBtn("hide", tcGUICore::tr("收起", "Hide"), hideW)) {
            gOsk = OskTarget::None;
        }
    } else {
        const float spaceW = std::max(unit * 3.0f, inner - leftW - hideW - kGap * 2.0f);
        if (keyBtn("123", "123", leftW)) {
            gOskDigits = true;
            gOskShift = false;
        }
        ImGui::SameLine(0.0f, kGap);
        if (keyBtn("space", tcGUICore::tr("空格", "Space"), spaceW) && buf) {
            oskAppend(buf, cap, ' ');
        }
        ImGui::SameLine(0.0f, kGap);
        if (keyBtn("hide", tcGUICore::tr("收起", "Hide"), hideW)) {
            gOsk = OskTarget::None;
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
}

void drawLoginField(const char* id, const char* label, char* buf, bool password, bool active)
{
    const auto& pal = tcGUICore::dash::palette();
    tcGUICore::dash::caption(label);
    const float h = px(52.0f);
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.12f, 0.26f, 0.48f, 1.0f) : pal.field);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.28f, 0.50f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.22f, 0.42f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(10.0f));
    const bool hit = ImGui::Button("##tap", ImVec2(w, h));
    const ImVec2 r0 = ImGui::GetItemRectMin();
    const ImVec2 r1 = ImGui::GetItemRectSize();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    char shown[72];
    ImU32 textCol = pack(pal.text);
    if (password) {
        const std::size_t n = std::strlen(buf);
        const std::size_t m = std::min(n, sizeof(shown) - 1);
        for (std::size_t i = 0; i < m; ++i) {
            shown[i] = '*';
        }
        shown[m] = '\0';
        if (m == 0) {
            std::snprintf(shown, sizeof(shown), "%s", tcGUICore::tr("点击输入密码", "Tap to enter password"));
            textCol = pack(pal.muted);
        }
    } else if (buf[0] == '\0') {
        std::snprintf(shown, sizeof(shown), "%s", tcGUICore::tr("点击输入用户", "Tap to enter user"));
        textCol = pack(pal.muted);
    } else {
        std::snprintf(shown, sizeof(shown), "%s", buf);
    }
    ImGui::PushFont(ImGui::GetFont(), px(22.0f));
    const ImVec2 ts = ImGui::CalcTextSize(shown);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(r0.x + px(14.0f), r0.y + (r1.y - ts.y) * 0.5f), textCol, shown);
    ImGui::PopFont();
    ImGui::PopID();

    // 必须按住再抬起足够久，才打开键盘；滤掉开机残留的瞬时点击。
    if (hit) {
        const float held = ImGui::GetIO().MouseDownDurationPrev[0];
        if (held >= 0.12f) {
            gLoginEditUnlocked = true;
            gOsk = password ? OskTarget::Password : OskTarget::User;
            gOskShift = false;
            gOskDigits = false;
            gOskKeysArmed = false;
            gOskArmAt = -1.0;
            gLastOskKeyAt = -1.0;
            gOskBurstKeys = 0;
        }
    }
}

void drawLogin()
{
    pollMasterLink();
    const bool plcOn = masterReady();
    const bool busy = connecting(kMasterId);
    tcGUICore::PlcClient* plc = clientOf(kMasterId);
    const auto& pal = tcGUICore::dash::palette();
    ImGuiIO& io = ImGui::GetIO();

    // 登录页只认屏幕键盘：清掉 USB HID / HDMI-CEC 等物理键盘误报。
    io.ClearInputKeys();
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetStyle().TouchExtraPadding = ImVec2(0.0f, 0.0f);

    // 未通过“按住输入框”解锁前，每帧写回默认账号，杜绝 aaaaaaa 一类幻触残留。
    if (!gLoginEditUnlocked) {
        resetLoginCredentials();
    }

    const double now = ImGui::GetTime();
    const bool timeReady = gLoginReadyAt < 0.0 || now >= gLoginReadyAt;
    const bool pointerDown = io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2];
    if (gLoginWaitPointerUp) {
        // 等待期间整段吞掉点击；抬起的那一帧也吞掉，避免残留按下被当成“点输入框”。
        suppressLoginPointerClicks(io);
        if (timeReady && !pointerDown) {
            gLoginWaitPointerUp = false;
        }
    }
    const bool inputReady = timeReady && !gLoginWaitPointerUp;

    // 地址不在登录页编辑：默认 / JSON 为 172.13.158.64:851。
    if (plc) {
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

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, origin + avail, pack(pal.window));

    const OskMetrics osk = makeOskMetrics(avail);
    const float targetSlide = gOsk != OskTarget::None ? 1.0f : 0.0f;
    const float dt = std::max(0.0f, ImGui::GetIO().DeltaTime);
    const float ease = 1.0f - std::exp(-12.0f * dt);
    gOskSlide += (targetSlide - gOskSlide) * ease;
    if (std::fabs(gOskSlide - targetSlide) < 0.002f) {
        gOskSlide = targetSlide;
    }
    if (gOsk == OskTarget::None || gOskSlide < 0.98f) {
        gOskKeysArmed = false;
        gOskArmAt = -1.0;
    } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        // 手指仍按着（含打开输入框的那次按下）时不武装，并重新计时。
        gOskKeysArmed = false;
        gOskArmAt = -1.0;
    } else {
        if (gOskArmAt < 0.0) {
            gOskArmAt = now + 0.35;
            gOskBurstKeys = 0;
        }
        gOskKeysArmed = now >= gOskArmAt;
    }

    const float kCardW = px(480.0f);
    static ImVec2 cardSize(480.0f, 420.0f);
    static OskTarget lastOsk = OskTarget::Password;
    const float kbShown = osk.height * gOskSlide;
    const float padX = std::max(0.0f, (avail.x - cardSize.x) * 0.5f);
    const float roomY = std::max(0.0f, avail.y - kbShown);
    const float padY = std::max(px(8.0f), (roomY - cardSize.y) * 0.5f);
    ImGui::SetCursorScreenPos(origin + ImVec2(padX, padY));

    if (!inputReady) {
        ImGui::BeginDisabled(true);
    }

    tcGUICore::dash::beginCard("##login", ImVec2(kCardW, 0.0f));
    tcGUICore::dash::title("Surgeon Console", px(36.0f));
    tcGUICore::dash::hint(tcGUICore::tr("医生控制台登录", "Surgeon console sign-in"));
    ImGui::Dummy(ImVec2(0.0f, px(6.0f)));

    if (plcOn) {
        tcGUICore::dash::badge("Control Core", tcGUICore::dash::Accent::Green);
    } else if (busy) {
        tcGUICore::dash::badge(tcGUICore::tr("连接中", "Connecting"), tcGUICore::dash::Accent::Amber);
    } else {
        tcGUICore::dash::badge(tcGUICore::tr("等待 Control Core", "Waiting Control Core"), tcGUICore::dash::Accent::Amber);
    }
    ImGui::Spacing();
    tcGUICore::dash::hint(plcOn
            ? tcGUICore::tr("Control Core 已连接，可以登录。", "Control Core linked. You may sign in.")
            : tcGUICore::tr("未连接也可登录；进入后功能栏锁定，并持续尝试连接 Control Core。",
                "You can sign in now. The workspace stays locked until Control Core links."));
    ImGui::Spacing();

    drawLoginField("##user_tap", tcGUICore::tr("用户", "User"), gLogin.user, false, gOsk == OskTarget::User);
    drawLoginField("##pass_tap", tcGUICore::tr("密码", "Password"), gLogin.password, true, gOsk == OskTarget::Password);
    ImGui::Spacing();

    const bool clicked = tcGUICore::dash::button(tcGUICore::tr("登录", "Sign in"), ImVec2(-1.0f, px(52.0f)), true);
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
    cardSize = ImGui::GetItemRectSize();

    if (!inputReady) {
        ImGui::EndDisabled();
    }

    if (gOsk != OskTarget::None) {
        lastOsk = gOsk;
    }
    if (gOskSlide > 0.002f) {
        char* buf = lastOsk == OskTarget::User ? gLogin.user : gLogin.password;
        const std::size_t cap = lastOsk == OskTarget::User ? sizeof(gLogin.user) : sizeof(gLogin.password);
        drawOnscreenKeyboard(origin, avail, buf, cap, gOskSlide);
    }

    auto submit = [&] {
        gLogin.failed = std::strcmp(gLogin.user, "admin") != 0 || std::strcmp(gLogin.password, "admin") != 0;
        gAuthed = !gLogin.failed;
        if (gAuthed) {
            gOsk = OskTarget::None;
            gOskSlide = 0.0f;
            gOskKeysArmed = false;
            gPhase = Phase::Home;
            gHomePage = HomePage::Instruments;
        }
    };

    if (inputReady && clicked) {
        submit();
    }
}

// 简约安装行：左侧状态字 + 右侧按键；点击且允许时返回 true。
bool drawStatusRow(const char* toggleId, const char* label, bool on, bool actionEnabled)
{
    const float rowStartX = ImGui::GetCursorPosX();
    const float availW = ImGui::GetContentRegionAvail().x;
    const float btnW = px(88.0f);
    const float btnH = px(36.0f);
    const float rowY = ImGui::GetCursorPosY();
    const auto& pal = tcGUICore::dash::palette();

    ImGui::PushFont(ImGui::GetFont(), px(15.0f));
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, on ? pal.text : pal.muted);
    ImGui::Text("%s  %s", label, on ? tcGUICore::tr("已装", "On") : tcGUICore::tr("未装", "Off"));
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::SameLine();
    ImGui::SetCursorPosX(rowStartX + std::max(0.0f, availW - btnW));
    ImGui::SetCursorPosY(rowY);
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

    // 目标高度：未装=0，仅快换板=1/6，器械安装=满槽。
    const float targetFill = slot.toolPresent ? 1.0f : (slot.isolatorPresent ? (1.0f / 6.0f) : 0.0f);
    const float dt = ImGui::GetIO().DeltaTime;
    const float ease = 1.0f - std::exp(-10.0f * std::max(dt, 0.0f));
    gBayFillAnim[bayIndex] += (targetFill - gBayFillAnim[bayIndex]) * ease;
    if (std::fabs(gBayFillAnim[bayIndex] - targetFill) < 0.001f) {
        gBayFillAnim[bayIndex] = targetFill;
    }
    const float fill = gBayFillAnim[bayIndex];

    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.card);
    ImGui::PushStyleColor(ImGuiCol_Border, pal.cardBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12.0f), px(10.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(6.0f), px(2.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 win = ImGui::GetWindowPos();
    const ImVec2 winSize = ImGui::GetWindowSize();
    const float round = px(12.0f);

    if (fill > 0.001f) {
        const float blockH = winSize.y * fill;
        const ImVec2 p0(win.x, win.y + winSize.y - blockH);
        const ImVec2 p1(win.x + winSize.x, win.y + winSize.y);
        ImVec4 blockCol = accentVec;
        blockCol.w = slot.toolPresent || fill > 0.9f ? 0.42f : 0.36f;
        const float r = std::min(round, blockH * 0.5f);
        dl->AddRectFilled(p0, p1, pack(blockCol), r);
    }

    ImGui::PushFont(ImGui::GetFont(), px(13.0f));
    tcGUICore::dash::caption(role);
    ImGui::PopFont();

    // 无器械：类型 None，寿命/版本空白；有器械：显示 PLC 同步值。突出类型与寿命。
    const E_ToolType shownType = slot.toolPresent ? slot.toolType : E_ToolType::None;
    const std::string typeText = tcGUICore::displayNameText(toolTypeLabel(shownType));
    char lifeText[32] = "—";
    char versionText[32] = "—";
    if (slot.toolPresent) {
        std::snprintf(lifeText, sizeof(lifeText), "%u", slot.toolLife);
        std::snprintf(versionText, sizeof(versionText), "%u", slot.toolVersion);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, pal.muted);
    ImGui::PushFont(ImGui::GetFont(), px(13.0f));
    ImGui::TextUnformatted(tcGUICore::tr("类型", "Type"));
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, pal.text);
    ImGui::PushFont(ImGui::GetFont(), px(26.0f));
    ImGui::TextUnformatted(typeText.c_str());
    ImGui::PopFont();
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0.0f, px(2.0f)));
    {
        const float rowX = ImGui::GetCursorPosX();
        ImGui::PushStyleColor(ImGuiCol_Text, pal.muted);
        ImGui::PushFont(ImGui::GetFont(), px(13.0f));
        ImGui::TextUnformatted(tcGUICore::tr("寿命", "Life"));
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::SameLine(rowX + px(48.0f), 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, pal.text);
        ImGui::PushFont(ImGui::GetFont(), px(24.0f));
        ImGui::TextUnformatted(lifeText);
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }
    {
        const float rowX = ImGui::GetCursorPosX();
        ImGui::PushStyleColor(ImGuiCol_Text, pal.muted);
        ImGui::PushFont(ImGui::GetFont(), px(12.0f));
        ImGui::TextUnformatted(tcGUICore::tr("版本", "Ver"));
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::SameLine(rowX + px(48.0f), 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, pal.muted);
        ImGui::PushFont(ImGui::GetFont(), px(14.0f));
        ImGui::TextUnformatted(versionText);
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0.0f, px(8.0f)));
    char toolToggleId[32];
    char isoToggleId[32];
    std::snprintf(toolToggleId, sizeof(toolToggleId), "tool_tog_%d", bayIndex);
    std::snprintf(isoToggleId, sizeof(isoToggleId), "iso_tog_%d", bayIndex);

    const bool canInstallTool = slot.isolatorPresent;
    const bool canRemoveIsolator = !slot.toolPresent;
    const bool toolActionOk = slot.toolPresent ? true : canInstallTool;
    const bool isoActionOk = slot.isolatorPresent ? canRemoveIsolator : true;

    if (drawStatusRow(toolToggleId, tcGUICore::tr("器械", "Tool"), slot.toolPresent, toolActionOk)) {
        applyToolPresent(bayIndex, !slot.toolPresent);
    }
    // 加大两行按键间距，降低误触。
    ImGui::Dummy(ImVec2(0.0f, px(12.0f)));
    if (drawStatusRow(isoToggleId, tcGUICore::tr("快换板", "Plate"), slot.isolatorPresent, isoActionOk)) {
        applyIsolatorPresent(bayIndex, !slot.isolatorPresent);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(4);
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

    const ImVec2 panel(px(440.0f), px(220.0f));
    ImGui::SetCursorPos(ImVec2((size.x - panel.x) * 0.5f, (size.y - panel.y) * 0.5f));
    tcGUICore::dash::beginCard("##ms_lock_card", panel);
    tcGUICore::dash::caption(tcGUICore::tr("主从状态", "Master-Slave"));
    tcGUICore::dash::title(tcGUICore::tr("屏幕已锁定", "Screen locked"), px(28.0f));
    tcGUICore::dash::hint(tcGUICore::tr("确认安全可解锁", "Confirm safe to unlock"));
    ImGui::Spacing();
    if (tcGUICore::dash::button(tcGUICore::tr("解锁", "Unlock"), ImVec2(-1.0f, px(52.0f)), true)) {
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
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(10.0f), px(6.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
    ImGui::BeginChild("##volume_strip", size, ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const float rowH = ImGui::GetContentRegionAvail().y;
    const float btn = std::min(px(36.0f), rowH);
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
        const float s = btn / 40.0f;
        dl->AddRectFilled(btnPos, btnPos + ImVec2(btn, btn), bg, px(10.0f));
        dl->AddRectFilled(c + ImVec2(-12.0f * s, -5.0f * s), c + ImVec2(-4.0f * s, 5.0f * s), col, 2.0f);
        dl->AddTriangleFilled(c + ImVec2(-6.0f * s, -8.0f * s), c + ImVec2(-6.0f * s, 8.0f * s),
            c + ImVec2(6.0f * s, 0.0f), col);
        dl->PathClear();
        dl->PathArcTo(c + ImVec2(4.0f * s, 0.0f), 9.0f * s, -0.85f, 0.85f, 12);
        dl->PathStroke(col, 0, 2.0f);
        if (gVolumeOpen) {
            dl->PathClear();
            dl->PathArcTo(c + ImVec2(4.0f * s, 0.0f), 14.0f * s, -0.85f, 0.85f, 12);
            dl->PathStroke(col, 0, 1.6f);
        }
    }

    ImGui::SameLine(0.0f, px(10.0f));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (btn - ImGui::GetFrameHeight()) * 0.5f);

    if (gVolumeOpen) {
        const float barW = std::max(px(160.0f), ImGui::GetContentRegionAvail().x - px(72.0f));
        ImGui::SetNextItemWidth(barW);
        if (tcGUICore::dash::slider("##volume", &gVolume, 0.0f, 1.0f) ||
            ImGui::IsItemDeactivatedAfterEdit()) {
            applyVolumeToPlc(gVolume);
        }
        ImGui::SameLine(0.0f, px(8.0f));
        ImGui::AlignTextToFramePadding();
        char level[32];
        std::snprintf(level, sizeof(level), "%d%%", static_cast<int>(gVolume * 100.0f + 0.5f));
        ImGui::TextUnformatted(level);
    } else {
        ImGui::AlignTextToFramePadding();
        tcGUICore::dash::caption(tcGUICore::tr("点击调节音量", "Tap to adjust volume"));
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void drawInstrumentsPage()
{
    syncToolsFromPlc();

    const auto& pal = tcGUICore::dash::palette();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(origin, origin + avail, pack(pal.window), 12.0f);

    const float gap = px(10.0f);
    const float bottomH = px(48.0f);
    const float cardH = std::max(px(200.0f), avail.y - bottomH - gap);
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
    drawVolumeStrip(ImVec2(avail.x, bottomH));
}

void drawBlankPage()
{
    const auto& pal = tcGUICore::dash::palette();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(origin, origin + avail, pack(pal.window), 12.0f);
    ImGui::Dummy(avail);
}

void drawPlcPage()
{
    drawBlankPage();
}

void drawStatusPage()
{
    drawBlankPage();
}

void drawRail()
{
    const float icon = px(50.0f);
    const float railW = icon + px(32.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.075f, 0.085f, 1.0f));
    ImGui::BeginChild("##rail", ImVec2(railW, 0.0f));
    ImGui::Dummy(ImVec2(0.0f, px(10.0f)));
    const float x = (railW - icon) * 0.5f;
    ImGui::SetCursorPosX(x);
    if (tcGUICore::dash::iconButton("##nav_tools", tcGUICore::dash::iconTiles, gHomePage == HomePage::Instruments)) {
        gHomePage = HomePage::Instruments;
    }
    ImGui::SetCursorPosX(x);
    if (tcGUICore::dash::iconButton("##nav_plc", tcGUICore::dash::iconPlug, gHomePage == HomePage::Plc)) {
        gHomePage = HomePage::Plc;
    }
    ImGui::SetCursorPosX(x);
    if (tcGUICore::dash::iconButton("##nav_st", tcGUICore::dash::iconPulse, gHomePage == HomePage::Status)) {
        gHomePage = HomePage::Status;
    }
    const float gap = px(10.0f);
    const float yQuit = ImGui::GetWindowHeight() - px(16.0f) - icon;
    const float yLogout = yQuit - gap - icon;
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY() + px(8.0f), yLogout));
    ImGui::SetCursorPosX(x);
    if (tcGUICore::dash::iconButton("##logout", tcGUICore::dash::iconPower, false)) {
        gAuthed = false;
        enterLoginPhase();
    }
    ImGui::SetCursorPosY(yQuit);
    ImGui::SetCursorPosX(x);
    if (tcGUICore::dash::iconButton("##quit", tcGUICore::dash::iconExit, false)) {
        if (tcGUICore::Application* app = tcGUICore::Application::instance()) {
            app->quit();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void drawHeader()
{
    const bool plcOn = masterReady();
    const float rowH = px(42.0f);

    // 左：主标题
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    tcGUICore::dash::title("Main Page", px(30.0f));
    const char* sub = tcGUICore::tr("器械舱", "Instrument bay");
    if (gHomePage == HomePage::Plc) {
        sub = tcGUICore::tr("Control Core 连接", "Control Core link");
    } else if (gHomePage == HomePage::Status) {
        sub = tcGUICore::tr("系统状态", "System status");
    }
    ImGui::PushFont(ImGui::GetFont(), px(18.0f));
    tcGUICore::dash::hint(sub);
    ImGui::PopFont();
    ImGui::EndGroup();

    // 右：灯 / Control Core / 语言 —— 同一行垂直居中对齐
    const char* ctrl = "Control Core";
    const char* langLabel = tcGUICore::currentLanguage() == tcGUICore::Language::Zh ? "中文" : "EN";
    const float langW = px(96.0f);
    const float ledW = px(22.0f);
    const float gap = px(14.0f);
    const float inset = px(24.0f);
    const float rightW = ledW + gap + ImGui::CalcTextSize(ctrl).x + gap + langW;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(px(8.0f), ImGui::GetWindowWidth() - rightW - inset));
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
    pollMasterLink();
    const bool opsEnabled = masterReady();
    if (opsEnabled) {
        syncToolsFromPlc();
    }
    const float footer = ImGui::GetFrameHeightWithSpacing();
    ImVec2 body = ImGui::GetContentRegionAvail();
    body.y -= footer;
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
    ImGui::BeginChild("##console", body, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawRail();
    ImGui::SameLine();
    ImGui::BeginChild("##pages", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawHeader();
    ImGui::Spacing();
    ImGui::BeginDisabled(!opsEnabled);
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
    ImGui::EndDisabled();
    ImGui::EndChild();
    if (opsEnabled) {
        drawMsLockOverlay();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
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
