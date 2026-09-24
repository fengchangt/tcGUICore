#include "ShellWindow.h"

#include "AdsManager/AdsScan.h"
#include "Common/I18n.h"
#include "Common/Log.h"
#include "Common/Platform.h"
#include "Theme.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "implot3d.h"

#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "imgui_impl_dx11.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tcGUICore {

namespace {

struct ShellChrome {
    GLFWwindow* window = nullptr;
    std::string title;
};

ShellChrome gChrome;
AdsHub* gHub = nullptr;
FrameCallback gProductFrame;

std::mutex gScanMu;
std::thread gScanner;
std::vector<AdsEndpoint> gFound;
std::atomic<bool> gScanning{false};
std::atomic<bool> gScanDone{false};

constexpr float kCornerRadius = 14.0f;

void requestIpScan()
{
    if (gScanning.load()) {
        return;
    }
    if (gScanner.joinable()) {
        gScanner.join();
    }
    gScanning = true;
    gScanner = std::thread([] {
        std::vector<AdsEndpoint> found = scanAdsIpsOnce(700);
        {
            std::lock_guard<std::mutex> lock(gScanMu);
            gFound = std::move(found);
        }
        gScanDone = true;
        gScanning = false;
    });
}

void mergeScannedIps(std::vector<std::string>& ips, std::vector<std::string>& netIds)
{
    if (!gScanDone.load()) {
        return;
    }
    std::vector<AdsEndpoint> found;
    {
        std::lock_guard<std::mutex> lock(gScanMu);
        found = std::move(gFound);
        gScanDone = false;
    }
    for (AdsEndpoint& item : found) {
        const std::string netId = item.amsNetId.empty() ? amsNetIdFromIp(item.ip) : item.amsNetId;
        const auto it = std::find(ips.begin(), ips.end(), item.ip);
        if (it == ips.end()) {
            ips.push_back(std::move(item.ip));
            netIds.push_back(netId);
            continue;
        }
        const auto index = static_cast<size_t>(std::distance(ips.begin(), it));
        if (index < netIds.size() && !item.amsNetId.empty()) {
            netIds[index] = item.amsNetId;
        }
    }
}

void joinIpScan()
{
    if (gScanner.joinable()) {
        gScanner.join();
    }
}

#ifdef _WIN32
void setCaptionBlack(GLFWwindow* window)
{
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) {
        return;
    }

    if (HMODULE theme = LoadLibraryW(L"uxtheme.dll")) {
        using SetPreferred = int(WINAPI*)(int);
        using AllowDark = BOOL(WINAPI*)(HWND, BOOL);
        using FlushThemes = void(WINAPI*)();
        if (auto setPreferred = reinterpret_cast<SetPreferred>(GetProcAddress(theme, MAKEINTRESOURCEA(135)))) {
            setPreferred(2);
        }
        if (auto allow = reinterpret_cast<AllowDark>(GetProcAddress(theme, MAKEINTRESOURCEA(133)))) {
            allow(hwnd, TRUE);
        }
        if (auto flush = reinterpret_cast<FlushThemes>(GetProcAddress(theme, MAKEINTRESOURCEA(136)))) {
            flush();
        }
        FreeLibrary(theme);
    }

    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        using SetAttr = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        if (auto setAttr = reinterpret_cast<SetAttr>(GetProcAddress(dwm, "DwmSetWindowAttribute"))) {
            BOOL dark = TRUE;
            setAttr(hwnd, 19, &dark, sizeof(dark));
            setAttr(hwnd, 20, &dark, sizeof(dark));
            const COLORREF black = RGB(0, 0, 0);
            setAttr(hwnd, 34, &black, sizeof(black));
            setAttr(hwnd, 35, &black, sizeof(black));
        }
        FreeLibrary(dwm);
    }

    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
}

void updateWindowCorners(GLFWwindow* window)
{
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) {
        return;
    }
    int width = 0;
    int height = 0;
    glfwGetWindowSize(window, &width, &height);
    const int maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);
    static int lastW = -1;
    static int lastH = -1;
    static int lastMax = -1;
    if (width == lastW && height == lastH && maximized == lastMax) {
        return;
    }
    lastW = width;
    lastH = height;
    lastMax = maximized;
    if (maximized || width < 2 || height < 2) {
        SetWindowRgn(hwnd, nullptr, TRUE);
        return;
    }
    const int ellipse = static_cast<int>(kCornerRadius * 2.0f);
    SetWindowRgn(hwnd, CreateRoundRectRgn(0, 0, width + 1, height + 1, ellipse, ellipse), TRUE);
}

int windowEdgeAtCursor();

void dragCaption()
{
    if (!gChrome.window) {
        return;
    }
    HWND hwnd = glfwGetWin32Window(gChrome.window);
    if (!hwnd) {
        return;
    }
    if (windowEdgeAtCursor()) {
        return;
    }
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        SendMessageW(hwnd, WM_NCLBUTTONDBLCLK, HTCAPTION, 0);
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ReleaseCapture();
        SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
}
#endif

int windowEdgeAtCursor()
{
    if (!gChrome.window || glfwGetWindowAttrib(gChrome.window, GLFW_MAXIMIZED)) {
        return 0;
    }
    int width = 0;
    int height = 0;
    glfwGetWindowSize(gChrome.window, &width, &height);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    constexpr float kGrip = 7.0f;
    const bool left = mouse.x >= 0.0f && mouse.x < kGrip;
    const bool right = mouse.x <= static_cast<float>(width) && mouse.x > static_cast<float>(width) - kGrip;
    const bool top = mouse.y >= 0.0f && mouse.y < kGrip;
    const bool bottom = mouse.y <= static_cast<float>(height) && mouse.y > static_cast<float>(height) - kGrip;
    if (!left && !right && !top && !bottom) {
        return 0;
    }
    const bool onCaptionButtons = mouse.y < 34.0f && mouse.x > static_cast<float>(width) - 46.0f * 3.0f;
    if (onCaptionButtons) {
        return 0;
    }
    return (left ? 1 : 0) | (right ? 2 : 0) | (top ? 4 : 0) | (bottom ? 8 : 0);
}

#ifndef _WIN32
void cursorOnScreen(int& sx, int& sy)
{
    double cx = 0.0;
    double cy = 0.0;
    glfwGetCursorPos(gChrome.window, &cx, &cy);
    int wx = 0;
    int wy = 0;
    glfwGetWindowPos(gChrome.window, &wx, &wy);
    sx = wx + static_cast<int>(cx);
    sy = wy + static_cast<int>(cy);
}

void dragCaptionGlfw(bool hovered)
{
    if (!gChrome.window) {
        return;
    }
    static bool dragging = false;
    static int anchorX = 0;
    static int anchorY = 0;
    static int originX = 0;
    static int originY = 0;
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        dragging = false;
        if (glfwGetWindowAttrib(gChrome.window, GLFW_MAXIMIZED)) {
            glfwRestoreWindow(gChrome.window);
        } else {
            glfwMaximizeWindow(gChrome.window);
        }
        return;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !windowEdgeAtCursor()) {
        dragging = true;
        cursorOnScreen(anchorX, anchorY);
        glfwGetWindowPos(gChrome.window, &originX, &originY);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        dragging = false;
    }
    if (!dragging || glfwGetWindowAttrib(gChrome.window, GLFW_MAXIMIZED)) {
        return;
    }
    int sx = 0;
    int sy = 0;
    cursorOnScreen(sx, sy);
    glfwSetWindowPos(gChrome.window, originX + (sx - anchorX), originY + (sy - anchorY));
}
#endif

