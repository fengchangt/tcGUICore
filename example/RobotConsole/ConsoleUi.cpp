#include "ConsoleUi.h"

#include "AdsManager/PlcClient.h"
#include "Common/I18n.h"
#include "Viewer/Theme.h"
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
enum class OskTarget { None, User, Password };

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
OskTarget gOsk = OskTarget::None;
bool gOskShift = false;
bool gOskDigits = false;
float gOskSlide = 0.0f;
Endpoint gMasterEp{"172.13.158.64", 851};
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
        const bool hit = tcGUICore::dash::button(label, ImVec2(w, keyH), primary);
        ImGui::PopID();
        return hit;
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

void drawLogin()
{
    pollMasterLink();
    const bool plcOn = masterReady();
    const bool busy = connecting(kMasterId);
    tcGUICore::PlcClient* plc = clientOf(kMasterId);
    const auto& pal = tcGUICore::dash::palette();

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

    const float kCardW = px(480.0f);
    static ImVec2 cardSize(480.0f, 420.0f);
    static OskTarget lastOsk = OskTarget::Password;
    const float kbShown = osk.height * gOskSlide;
    const float padX = std::max(0.0f, (avail.x - cardSize.x) * 0.5f);
    const float roomY = std::max(0.0f, avail.y - kbShown);
    const float padY = std::max(px(8.0f), (roomY - cardSize.y) * 0.5f);
    ImGui::SetCursorScreenPos(origin + ImVec2(padX, padY));

    tcGUICore::dash::beginCard("##login", ImVec2(kCardW, 0.0f));
    tcGUICore::dash::title("Surgeon Console", px(32.0f));
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
            : tcGUICore::tr("未连接也可登录；进入后功能栏锁定，并持续尝试连接 Control Core。",
                "You can sign in now. The workspace stays locked until Control Core links."));
    ImGui::Spacing();

    tcGUICore::dash::caption(tcGUICore::tr("用户", "User"));
    ImGui::SetNextItemWidth(-1.0f);
    tcGUICore::dash::textField("##user", gLogin.user, sizeof(gLogin.user));
    if (ImGui::IsItemActivated()) {
        gOsk = OskTarget::User;
        gOskShift = false;
        gOskDigits = false;
    }
    tcGUICore::dash::caption(tcGUICore::tr("密码", "Password"));
    ImGui::SetNextItemWidth(-1.0f);
    const bool enter = tcGUICore::dash::textField("##pass", gLogin.password, sizeof(gLogin.password),
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemActivated()) {
        gOsk = OskTarget::Password;
        gOskShift = false;
        gOskDigits = false;
    }
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
            gPhase = Phase::Home;
            gHomePage = HomePage::Instruments;
        }
    };

    if (clicked || enter) {
        submit();
    }
}

float gVolume = 0.55f;
bool gVolumeOpen = false;

// 状态行 + 安装/拆卸按键；点击且允许时返回 true。
bool drawStatusRow(const char* toggleId, const char* label, bool on,
    const char* onText, const char* offText, bool actionEnabled)
{
    const float rowStartX = ImGui::GetCursorPosX();
    const float availW = ImGui::GetContentRegionAvail().x;
    const float btnW = px(96.0f);
    const float btnH = px(40.0f);

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
            tcGUICore::dash::title(type.c_str(), px(32.0f));
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
    drawVolumeStrip(ImVec2(avail.x, std::max(px(72.0f), bottomH - gap)));
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
    const float icon = px(44.0f);
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
        gPhase = Phase::Login;
        gLogin.password[0] = '\0';
        gLogin.failed = false;
        gOsk = OskTarget::None;
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
    const float rowH = px(36.0f);

    // 左：主标题
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    tcGUICore::dash::title("Main Page", px(24.0f));
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
    const float langW = px(88.0f);
    const float ledW = px(20.0f);
    const float gap = px(12.0f);
    const float inset = px(28.0f);
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
    ImGui::BeginChild("##console", body);
    drawRail();
    ImGui::SameLine();
    ImGui::BeginChild("##pages", ImVec2(0.0f, 0.0f));
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
