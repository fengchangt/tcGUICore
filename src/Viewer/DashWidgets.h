#pragma once

// 深色仪表盘控件。产品在 onFrame 里调用 tcGUICore::dash。
// 卡片必须 beginCard / endCard 成对；其余控件各自画完并平衡样式栈。

#include "imgui.h"

#include <cstddef>

namespace tcGUICore::dash {

struct Palette {
    ImVec4 window;
    ImVec4 card;
    ImVec4 cardBorder;
    ImVec4 text;
    ImVec4 muted;
    ImVec4 field;
    ImVec4 green;
    ImVec4 greenDim;
    ImVec4 blue;
    ImVec4 cyan;
    ImVec4 amber;
    ImVec4 violet;
    ImVec4 danger;
    ImVec4 knob;
};

enum class Accent { Green, Blue, Cyan, Amber, Violet };

const Palette& palette();
ImU32 colorOf(Accent accent);

bool beginCard(const char* id, const ImVec2& size = ImVec2(0.0f, 0.0f));
void endCard();

void caption(const char* text);
void title(const char* text, float size = 22.0f);
void hint(const char* text);

void led(bool on, float radius = 6.0f);
void progress(float fraction);

bool toggle(const char* id, bool* value);
bool toggleRow(const char* id, const char* label, bool* value);
bool slider(const char* id, float* value, float vMin, float vMax);
bool button(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f), bool primary = false);
bool textField(const char* id, char* buffer, std::size_t bufferSize, ImGuiInputTextFlags flags = 0);
bool numberField(const char* id, int* value, int vMin, int vMax);
bool combo(const char* id, int* current, const char* const items[], int count);
bool check(const char* id, const char* label, bool* value);
bool radio(const char* id, const char* label, int* current, int option);
void badge(const char* label, Accent accent = Accent::Green);
void divider();
bool chip(const char* label, bool selected, Accent accent = Accent::Green);

using IconFn = void (*)(ImDrawList* drawList, ImVec2 center, ImU32 color);
bool iconButton(const char* id, IconFn icon, bool active, float side = 44.0f);

void iconPlug(ImDrawList* drawList, ImVec2 center, ImU32 color);
void iconPulse(ImDrawList* drawList, ImVec2 center, ImU32 color);
void iconArm(ImDrawList* drawList, ImVec2 center, ImU32 color);
void iconArmPair(ImDrawList* drawList, ImVec2 center, ImU32 color);
void iconPower(ImDrawList* drawList, ImVec2 center, ImU32 color);
void iconTiles(ImDrawList* drawList, ImVec2 center, ImU32 color);

void sparkline(const float* samples, int count, const ImVec2& size, float yMin = 0.0f, float yMax = 1.0f);

} // namespace tcGUICore::dash