void pollWindowResize()
{
    static int edge = 0;
#ifdef _WIN32
    static POINT anchor{};
#endif
    static int startX = 0;
    static int startY = 0;
    static int startW = 0;
    static int startH = 0;

    const int hover = windowEdgeAtCursor();
    if (hover) {
        ImGuiMouseCursor cursor = ImGuiMouseCursor_Arrow;
        if (hover == 1 || hover == 2) {
            cursor = ImGuiMouseCursor_ResizeEW;
        } else if (hover == 4 || hover == 8) {
            cursor = ImGuiMouseCursor_ResizeNS;
        } else if (hover == 5 || hover == 10) {
            cursor = ImGuiMouseCursor_ResizeNWSE;
        } else {
            cursor = ImGuiMouseCursor_ResizeNESW;
        }
        ImGui::SetMouseCursor(cursor);
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hover) {
        edge = hover;
        glfwGetWindowPos(gChrome.window, &startX, &startY);
        glfwGetWindowSize(gChrome.window, &startW, &startH);
#ifdef _WIN32
        GetCursorPos(&anchor);
#endif
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        edge = 0;
    }
    if (!edge || !gChrome.window) {
        return;
    }

#ifdef _WIN32
    POINT pt{};
    GetCursorPos(&pt);
    const int dx = pt.x - anchor.x;
    const int dy = pt.y - anchor.y;
    int nx = startX;
    int ny = startY;
    int nw = startW;
    int nh = startH;
    if (edge & 1) {
        nx = startX + dx;
        nw = startW - dx;
    }
    if (edge & 2) {
        nw = startW + dx;
    }
    if (edge & 4) {
        ny = startY + dy;
        nh = startH - dy;
    }
    if (edge & 8) {
        nh = startH + dy;
    }
    constexpr int kMinW = 960;
    constexpr int kMinH = 640;
    if (nw < kMinW) {
        if (edge & 1) {
            nx -= kMinW - nw;
        }
        nw = kMinW;
    }
    if (nh < kMinH) {
        if (edge & 4) {
            ny -= kMinH - nh;
        }
        nh = kMinH;
    }
    glfwSetWindowPos(gChrome.window, nx, ny);
    glfwSetWindowSize(gChrome.window, nw, nh);
#else
    int sx = 0;
    int sy = 0;
    cursorOnScreen(sx, sy);
    static int anchorX = 0;
    static int anchorY = 0;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && edge) {
        anchorX = sx;
        anchorY = sy;
    }
    const int dx = sx - anchorX;
    const int dy = sy - anchorY;
    int nx = startX;
    int ny = startY;
    int nw = startW;
    int nh = startH;
    if (edge & 1) {
        nx = startX + dx;
        nw = startW - dx;
    }
    if (edge & 2) {
        nw = startW + dx;
    }
    if (edge & 4) {
        ny = startY + dy;
        nh = startH - dy;
    }
    if (edge & 8) {
        nh = startH + dy;
    }
    constexpr int kMinW = 960;
    constexpr int kMinH = 640;
    if (nw < kMinW) {
        if (edge & 1) {
            nx -= kMinW - nw;
        }
        nw = kMinW;
    }
    if (nh < kMinH) {
        if (edge & 4) {
            ny -= kMinH - nh;
        }
        nh = kMinH;
    }
    glfwSetWindowPos(gChrome.window, nx, ny);
    glfwSetWindowSize(gChrome.window, nw, nh);
#endif
}

void drawCaptionGlyph(ImVec2 center, int kind)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    const float s = 5.5f;
    if (kind == 0) {
        dl->AddLine(ImVec2(center.x - s, center.y), ImVec2(center.x + s, center.y), col, 1.4f);
    } else if (kind == 2) {
        dl->AddLine(ImVec2(center.x - s, center.y - s), ImVec2(center.x + s, center.y + s), col, 1.4f);
        dl->AddLine(ImVec2(center.x + s, center.y - s), ImVec2(center.x - s, center.y + s), col, 1.4f);
        return;
    }

    const bool maximized = gChrome.window && glfwGetWindowAttrib(gChrome.window, GLFW_MAXIMIZED);
    if (maximized) {
        const float o = 2.0f;
        dl->AddRect(ImVec2(center.x - s + o, center.y - s - 1.0f), ImVec2(center.x + s + o, center.y + s - 1.0f), col, 0.0f, 0, 1.2f);
        dl->AddRectFilled(ImVec2(center.x - s - o, center.y - s + 2.0f), ImVec2(center.x + s - o, center.y + s + 2.0f), IM_COL32(0, 0, 0, 255));
        dl->AddRect(ImVec2(center.x - s - o, center.y - s + 2.0f), ImVec2(center.x + s - o, center.y + s + 2.0f), col, 0.0f, 0, 1.2f);
    } else {
        dl->AddRect(ImVec2(center.x - s, center.y - s), ImVec2(center.x + s, center.y + s), col, 0.0f, 0, 1.3f);
    }
}

void drawTitleBar()
{
    constexpr float kBarH = 34.0f;
    constexpr float kBtnW = 46.0f;
    const ImVec2 origin = ImGui::GetWindowPos();
    const float width = ImGui::GetWindowSize().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + kBarH), IM_COL32(0, 0, 0, 255));

    ImFont* font = titleFont();
    const bool syntheticBold = font == nullptr;
    if (!font) {
        font = ImGui::GetFont();
    }
    ImGui::PushFont(font, 16.0f);
    const char* title = gChrome.title.c_str();
    const ImVec2 textSize = ImGui::CalcTextSize(title);
    const ImVec2 textPos(origin.x + 14.0f, origin.y + (kBarH - textSize.y) * 0.5f);
    const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
    dl->AddText(font, ImGui::GetFontSize(), textPos, textCol, title);
    if (syntheticBold) {
        dl->AddText(font, ImGui::GetFontSize(), ImVec2(textPos.x + 0.7f, textPos.y), textCol, title);
    }
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##caption_drag", ImVec2(std::max(1.0f, width - kBtnW * 3.0f), kBarH));
#ifdef _WIN32
    if (ImGui::IsItemHovered()) {
        dragCaption();
    }
#else
    dragCaptionGlfw(ImGui::IsItemHovered());
#endif

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.24f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    auto captionButton = [&](const char* id, int kind, float x, bool close) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + x, origin.y));
        if (close) {
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.78f, 0.16f, 0.16f, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.68f, 0.10f, 0.10f, 1.0f));
        }
        const bool pressed = ImGui::Button(id, ImVec2(kBtnW, kBarH));
        const ImVec2 mn = ImGui::GetItemRectMin();
        const ImVec2 mx = ImGui::GetItemRectMax();
        drawCaptionGlyph(ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f), kind);
        if (close) {
            ImGui::PopStyleColor(2);
        }
        return pressed;
    };

    if (captionButton("##min", 0, width - kBtnW * 3.0f, false) && gChrome.window) {
        glfwIconifyWindow(gChrome.window);
    }
    if (captionButton("##max", 1, width - kBtnW * 2.0f, false) && gChrome.window) {
        if (glfwGetWindowAttrib(gChrome.window, GLFW_MAXIMIZED)) {
            glfwRestoreWindow(gChrome.window);
        } else {
            glfwMaximizeWindow(gChrome.window);
        }
    }
    if (captionButton("##close", 2, width - kBtnW, true) && gChrome.window) {
        glfwSetWindowShouldClose(gChrome.window, GLFW_TRUE);
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, kBarH + ImGui::GetStyle().WindowPadding.y));
}

void drawLed(bool success)
{
    const float h = ImGui::GetFrameHeight();
    const float r = 11.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 c(p.x + r + 1.0f, p.y + h * 0.5f);
    const ImU32 col = success ? IM_COL32(46, 204, 96, 255) : ImGui::GetColorU32(ImGuiCol_Text);
    ImGui::GetWindowDrawList()->AddCircleFilled(c, r, col);
    ImGui::Dummy(ImVec2(r * 2.0f + 6.0f, h));
}

const char* gSelectedSection = nullptr;
bool gSymbolsScopeOpen = false;

PlcClient* activePlc()
{
    if (!gHub) {
        return nullptr;
    }
    PlcClient* plc = gHub->selected();
    if (!plc) {
        const auto clients = gHub->clients();
        if (!clients.empty()) {
            plc = clients.front();
        }
    }
    return plc;
}

struct PlcDim {
    int32_t lower = 0;
    uint32_t count = 0;
};

struct PlcRow {
    std::string name;
    std::string typeName;
    std::string comment;
    uint32_t size = 0;
    uint32_t dataType = 0;
    uint32_t group = 0;
    uint32_t offset = 0;
    uint32_t elemSize = 0;
    std::vector<PlcDim> dims;
    bool readOnly = false;
};

struct PlcSymbolList {
    std::string key;
    std::string error;
    std::vector<PlcRow> rows;
};

std::mutex gSymMu;
std::thread gSymThread;
std::shared_ptr<PlcSymbolList> gSymList;
std::atomic<bool> gSymLoading{false};
std::atomic<bool> gSymReload{false};
uint32_t gSymEpoch = 0;

void notePlcConnect()
{
    ++gSymEpoch;
    gSymReload = true;
}

void joinSymbolLoad()
{
    if (gSymThread.joinable()) {
        gSymThread.join();
    }
}

uint32_t readU32(const uint8_t* p)
{
    uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

uint16_t readU16(const uint8_t* p)
{
    uint16_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

bool takeField(const uint8_t* data, std::size_t end, std::size_t& cursor, uint16_t len, std::string& out)
{
    if (cursor + static_cast<std::size_t>(len) + 1 > end) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(data + cursor), len);
    cursor += static_cast<std::size_t>(len) + 1;
    return true;
}

void normalizeArrayRow(PlcRow& row);

bool parsePlcSymbols(const uint8_t* data, std::size_t bytes, uint32_t count, std::vector<PlcRow>& rows)
{
    std::size_t off = 0;
    rows.reserve(static_cast<std::size_t>(count));
    for (uint32_t n = 0; n < count && off + 30 <= bytes; ++n) {
        const uint32_t entryLength = readU32(data + off);
        if (entryLength < 30 || off + entryLength > bytes) {
            break;
        }
        PlcRow row;
        row.group = readU32(data + off + 4);
        row.offset = readU32(data + off + 8);
        row.size = readU32(data + off + 12);
        row.dataType = readU32(data + off + 16);
        row.readOnly = (readU32(data + off + 20) & 0x20u) != 0;
        const uint16_t nameLen = readU16(data + off + 24);
        const uint16_t typeLen = readU16(data + off + 26);
        const uint16_t commentLen = readU16(data + off + 28);
        std::size_t cursor = off + 30;
        const std::size_t end = off + entryLength;
        if (!takeField(data, end, cursor, nameLen, row.name) ||
            !takeField(data, end, cursor, typeLen, row.typeName) ||
            !takeField(data, end, cursor, commentLen, row.comment)) {
            break;
        }
        if (!row.name.empty()) {
            normalizeArrayRow(row);
            rows.push_back(std::move(row));
        }
        off += entryLength;
    }
    std::sort(rows.begin(), rows.end(), [](const PlcRow& a, const PlcRow& b) {
        return a.name < b.name;
    });
    return !rows.empty() || count == 0;
}

std::string readWholeFile(const std::string& path)
{
    std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in) {
        return {};
    }
    in.seekg(0, std::ios::end);
    const auto n = in.tellg();
    if (n <= 0 || n > 8 * 1024 * 1024) {
        return {};
    }
    std::string data(static_cast<std::size_t>(n), '\0');
    in.seekg(0);
    in.read(data.data(), n);
    return data;
}

std::string xmlAttr(const std::string& element, const char* name)
{
    const std::string key = std::string(name) + "=\"";
    const auto a = element.find(key);
    if (a == std::string::npos) {
        return {};
    }
    const auto start = a + key.size();
    const auto b = element.find('"', start);
    if (b == std::string::npos) {
        return {};
    }
    return element.substr(start, b - start);
}

std::string innerText(const std::string& block, const char* open, const char* close)
{
    const auto a = block.find(open);
    if (a == std::string::npos) {
        return {};
    }
    const auto start = a + std::strlen(open);
    const auto b = block.find(close, start);
    if (b == std::string::npos) {
        return {};
    }
    return block.substr(start, b - start);
}

uint32_t adsTypeOf(const std::string& type)
{
    if (type == "BOOL") return 33;
    if (type == "SINT") return 16;
    if (type == "BYTE" || type == "USINT") return 17;
    if (type == "INT") return 2;
    if (type == "UINT" || type == "WORD") return 18;
    if (type == "DINT") return 3;
    if (type == "UDINT" || type == "DWORD" || type == "OTCID" || type == "TIME" || type == "DATE" || type == "TOD" || type == "DT") return 19;
    if (type == "LINT") return 20;
    if (type == "ULINT" || type == "LWORD") return 21;
    if (type == "REAL") return 4;
    if (type == "LREAL") return 5;
    if (type.compare(0, 7, "WSTRING") == 0) return 31;
    if (type.compare(0, 6, "STRING") == 0) return 30;
    return 0;
}

std::string trimCopy(std::string text)
{
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool parseArrayDecl(const std::string& type, std::vector<PlcDim>& dims, std::string& element)
{
    const std::string text = trimCopy(type);
    if (text.size() < 8) {
        return false;
    }
    std::string upper = text;
    for (char& ch : upper) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    if (upper.compare(0, 5, "ARRAY") != 0) {
        return false;
    }
    const auto left = text.find('[');
    const auto right = text.rfind(']');
    const auto of = upper.find(" OF ");
    if (left == std::string::npos || right == std::string::npos || of == std::string::npos || right < left || of < right) {
        return false;
    }
    std::string inside = text.substr(left + 1, right - left - 1);
    element = trimCopy(text.substr(of + 4));
    if (element.empty()) {
        return false;
    }
    std::string bounds;
    for (std::size_t i = 0; i < inside.size(); ++i) {
        if (inside[i] == ']' && i + 1 < inside.size() && inside[i + 1] == '[') {
            bounds.push_back(',');
            ++i;
        } else {
            bounds.push_back(inside[i]);
        }
    }
    std::vector<PlcDim> parsed;
    std::size_t start = 0;
    while (start <= bounds.size()) {
        const auto comma = bounds.find(',', start);
        const std::string part = trimCopy(bounds.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (!part.empty()) {
            const auto dots = part.find("..");
            if (dots == std::string::npos) {
                return false;
            }
            char* end = nullptr;
            const long low = std::strtol(part.c_str(), &end, 10);
            if (!end || end == part.c_str()) {
                return false;
            }
            while (*end && std::isspace(static_cast<unsigned char>(*end))) {
                ++end;
            }
            if (end[0] != '.' || end[1] != '.') {
                return false;
            }
            end += 2;
            const long high = std::strtol(end, &end, 10);
            if (high < low) {
                return false;
            }
            PlcDim dim;
            dim.lower = static_cast<int32_t>(low);
            dim.count = static_cast<uint32_t>(high - low + 1);
            parsed.push_back(dim);
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    if (parsed.empty()) {
        return false;
    }
    dims = std::move(parsed);
    return true;
}

void normalizeArrayRow(PlcRow& row)
{
    std::vector<PlcDim> fromName;
    std::string element;
    if (parseArrayDecl(row.typeName, fromName, element)) {
        row.dims = std::move(fromName);
        row.typeName = element;
    }
    if (row.dims.empty()) {
        row.elemSize = 0;
        return;
    }
    uint64_t count = 1;
    for (const PlcDim& dim : row.dims) {
        if (dim.count == 0 || count > (1ull << 20) / dim.count) {
            row.dims.clear();
            row.elemSize = 0;
            return;
        }
        count *= dim.count;
    }
    if (row.dataType == 0 || row.dataType == 65) {
        row.dataType = adsTypeOf(row.typeName);
    }
    if (row.size == 0 || (row.size % static_cast<uint32_t>(count)) != 0) {
        row.elemSize = 0;
        return;
    }
    row.elemSize = row.size / static_cast<uint32_t>(count);
}

uint32_t elementCount(const PlcRow& row)
{
    uint64_t count = 1;
    for (const PlcDim& dim : row.dims) {
        count *= dim.count;
        if (count > 0xffffffffull) {
            return 0xffffffffu;
        }
    }
    return row.dims.empty() ? 0u : static_cast<uint32_t>(count);
}

std::string formatIndexes(const std::vector<PlcDim>& dims, uint32_t linear)
{
    std::string text = "[";
    uint32_t rest = linear;
    std::vector<int32_t> index(dims.size(), 0);
    for (int dim = static_cast<int>(dims.size()) - 1; dim >= 0; --dim) {
        const uint32_t count = dims[static_cast<std::size_t>(dim)].count;
        index[static_cast<std::size_t>(dim)] = dims[static_cast<std::size_t>(dim)].lower +
            static_cast<int32_t>(count == 0 ? 0 : rest % count);
        if (count != 0) {
            rest /= count;
        }
    }
    for (std::size_t i = 0; i < index.size(); ++i) {
        if (i != 0) {
            text += ",";
        }
        text += std::to_string(index[i]);
    }
    text += "]";
    return text;
}

std::string arrayTypeLabel(const PlcRow& row)
{
    if (row.dims.empty()) {
        return row.typeName;
    }
    std::string text = "ARRAY [";
    for (std::size_t i = 0; i < row.dims.size(); ++i) {
        if (i != 0) {
            text += ", ";
        }
        const int32_t high = row.dims[i].lower + static_cast<int32_t>(row.dims[i].count) - 1;
        text += std::to_string(row.dims[i].lower);
        text += "..";
        text += std::to_string(high);
    }
    text += "] OF ";
    text += row.typeName;
    return text;
}

bool loadProjectSymbols(const std::string& netId, uint16_t port, std::vector<PlcRow>& rows)
{
    const std::string boot = readWholeFile("C:\\TwinCAT\\3.1\\Boot\\CurrentConfig.xml");
    const std::string project = innerText(boot, "<ConfigurationFile>", "</ConfigurationFile>");
    const auto netAt = boot.rfind("<AmsNetId>");
    std::string bootNet;
    if (netAt != std::string::npos) {
        bootNet = innerText(boot.substr(netAt), "<AmsNetId>", "</AmsNetId>");
    }
    if (project.empty() || bootNet.empty() || bootNet != netId) {
        return false;
    }
    const std::string tsproj = readWholeFile(project);
    const std::string portAttr = "AmsPort=\"" + std::to_string(port) + "\"";
    const auto portAt = tsproj.find(portAttr);
    if (portAt == std::string::npos) {
        return false;
    }
    const auto begin = tsproj.rfind('<', portAt);
    const auto end = tsproj.find('>', portAt);
    if (begin == std::string::npos || end == std::string::npos) {
        return false;
    }
    const std::string tmcRel = xmlAttr(tsproj.substr(begin, end - begin), "TmcFilePath");
    if (tmcRel.empty()) {
        return false;
    }
    const auto slash = project.find_last_of("\\/");
    if (slash == std::string::npos) {
        return false;
    }
    std::string tmcPath = project.substr(0, slash + 1) + tmcRel;
    for (char& ch : tmcPath) {
        if (ch == '/') {
            ch = '\\';
        }
    }
    const std::string tmc = readWholeFile(tmcPath);
    std::size_t at = 0;
    while ((at = tmc.find("<Symbol>", at)) != std::string::npos) {
        const auto stop = tmc.find("</Symbol>", at);
        if (stop == std::string::npos) {
            break;
        }
        const std::string block = tmc.substr(at, stop - at);
        at = stop + 9;
        if (block.find("<BitOffs>") == std::string::npos) {
            continue;
        }
        PlcRow row;
        row.name = innerText(block, "<Name>", "</Name>");
        if (row.name.empty()) {
            continue;
        }
        const auto typeAt = block.find("<BaseType");
        if (typeAt != std::string::npos) {
            const auto gt = block.find('>', typeAt);
            const auto lt = gt == std::string::npos ? std::string::npos : block.find('<', gt + 1);
            if (gt != std::string::npos && lt != std::string::npos && lt > gt) {
                row.typeName = block.substr(gt + 1, lt - gt - 1);
            }
        }
        const uint32_t bitSize = static_cast<uint32_t>(std::strtoul(innerText(block, "<BitSize>", "</BitSize>").c_str(), nullptr, 10));
        const uint32_t bitOffs = static_cast<uint32_t>(std::strtoul(innerText(block, "<BitOffs>", "</BitOffs>").c_str(), nullptr, 10));
        row.size = bitSize == 0 ? 0 : (bitSize + 7) / 8;
        row.dataType = adsTypeOf(row.typeName);
        row.group = 0x4040;
        row.offset = bitOffs / 8;
        row.readOnly = block.find("const_non_replaced") != std::string::npos;
        std::size_t scan = 0;
        while ((scan = block.find("<ArrayInfo>", scan)) != std::string::npos) {
            const auto infoEnd = block.find("</ArrayInfo>", scan);
            if (infoEnd == std::string::npos) {
                break;
            }
            const std::string info = block.substr(scan, infoEnd - scan);
            PlcDim dim;
            dim.lower = static_cast<int32_t>(std::strtol(innerText(info, "<LBound>", "</LBound>").c_str(), nullptr, 10));
            dim.count = static_cast<uint32_t>(std::strtoul(innerText(info, "<Elements>", "</Elements>").c_str(), nullptr, 10));
            if (dim.count > 0) {
                row.dims.push_back(dim);
            }
            scan = infoEnd + 12;
        }
        normalizeArrayRow(row);
        rows.push_back(std::move(row));
    }
    std::sort(rows.begin(), rows.end(), [](const PlcRow& a, const PlcRow& b) {
        return a.name < b.name;
    });
    return !rows.empty();
}

bool querySymbolUploadInfo(PlcClient* plc, uint32_t& count, uint32_t& bytes)
{
    unsigned char buf[64] = {};
    std::size_t got = 0;
    const uint32_t groups[] = {0xF00F, 0xF00C};
    const std::size_t lengths[] = {sizeof(buf), 8};
    for (int i = 0; i < 2; ++i) {
        got = 0;
        if (!plc->readBytes(groups[i], 0, buf, lengths[i], &got) || got < 8) {
            continue;
        }
        std::memcpy(&count, buf, 4);
        std::memcpy(&bytes, buf + 4, 4);
        return true;
    }
    return false;
}

void uploadPlcSymbols(PlcClient* plc, PlcSymbolList& list)
{
    const PlcConfig cfg = plc->config();
    const std::string netId = plc->sessionAmsNetId();
    uint32_t count = 0;
    uint32_t bytes = 0;
    if (!querySymbolUploadInfo(plc, count, bytes)) {
        if (loadProjectSymbols(netId, cfg.adsPort, list.rows)) {
            return;
        }
        list.error = plc->lastError();
        if (list.error.empty()) {
            list.error = tr("读取 PLC 符号信息失败", "Failed to read PLC symbol info");
        }
        return;
    }
    if (count == 0 || bytes == 0) {
        return;
    }
    if (bytes > 16u * 1024u * 1024u) {
        list.error = tr("PLC 符号表过大", "PLC symbol table is too large");
        return;
    }
    std::vector<uint8_t> blob(bytes);
    std::size_t got = 0;
    if (!plc->readBytes(0xF00B, 0, blob.data(), blob.size(), &got) || got == 0) {
        list.error = plc->lastError();
        if (list.error.empty()) {
            list.error = tr("读取 PLC 符号失败", "Failed to read PLC symbols");
        }
        return;
    }
    if (!parsePlcSymbols(blob.data(), got, count, list.rows)) {
        list.error = tr("PLC 符号表无法解析", "PLC symbol table could not be parsed");
    }
}

void requestPlcSymbols(PlcClient* plc, const std::string& key)
{
    if (!plc || gSymLoading.exchange(true)) {
        return;
    }
    if (gSymThread.joinable()) {
        gSymThread.join();
    }
    gSymThread = std::thread([plc, key] {
        auto list = std::make_shared<PlcSymbolList>();
        list->key = key;
        try {
            uploadPlcSymbols(plc, *list);
        } catch (const std::exception& ex) {
            list->error = ex.what();
            list->rows.clear();
        } catch (...) {
            list->error = "symbol upload failed";
            list->rows.clear();
        }
        {
            std::lock_guard<std::mutex> lock(gSymMu);
            gSymList = std::move(list);
        }
        gSymLoading = false;
    });
}

bool rowIsBool(const PlcRow& row)
{
    return row.dataType == 33 || (row.size <= 1 && row.typeName == "BOOL");
}

bool scalarWidth(const PlcRow& row, std::size_t& width)
{
    switch (row.dataType) {
    case 16:
    case 17:
        width = 1;
        return row.size == 1;
    case 2:
    case 18:
        width = 2;
        return row.size == 2;
    case 3:
    case 4:
    case 19:
        width = 4;
        return row.size == 4;
    case 5:
    case 20:
    case 21:
        width = 8;
        return row.size == 8;
    default:
        return false;
    }
}

bool rowIsString(const PlcRow& row)
{
    return row.dataType == 30 && row.size > 1 && row.size <= 256;
}

bool rowIsWString(const PlcRow& row)
{
    return row.dataType == 31 && row.size >= 2 && (row.size % 2) == 0 && row.size <= 512;
}

bool rowIsBasic(const PlcRow& row)
{
    std::size_t width = 0;
    return rowIsBool(row) || rowIsString(row) || rowIsWString(row) || scalarWidth(row, width);
}

bool rowIsExpandable(const PlcRow& row)
{
    if (row.dims.empty() || row.elemSize == 0) {
        return false;
    }
    PlcRow element;
    element.typeName = row.typeName;
    element.dataType = row.dataType == 0 || row.dataType == 65 ? adsTypeOf(row.typeName) : row.dataType;
    element.size = row.elemSize;
    return rowIsBasic(element);
}

bool makeElement(const PlcRow& parent, uint32_t linear, PlcRow& out)
{
    const uint32_t count = elementCount(parent);
    if (!rowIsExpandable(parent) || linear >= count) {
        return false;
    }
    out = parent;
    out.dims.clear();
    out.elemSize = 0;
    out.size = parent.elemSize;
    out.offset = parent.offset + linear * parent.elemSize;
    out.dataType = parent.dataType == 0 || parent.dataType == 65 ? adsTypeOf(parent.typeName) : parent.dataType;
    out.typeName = parent.typeName;
    out.name = parent.name + formatIndexes(parent.dims, linear);
    out.comment.clear();
    return true;
}

bool rowCanWrite(const PlcRow& row)
{
    if (row.readOnly) {
        return false;
    }
    std::size_t width = 0;
    return rowIsBool(row) || rowIsString(row) || rowIsWString(row) || scalarWidth(row, width);
}

void formatPlcValue(const PlcRow& row, const uint8_t* raw, std::size_t n, char* out, std::size_t outSize)
{
    if (!out || outSize == 0) {
        return;
    }
    out[0] = '\0';
    if (!raw || n == 0) {
        return;
    }
    if (rowIsBool(row)) {
        std::snprintf(out, outSize, "%s", raw[0] ? "TRUE" : "FALSE");
        return;
    }
    switch (row.dataType) {
    case 16:
        std::snprintf(out, outSize, "%d", static_cast<int>(static_cast<int8_t>(raw[0])));
        return;
    case 17:
        std::snprintf(out, outSize, "%u", static_cast<unsigned>(raw[0]));
        return;
    case 2: {
        int16_t v = 0;
        std::memcpy(&v, raw, sizeof(v));
        std::snprintf(out, outSize, "%d", static_cast<int>(v));
        return;
    }
    case 18: {
        uint16_t v = 0;
        std::memcpy(&v, raw, sizeof(v));
        std::snprintf(out, outSize, "%u", static_cast<unsigned>(v));
        return;
    }
    case 3: {
        int32_t v = 0;
        std::memcpy(&v, raw, sizeof(v));
        std::snprintf(out, outSize, "%d", static_cast<int>(v));
        return;
    }
    case 19: {
        uint32_t v = 0;
        std::memcpy(&v, raw, sizeof(v));
        std::snprintf(out, outSize, "%u", static_cast<unsigned>(v));
        return;
    }
    case 20: {
        int64_t v = 0;
        std::memcpy(&v, raw, sizeof(v));
        std::snprintf(out, outSize, "%lld", static_cast<long long>(v));
        return;
    }
    case 21: {
        uint64_t v = 0;
        std::memcpy(&v, raw, sizeof(v));
        std::snprintf(out, outSize, "%llu", static_cast<unsigned long long>(v));
        return;
    }
    case 4: {
        float v = 0;
        std::memcpy(&v, raw, sizeof(v));
        std::snprintf(out, outSize, "%.4f", static_cast<double>(v));
        return;
    }
    case 5: {
        double v = 0;
        std::memcpy(&v, raw, sizeof(v));
        std::snprintf(out, outSize, "%.6f", v);
        return;
    }
    case 30: {
        std::size_t len = 0;
        while (len < n && len + 1 < outSize && raw[len] != 0) {
            ++len;
        }
        std::memcpy(out, raw, len);
        out[len] = '\0';
        return;
    }
    case 31: {
        std::size_t used = 0;
        for (std::size_t i = 0; i + 1 < n && used + 1 < outSize; i += 2) {
            const uint16_t ch = static_cast<uint16_t>(raw[i] | (static_cast<uint16_t>(raw[i + 1]) << 8));
            if (ch == 0) {
                break;
            }
            out[used++] = ch < 128 ? static_cast<char>(ch) : '?';
        }
        out[used] = '\0';
        return;
    }
    default:
        break;
    }
    std::size_t used = 0;
    const std::size_t shown = std::min<std::size_t>(n, 8);
    for (std::size_t i = 0; i < shown && used + 4 < outSize; ++i) {
        used += static_cast<std::size_t>(std::snprintf(
            out + used, outSize - used, i == 0 ? "%02X" : " %02X", raw[i]));
    }
    if (n > shown && used + 4 < outSize) {
        std::snprintf(out + used, outSize - used, " ...");
    }
}

bool parsePlcValue(const PlcRow& row, const char* text, bool flag, std::vector<uint8_t>& out)
{
    out.clear();
    if (rowIsBool(row)) {
        out.push_back(flag ? 1 : 0);
        return true;
    }
    if (rowIsString(row)) {
        out.assign(row.size, 0);
        if (text) {
            const std::size_t n = std::min(static_cast<std::size_t>(row.size - 1), std::strlen(text));
            std::memcpy(out.data(), text, n);
        }
        return true;
    }
    if (rowIsWString(row)) {
        out.assign(row.size, 0);
        if (text) {
            const std::size_t chars = std::min(std::strlen(text), static_cast<std::size_t>(row.size / 2 - 1));
            for (std::size_t i = 0; i < chars; ++i) {
                out[i * 2] = static_cast<uint8_t>(text[i]);
            }
        }
        return true;
    }
    std::size_t width = 0;
    if (!scalarWidth(row, width)) {
        return false;
    }
    out.assign(width, 0);
    char* end = nullptr;
    switch (row.dataType) {
    case 16: {
        const long v = std::strtol(text, &end, 10);
        if (end == text) return false;
        const int8_t x = static_cast<int8_t>(v);
        std::memcpy(out.data(), &x, sizeof(x));
        return true;
    }
    case 17: {
        const unsigned long v = std::strtoul(text, &end, 10);
        if (end == text) return false;
        out[0] = static_cast<uint8_t>(v);
        return true;
    }
    case 2: {
        const long v = std::strtol(text, &end, 10);
        if (end == text) return false;
        const int16_t x = static_cast<int16_t>(v);
        std::memcpy(out.data(), &x, sizeof(x));
        return true;
    }
    case 18: {
        const unsigned long v = std::strtoul(text, &end, 10);
        if (end == text) return false;
        const uint16_t x = static_cast<uint16_t>(v);
        std::memcpy(out.data(), &x, sizeof(x));
        return true;
    }
    case 3: {
        const long v = std::strtol(text, &end, 10);
        if (end == text) return false;
        const int32_t x = static_cast<int32_t>(v);
        std::memcpy(out.data(), &x, sizeof(x));
        return true;
    }
    case 19: {
        const unsigned long v = std::strtoul(text, &end, 10);
        if (end == text) return false;
        const uint32_t x = static_cast<uint32_t>(v);
        std::memcpy(out.data(), &x, sizeof(x));
        return true;
    }
    case 20: {
        const long long v = std::strtoll(text, &end, 10);
        if (end == text) return false;
        const int64_t x = static_cast<int64_t>(v);
        std::memcpy(out.data(), &x, sizeof(x));
        return true;
    }
    case 21: {
        const unsigned long long v = std::strtoull(text, &end, 10);
        if (end == text) return false;
        const uint64_t x = static_cast<uint64_t>(v);
        std::memcpy(out.data(), &x, sizeof(x));
        return true;
    }
    case 4: {
        const float v = std::strtof(text, &end);
        if (end == text) return false;
        std::memcpy(out.data(), &v, sizeof(v));
        return true;
    }
    case 5: {
        const double v = std::strtod(text, &end);
        if (end == text) return false;
        std::memcpy(out.data(), &v, sizeof(v));
        return true;
    }
    default:
        return false;
    }
}

struct SymbolCell {
    char text[96] = "-";
    bool flag = false;
    bool known = false;
};

SymbolCell& symbolCell(const std::string& key)
{
    static std::unordered_map<std::string, SymbolCell> cells;
    static std::string bound;
    PlcClient* plc = activePlc();
    const std::string scope = plc ? plc->sessionAmsNetId() + ":" + std::to_string(gSymEpoch) : std::string();
    if (bound != scope) {
        cells.clear();
        bound = scope;
    }
    return cells[key];
}

void readPlcRow(PlcClient* plc, const PlcRow& row, SymbolCell& cell)
{
    std::size_t width = 0;
    std::size_t n = 0;
    if (rowIsBool(row)) {
        n = 1;
    } else if (scalarWidth(row, width) || rowIsString(row) || rowIsWString(row)) {
        n = (rowIsString(row) || rowIsWString(row)) ? row.size : width;
    } else if (row.size > 0) {
        n = std::min<std::size_t>(row.size, 64);
    }
    if (!plc || n == 0) {
        std::snprintf(cell.text, sizeof(cell.text), "-");
        cell.known = false;
        return;
    }
    std::vector<uint8_t> raw(n);
    std::size_t got = 0;
    if (!plc->readBytes(row.group, row.offset, raw.data(), n, &got) || got == 0) {
        const std::string err = plc->lastError();
        std::snprintf(cell.text, sizeof(cell.text), "%s", err.empty() ? "-" : err.c_str());
        cell.known = false;
        return;
    }
    cell.flag = raw[0] != 0;
    cell.known = true;
    formatPlcValue(row, raw.data(), got, cell.text, sizeof(cell.text));
}

std::string cellKeyOf(const PlcRow& row)
{
    return row.name + "#" + std::to_string(row.offset);
}

void readArrayElements(PlcClient* plc, const PlcRow& row, SymbolCell& parentCell)
{
    const uint32_t count = elementCount(row);
    if (!plc || !rowIsExpandable(row) || count == 0 || row.size == 0 || row.size > 256u * 1024u) {
        std::snprintf(parentCell.text, sizeof(parentCell.text), "-");
        parentCell.known = false;
        return;
    }
    std::vector<uint8_t> raw(row.size);
    std::size_t got = 0;
    if (!plc->readBytes(row.group, row.offset, raw.data(), row.size, &got) || got < row.size) {
        const std::string err = plc->lastError();
        std::snprintf(parentCell.text, sizeof(parentCell.text), "%s", err.empty() ? "-" : err.c_str());
        parentCell.known = false;
        return;
    }
    const uint32_t shown = std::min(count, 4096u);
    for (uint32_t i = 0; i < shown; ++i) {
        PlcRow element;
        if (!makeElement(row, i, element)) {
            continue;
        }
        SymbolCell& cell = symbolCell(cellKeyOf(element));
        const std::size_t at = static_cast<std::size_t>(i) * row.elemSize;
        formatPlcValue(element, raw.data() + at, row.elemSize, cell.text, sizeof(cell.text));
        cell.flag = raw[at] != 0;
        cell.known = true;
    }
    std::snprintf(parentCell.text, sizeof(parentCell.text), "%s", tr("已读取", "Read"));
    parentCell.known = true;
}

void writePlcRow(PlcClient* plc, const PlcRow& row, SymbolCell& cell)
{
    if (!plc || !rowCanWrite(row)) {
        return;
    }
    std::vector<uint8_t> raw;
    if (!parsePlcValue(row, cell.text, cell.flag, raw) || raw.empty()) {
        return;
    }
    if (!plc->writeBytes(row.group, row.offset, raw.data(), raw.size())) {
        const std::string err = plc->lastError();
        std::snprintf(cell.text, sizeof(cell.text), "%s", err.empty() ? "-" : err.c_str());
        cell.known = false;
        return;
    }
    readPlcRow(plc, row, cell);
}

bool textMatches(const std::string& hay, const std::string& raw, const std::string& lower)
{
    if (raw.empty()) {
        return true;
    }
    if (hay.find(raw) != std::string::npos) {
        return true;
    }
    std::string folded(hay.size(), '\0');
    for (std::size_t i = 0; i < hay.size(); ++i) {
        folded[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(hay[i])));
    }
    return folded.find(lower) != std::string::npos;
}
void drawSymbolTable()
{
    PlcClient* plc = activePlc();
    const bool linked = plc && plc->state() == ConnectionState::Connected;
    if (!linked) {
        ImGui::TextUnformatted(tr("请先连接 PLC，表格显示该 PLC 的符号。", "Connect a PLC to list its symbols."));
        return;
    }

    const std::string key = plc->sessionAmsNetId() + ":" + std::to_string(plc->config().adsPort) + ":" +
        std::to_string(gSymEpoch);
    std::shared_ptr<PlcSymbolList> list;
    {
        std::lock_guard<std::mutex> lock(gSymMu);
        list = gSymList;
    }
    if (ImGui::Button(tr("刷新", "Refresh"))) {
        gSymReload = true;
    }
    const bool pending = gSymLoading.load() || gSymReload.load() || !list || list->key != key;
    ImGui::SameLine();
    if (pending) {
        if (!gSymLoading.load()) {
            gSymReload = false;
            requestPlcSymbols(plc, key);
        }
        ImGui::TextUnformatted(tr("正在从 PLC 读取符号...", "Reading symbols from the PLC..."));
        return;
    }

    static char filter[128] = "";
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##sym_filter", tr("筛选符号", "Filter symbols"), filter, sizeof(filter));
    if (!list->error.empty()) {
        ImGui::TextUnformatted(list->error.c_str());
    }
    if (list->rows.empty()) {
        if (list->error.empty()) {
            ImGui::TextUnformatted(tr("PLC 没有符号", "The PLC has no symbols."));
        }
        return;
    }

    static const PlcSymbolList* filteredFrom = nullptr;
    static std::string filteredText;
    static std::vector<int> filtered;
    if (filteredFrom != list.get() || filteredText != filter) {
        filteredFrom = list.get();
        filteredText = filter;
        filtered.clear();
        if (!filteredText.empty()) {
            std::string lower = filteredText;
            for (char& c : lower) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            for (int i = 0; i < static_cast<int>(list->rows.size()); ++i) {
                const PlcRow& row = list->rows[static_cast<std::size_t>(i)];
                if (textMatches(row.name, filteredText, lower) ||
                    textMatches(row.typeName, filteredText, lower) ||
                    textMatches(row.comment, filteredText, lower)) {
                    filtered.push_back(i);
                }
            }
        }
    }

    const bool useFilter = filter[0] != '\0';
    const int rowCount = useFilter ? static_cast<int>(filtered.size()) : static_cast<int>(list->rows.size());
    if (useFilter && rowCount == 0) {
        ImGui::TextUnformatted(tr("没有匹配的符号", "No matching symbols."));
        return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##symbol_table", 7, flags, ImVec2(0.0f, 0.0f))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(tr("名称", "Name"), ImGuiTableColumnFlags_WidthStretch, 2.2f);
    ImGui::TableSetupColumn(tr("类型", "Type"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn(tr("注释", "Comment"), ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn(tr("大小", "Size"), ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableSetupColumn(tr("值", "Value"), ImGuiTableColumnFlags_WidthFixed, 160.0f);
    ImGui::TableSetupColumn(tr("读取", "Read"), ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableSetupColumn(tr("写入", "Write"), ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableHeadersRow();

    struct ViewItem {
        int symbol = 0;
        int linear = -1;
    };
    static std::unordered_set<std::string> openArrays;
    std::vector<ViewItem> view;
    view.reserve(static_cast<std::size_t>(rowCount));
    for (int i = 0; i < rowCount; ++i) {
        const int index = useFilter ? filtered[static_cast<std::size_t>(i)] : i;
        view.push_back(ViewItem{index, -1});
        const PlcRow& row = list->rows[static_cast<std::size_t>(index)];
        if (rowIsExpandable(row) && openArrays.find(row.name) != openArrays.end()) {
            const uint32_t shown = std::min(elementCount(row), 4096u);
            for (uint32_t element = 0; element < shown; ++element) {
                view.push_back(ViewItem{index, static_cast<int>(element)});
            }
        }
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(view.size()));
    while (clipper.Step()) {
        for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex) {
            const ViewItem item = view[static_cast<std::size_t>(rowIndex)];
            const PlcRow& parent = list->rows[static_cast<std::size_t>(item.symbol)];
            PlcRow element;
            const bool child = item.linear >= 0 && makeElement(parent, static_cast<uint32_t>(item.linear), element);
            const PlcRow& row = child ? element : parent;
            const bool expandable = !child && rowIsExpandable(parent);
            SymbolCell& cell = symbolCell(cellKeyOf(row));
            const bool canWrite = !expandable && rowCanWrite(row);
            ImGui::TableNextRow();
            ImGui::PushID(item.symbol);
            ImGui::PushID(item.linear + 1);
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            if (expandable) {
                const bool open = openArrays.find(parent.name) != openArrays.end();
                if (ImGui::ArrowButton("##exp", open ? ImGuiDir_Down : ImGuiDir_Right)) {
                    if (open) {
                        openArrays.erase(parent.name);
                    } else {
                        openArrays.insert(parent.name);
                    }
                }
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(row.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(expandable ? arrayTypeLabel(parent).c_str() : row.typeName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(row.comment.c_str());
            ImGui::TableSetColumnIndex(3);
            if (row.size == 0) {
                ImGui::TextUnformatted("bit");
            } else {
                ImGui::Text("%u", row.size);
            }
            ImGui::TableSetColumnIndex(4);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (expandable) {
                const uint32_t count = elementCount(parent);
                if (!cell.known && cell.text[0] != '\0' && std::strcmp(cell.text, "-") != 0) {
                    ImGui::TextUnformatted(cell.text);
                } else if (count > 4096u) {
                    ImGui::Text("%u %s", count, tr("个元素，展开前 4096 个", "elements, first 4096 shown"));
                } else {
                    ImGui::Text("%u %s", count, tr("个元素", "elements"));
                }
            } else if (rowIsBool(row) && cell.known && canWrite) {
                ImGui::Checkbox("##value", &cell.flag);
            } else if (canWrite && !rowIsBool(row)) {
                ImGui::InputText("##value", cell.text, sizeof(cell.text));
            } else {
                ImGui::TextUnformatted(cell.text);
            }
            ImGui::TableSetColumnIndex(5);
            if (ImGui::SmallButton(tr("读取##sym_read", "Read##sym_read"))) {
                if (expandable) {
                    readArrayElements(plc, parent, cell);
                } else {
                    readPlcRow(plc, row, cell);
                }
            }
            ImGui::TableSetColumnIndex(6);
            ImGui::BeginDisabled(!canWrite);
            if (ImGui::SmallButton(tr("写入##sym_write", "Write##sym_write"))) {
                writePlcRow(plc, row, cell);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

void drawMarker(const char* title, const char* section)
{
    const bool open = ImGui::CollapsingHeader(title);
    if (std::strcmp(section, "SymbolsScope") == 0) {
        gSymbolsScopeOpen = open;
    }
    if (ImGui::IsItemClicked()) {
        gSelectedSection = open ? title : nullptr;
    }
    if (open) {
        ImGui::DemoMarker("ShellWindow.cpp", 0, section);
    }
}

void drawSelectedLabel()
{
    if (!gSelectedSection || gSelectedSection[0] == '\0') {
        return;
    }
    ImGui::PushFont(ImGui::GetFont(), 32.0f);
    const ImVec2 text = ImGui::CalcTextSize(gSelectedSection);
    const ImVec2 size = ImGui::GetWindowSize();
    ImGui::SetCursorPos(ImVec2(
        std::max(0.0f, (size.x - text.x) * 0.5f),
        std::max(0.0f, (size.y - text.y) * 0.5f)));
    ImGui::TextUnformatted(gSelectedSection);
    ImGui::PopFont();
}

void drawFrameFooter(const ImGuiViewport* vp)
{
    char fps[32];
    std::snprintf(fps, sizeof(fps), "FPS: %.0f", ImGui::GetIO().Framerate);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(fps).x);
    ImGui::TextUnformatted(fps);

    const bool maximized = gChrome.window && glfwGetWindowAttrib(gChrome.window, GLFW_MAXIMIZED);
    const float rounding = maximized ? 0.0f : kCornerRadius;
    ImGui::GetForegroundDrawList()->AddRect(
        vp->WorkPos + ImVec2(1.0f, 1.0f),
        vp->WorkPos + vp->WorkSize - ImVec2(1.0f, 1.0f),
        ImGui::GetColorU32(ImGuiCol_Border),
        rounding,
        0,
        1.0f);
    ImGui::End();
    ImGui::PopStyleVar();
}

void drawPlcConnectionBarImpl()
{
    static int ipIndex = 0;
    static int portIndex = 0;
    static std::vector<std::string> ips = {"172.13.158.17", "172.13.158.13"};
    static std::vector<std::string> netIds = {"172.13.158.17.1.1", "172.13.158.13.1.1"};
    const char* ports[] = {"851", "852", "853", "801", "10000"};
    mergeScannedIps(ips, netIds);
    if (netIds.size() < ips.size()) {
        netIds.resize(ips.size());
    }
    if (ipIndex < 0 || ipIndex >= static_cast<int>(ips.size())) {
        ipIndex = 0;
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Net IP:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(168.0f);
    const char* preview = ips.empty() ? "—" : ips[static_cast<size_t>(ipIndex)].c_str();
    if (ImGui::BeginCombo("##net_ip", preview)) {
        if (ImGui::IsWindowAppearing()) {
            requestIpScan();
        }
        for (int i = 0; i < static_cast<int>(ips.size()); ++i) {
            const bool selected = ipIndex == i;
            if (ImGui::Selectable(ips[static_cast<size_t>(i)].c_str(), selected)) {
                ipIndex = i;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("Port:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::Combo("##port", &portIndex, ports, IM_ARRAYSIZE(ports));
    ImGui::SameLine();

    PlcClient* plc = nullptr;
    if (gHub) {
        plc = gHub->selected();
        if (!plc) {
            const auto clients = gHub->clients();
            if (!clients.empty()) {
                plc = clients.front();
            }
        }
    }
    const bool linked = plc && plc->state() == ConnectionState::Connected;
    drawLed(linked);
    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    if (ImGui::Button(linked ? tr("断开", "Disconnect") : tr("连接", "Connect"), ImVec2(108.0f, 0.0f))) {
        if (plc && linked) {
            plc->disconnect();
        } else if (plc && plc->state() != ConnectionState::Connecting && gHub && !ips.empty()) {
            const int portSlot = portIndex >= 0 && portIndex < IM_ARRAYSIZE(ports) ? portIndex : 0;
            const auto port = static_cast<uint16_t>(std::atoi(ports[portSlot]));
            const std::string ip = ips[static_cast<size_t>(ipIndex)];
            const std::string netId = ipIndex < static_cast<int>(netIds.size()) && !netIds[static_cast<size_t>(ipIndex)].empty()
                ? netIds[static_cast<size_t>(ipIndex)]
                : amsNetIdFromIp(ip);
            gHub->applyConnectionSettings(plc->config().id, ip, port, netId);
            notePlcConnect();
            plc->connectAsync();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(currentLanguage() == Language::Zh ? "中文" : "EN", ImVec2(88.0f, 0.0f))) {
        toggleLanguage();
    }
    ImGui::PopStyleVar();
}

void drawExplorerFrame()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##explorer", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
    pollWindowResize();
    drawTitleBar();
    if (gProductFrame) {
        gProductFrame();
        drawFrameFooter(vp);
        return;
    }

    drawPlcConnectionBarImpl();
    ImGui::Separator();

    ImVec2 body = ImGui::GetContentRegionAvail();
    body.y -= ImGui::GetFrameHeightWithSpacing();
    if (body.y < 1.0f) {
        body.y = 1.0f;
    }

    constexpr float kMinSide = 300.0f;
    constexpr float kMaxSide = 450.0f;
    const float maxSide = std::min(kMaxSide, std::max(kMinSide, body.x - 160.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(kMinSide, body.y), ImVec2(maxSide, body.y));
    ImGui::BeginChild("##demo_area", ImVec2(kMinSide, body.y),
        ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

    const float labelWidth = ImGui::GetFontSize() * 12.0f;
    ImGui::PushItemWidth(-labelWidth);
    gSymbolsScopeOpen = false;
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.08f, 0.18f, 0.36f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.12f, 0.26f, 0.48f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.05f, 0.12f, 0.26f, 1.00f));
    drawMarker("SlaveRobot", "SlaveRobot");
    drawMarker("MasterRobot", "MasterRobot");
    drawMarker("MSWork", "MSWork");
    drawMarker("Factory", "Factory");
    drawMarker("SymbolsScope", "SymbolsScope");
    ImGui::PopStyleColor(3);
    ImGui::PopItemWidth();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##editor", ImVec2(0.0f, body.y), ImGuiChildFlags_Borders);
    if (gSymbolsScopeOpen) {
        drawSymbolTable();
    } else {
        drawSelectedLabel();
    }
    ImGui::EndChild();

    drawFrameFooter(vp);
}

} // namespace

void drawPlcConnectionBar()
{
    drawPlcConnectionBarImpl();
}

ShellWindow::ShellWindow(std::string title, AdsHub& hub, FrameCallback onFrame, WindowConfig window)
    : title_(std::move(title))
    , hub_(hub)
    , onFrame_(std::move(onFrame))
    , window_(window)
{
    if (title_.empty()) {
        title_ = platform::executableStem();
    }
}

int ShellWindow::run()
{
    glfwSetErrorCallback([](int error, const char* desc) {
        std::fprintf(stderr, "GLFW %d: %s\n", error, desc);
    });
    if (!glfwInit()) {
        return 1;
    }

#ifdef _WIN32
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(window_.width, window_.height, title_.c_str(), nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
#ifndef _WIN32
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
#endif
    gChrome.window = window;
    gChrome.title = title_;
    gHub = &hub_;
    gProductFrame = onFrame_;
#ifdef _WIN32
    setCaptionBlack(window);
    updateWindowCorners(window);
#endif
    glfwShowWindow(window);
#ifdef _WIN32
    updateWindowCorners(window);
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImPlot3D::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    applyIndustrialDarkTheme();
    loadDefaultFonts();
    ImPlot::StyleColorsDark();
    ImPlot3D::StyleColorsDark();

#ifdef _WIN32
    ImGui_ImplGlfw_InitForOther(window, true);
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* deviceContext = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* renderTarget = nullptr;
    auto releaseTarget = [&]() {
        if (renderTarget) {
            renderTarget->Release();
            renderTarget = nullptr;
        }
    };
    auto createTarget = [&]() -> bool {
        releaseTarget();
        ID3D11Texture2D* back = nullptr;
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&back)))) {
            return false;
        }
        const HRESULT created = device->CreateRenderTargetView(back, nullptr, &renderTarget);
        back->Release();
        return SUCCEEDED(created);
    };
    HWND hwnd = glfwGetWin32Window(window);
    DXGI_SWAP_CHAIN_DESC swapDesc = {};
    swapDesc.BufferCount = 2;
    swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.OutputWindow = hwnd;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT deviceHr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
        &swapDesc, &swapChain, &device, &featureLevel, &deviceContext);
    if (FAILED(deviceHr)) {
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        swapDesc.BufferCount = 1;
        deviceHr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
            &swapDesc, &swapChain, &device, &featureLevel, &deviceContext);
    }
    if (FAILED(deviceHr) || !createTarget() || !ImGui_ImplDX11_Init(device, deviceContext)) {
        releaseTarget();
        if (swapChain) {
            swapChain->Release();
        }
        if (deviceContext) {
            deviceContext->Release();
        }
        if (device) {
            device->Release();
        }
        ImGui_ImplGlfw_Shutdown();
        ImPlot3D::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
#else
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
#endif

    logInfo(std::string(tr("启动界面: ", "UI start: ")) + title_);

    std::atomic<bool> hubRun{true};
    std::thread hubThread([&hubRun, this] {
        while (hubRun.load(std::memory_order_relaxed)) {
            if (!gSymLoading.load(std::memory_order_relaxed)) {
                hub_.tick();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    int backW = 0;
    int backH = 0;
#ifdef _WIN32
    if (HMODULE winmm = LoadLibraryW(L"winmm.dll")) {
        using TimePeriod = unsigned int(WINAPI*)(unsigned int);
        if (auto begin = reinterpret_cast<TimePeriod>(GetProcAddress(winmm, "timeBeginPeriod"))) {
            begin(1);
        }
    }
#endif
    using frame_clock = std::chrono::steady_clock;
    constexpr auto kFrame = std::chrono::nanoseconds(1'000'000'000 / 120);
    while (!glfwWindowShouldClose(window)) {
        const auto frameStart = frame_clock::now();
        glfwPollEvents();
#ifdef _WIN32
        updateWindowCorners(window);
#endif

#ifdef _WIN32
        ImGui_ImplDX11_NewFrame();
#else
        ImGui_ImplOpenGL3_NewFrame();
#endif
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        drawExplorerFrame();
        ImGui::Render();

        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
#ifdef _WIN32
        if (w > 0 && h > 0 && (w != backW || h != backH)) {
            releaseTarget();
            deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
            swapChain->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_UNKNOWN, 0);
            createTarget();
            backW = w;
            backH = h;
        }
        const float clearColor[4] = {bg.x, bg.y, bg.z, 1.0f};
        deviceContext->OMSetRenderTargets(1, &renderTarget, nullptr);
        D3D11_VIEWPORT viewport = {};
        viewport.Width = static_cast<float>(w);
        viewport.Height = static_cast<float>(h);
        viewport.MaxDepth = 1.0f;
        deviceContext->RSSetViewports(1, &viewport);
        if (renderTarget) {
            deviceContext->ClearRenderTargetView(renderTarget, clearColor);
        }
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swapChain->Present(0, 0);
#else
        glViewport(0, 0, w, h);
        glClearColor(bg.x, bg.y, bg.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
#endif
        const auto deadline = frameStart + kFrame;
        const auto remain = deadline - frame_clock::now();
        if (remain > std::chrono::milliseconds(2)) {
            std::this_thread::sleep_for(remain - std::chrono::milliseconds(1));
        }
#ifdef _WIN32
        while (frame_clock::now() < deadline) {
            YieldProcessor();
        }
#endif
    }

    hubRun.store(false, std::memory_order_relaxed);
    if (hubThread.joinable()) {
        hubThread.join();
    }

    joinSymbolLoad();
    joinIpScan();
    gChrome.window = nullptr;
    gHub = nullptr;

#ifdef _WIN32
    ImGui_ImplDX11_Shutdown();
    releaseTarget();
    if (swapChain) {
        swapChain->Release();
    }
    if (deviceContext) {
        deviceContext->Release();
    }
    if (device) {
        device->Release();
    }
#else
    ImGui_ImplOpenGL3_Shutdown();
#endif
    ImGui_ImplGlfw_Shutdown();
    ImPlot3D::DestroyContext();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace tcGUICore
