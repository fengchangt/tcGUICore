#include "DashWidgets.h"

#include <algorithm>
#include <cmath>

namespace tcGUICore::dash {

namespace {

ImU32 pack(const ImVec4& color)
{
    return ImGui::ColorConvertFloat4ToU32(color);
}

const Palette kPalette{
    ImVec4(0.09f, 0.10f, 0.11f, 1.0f),
    ImVec4(0.16f, 0.17f, 0.19f, 1.0f),
    ImVec4(1.0f, 1.0f, 1.0f, 0.06f),
    ImVec4(0.95f, 0.96f, 0.97f, 1.0f),
    ImVec4(0.55f, 0.58f, 0.63f, 1.0f),
    ImVec4(0.10f, 0.11f, 0.12f, 1.0f),
    ImVec4(0.30f, 0.84f, 0.49f, 1.0f),
    ImVec4(0.16f, 0.28f, 0.20f, 1.0f),
    ImVec4(0.36f, 0.58f, 0.98f, 1.0f),
    ImVec4(0.28f, 0.78f, 0.86f, 1.0f),
    ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
    ImVec4(0.62f, 0.48f, 0.96f, 1.0f),
    ImVec4(0.90f, 0.32f, 0.34f, 1.0f),
    ImVec4(0.96f, 0.97f, 0.98f, 1.0f),
};

void stroke(ImDrawList* drawList, ImVec2 a, ImVec2 b, ImU32 color)
{
    drawList->AddLine(a, b, color, 1.6f);
}

} // namespace

const Palette& palette()
{
    return kPalette;
}

ImU32 colorOf(Accent accent)
{
    switch (accent) {
    case Accent::Blue:
        return pack(kPalette.blue);
    case Accent::Cyan:
        return pack(kPalette.cyan);
    case Accent::Amber:
        return pack(kPalette.amber);
    case Accent::Violet:
        return pack(kPalette.violet);
    case Accent::Green:
        break;
    }
    return pack(kPalette.green);
}

bool beginCard(const char* id, const ImVec2& size)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPalette.card);
    ImGui::PushStyleColor(ImGuiCol_Border, kPalette.cardBorder);
    ImGui::PushStyleColor(ImGuiCol_Text, kPalette.text);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 16.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    return ImGui::BeginChild(id, size, ImGuiChildFlags_Borders);
}

void endCard()
{
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}

void caption(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, kPalette.muted);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void title(const char* text, float size)
{
    ImGui::PushStyleColor(ImGuiCol_Text, kPalette.text);
    ImGui::PushFont(ImGui::GetFont(), size);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
    ImGui::PopStyleColor();
}

void hint(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, kPalette.muted);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void led(bool on, float radius)
{
    const float h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 c(p.x + radius + 2.0f, p.y + h * 0.5f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (on) {
        ImVec4 glow = kPalette.green;
        glow.w = 0.28f;
        drawList->AddCircleFilled(c, radius + 4.0f, pack(glow));
        drawList->AddCircleFilled(c, radius, pack(kPalette.green));
    } else {
        drawList->AddCircleFilled(c, radius, IM_COL32(88, 92, 100, 255));
    }
    ImGui::Dummy(ImVec2(radius * 2.0f + 8.0f, h));
}

void progress(float fraction)
{
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = 6.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 a = p;
    const ImVec2 b(p.x + width, p.y + height);
    drawList->AddRectFilled(a, b, pack(kPalette.field), 3.0f);
    if (fraction < 0.0f) {
        const float span = width * 0.28f;
        const float travel = width + span;
        const float x = p.x + std::fmod(static_cast<float>(ImGui::GetTime()) * 140.0f, travel) - span;
        const float x0 = std::clamp(x, p.x, p.x + width);
        const float x1 = std::clamp(x + span, p.x, p.x + width);
        if (x1 > x0) {
            drawList->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, p.y + height), pack(kPalette.green), 3.0f);
        }
    } else {
        const float t = std::clamp(fraction, 0.0f, 1.0f);
        if (t > 0.0f) {
            drawList->AddRectFilled(a, ImVec2(p.x + width * t, p.y + height), pack(kPalette.green), 3.0f);
        }
    }
    ImGui::Dummy(ImVec2(width, height));
}

bool toggle(const char* id, bool* value)
{
    const float width = 46.0f;
    const float height = 26.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(width, height));
    const bool pressed = ImGui::IsItemClicked();
    if (pressed && value) {
        *value = !*value;
    }
    const bool on = value && *value;
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec4 track = on ? kPalette.green : ImVec4(0.22f, 0.24f, 0.27f, 1.0f);
    if (hovered && !on) {
        track = ImVec4(0.28f, 0.30f, 0.34f, 1.0f);
    }
    const float radius = height * 0.5f;
    drawList->AddRectFilled(p, ImVec2(p.x + width, p.y + height), pack(track), radius);
    const float knob = radius - 3.0f;
    const float cx = on ? (p.x + width - radius) : (p.x + radius);
    drawList->AddCircleFilled(ImVec2(cx, p.y + radius), knob, pack(kPalette.knob));
    return pressed;
}

bool toggleRow(const char* id, const char* label, bool* value)
{
    const float row = 26.0f;
    const float y = ImGui::GetCursorPosY() + (row - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::SetCursorPosY(y);
    caption(label);
    ImGui::SameLine();
    const float toggleWidth = 46.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - toggleWidth);
    ImGui::SetCursorPosY(y - (row - ImGui::GetTextLineHeight()) * 0.5f);
    return toggle(id, value);
}

bool slider(const char* id, float* value, float vMin, float vMax)
{
    ImGui::PushID(id);
    const float width = std::max(48.0f, ImGui::CalcItemWidth());
    const float height = 22.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##track", ImVec2(width, height));
    bool changed = false;
    if (value && vMax > vMin && (ImGui::IsItemActive() || ImGui::IsItemClicked())) {
        const float t = std::clamp((ImGui::GetIO().MousePos.x - pos.x) / width, 0.0f, 1.0f);
        const float next = vMin + (vMax - vMin) * t;
        if (next != *value) {
            *value = next;
            changed = true;
        }
    }
    float t = 0.0f;
    if (value && vMax > vMin) {
        t = std::clamp((*value - vMin) / (vMax - vMin), 0.0f, 1.0f);
    }
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float midY = pos.y + height * 0.5f;
    const float trackH = 6.0f;
    drawList->AddRectFilled(
        ImVec2(pos.x, midY - trackH * 0.5f),
        ImVec2(pos.x + width, midY + trackH * 0.5f),
        pack(kPalette.field),
        3.0f);
    if (t > 0.0f) {
        drawList->AddRectFilled(
            ImVec2(pos.x, midY - trackH * 0.5f),
            ImVec2(pos.x + width * t, midY + trackH * 0.5f),
            pack(kPalette.green),
            3.0f);
    }
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    drawList->AddCircleFilled(ImVec2(pos.x + width * t, midY), hot ? 9.0f : 7.5f, pack(kPalette.knob));
    ImGui::PopID();
    return changed;
}

bool button(const char* label, const ImVec2& size, bool primary)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 8.0f));
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, kPalette.green);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.90f, 0.56f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.70f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.10f, 0.07f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, kPalette.field);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.20f, 0.23f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.13f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, kPalette.text);
    }
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    return pressed;
}

bool textField(const char* id, char* buffer, std::size_t bufferSize, ImGuiInputTextFlags flags)
{
    if (!buffer || bufferSize == 0) {
        return false;
    }
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kPalette.field);
    ImGui::PushStyleColor(ImGuiCol_Border, kPalette.cardBorder);
    ImGui::PushStyleColor(ImGuiCol_Text, kPalette.text);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
    const bool changed = ImGui::InputText(id, buffer, bufferSize, flags);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
    return changed;
}

bool numberField(const char* id, int* value, int vMin, int vMax)
{
    if (!value) {
        return false;
    }
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kPalette.field);
    ImGui::PushStyleColor(ImGuiCol_Border, kPalette.cardBorder);
    ImGui::PushStyleColor(ImGuiCol_Text, kPalette.text);
    ImGui::PushStyleColor(ImGuiCol_Button, kPalette.card);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
    const bool changed = ImGui::InputInt(id, value);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
    if (*value < vMin) {
        *value = vMin;
    }
    if (*value > vMax) {
        *value = vMax;
    }
    return changed;
}

bool check(const char* id, const char* label, bool* value)
{
    const float box = 18.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 text = ImGui::CalcTextSize(label ? label : "");
    const ImVec2 size(box + 8.0f + text.x, std::max(box, text.y));
    ImGui::InvisibleButton(id, size);
    const bool pressed = ImGui::IsItemClicked();
    if (pressed && value) {
        *value = !*value;
    }
    const bool on = value && *value;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float y = p.y + (size.y - box) * 0.5f;
    drawList->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + box, y + box), on ? pack(kPalette.green) : pack(kPalette.field), 5.0f);
    if (on) {
        stroke(drawList, ImVec2(p.x + 4.0f, y + 9.0f), ImVec2(p.x + 7.5f, y + 13.0f), IM_COL32(16, 28, 20, 255));
        stroke(drawList, ImVec2(p.x + 7.5f, y + 13.0f), ImVec2(p.x + 14.0f, y + 5.0f), IM_COL32(16, 28, 20, 255));
    }
    if (label && label[0] != '\0') {
        drawList->AddText(ImVec2(p.x + box + 8.0f, p.y + (size.y - text.y) * 0.5f), pack(kPalette.text), label);
    }
    return pressed;
}

bool radio(const char* id, const char* label, int* current, int option)
{
    const float diameter = 18.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 text = ImGui::CalcTextSize(label ? label : "");
    const ImVec2 size(diameter + 8.0f + text.x, std::max(diameter, text.y));
    ImGui::InvisibleButton(id, size);
    const bool pressed = ImGui::IsItemClicked();
    if (pressed && current) {
        *current = option;
    }
    const bool on = current && *current == option;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 c(p.x + diameter * 0.5f, p.y + size.y * 0.5f);
    drawList->AddCircleFilled(c, diameter * 0.5f, pack(kPalette.field));
    if (on) {
        drawList->AddCircleFilled(c, 5.0f, pack(kPalette.green));
    }
    if (label && label[0] != '\0') {
        drawList->AddText(ImVec2(p.x + diameter + 8.0f, p.y + (size.y - text.y) * 0.5f), pack(kPalette.text), label);
    }
    return pressed;
}

void badge(const char* label, Accent accent)
{
    const ImVec2 text = ImGui::CalcTextSize(label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size(text.x + 16.0f, 22.0f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec4 fill = ImGui::ColorConvertU32ToFloat4(colorOf(accent));
    fill.w = 0.22f;
    drawList->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), pack(fill), size.y * 0.5f);
    drawList->AddText(ImVec2(p.x + 8.0f, p.y + (size.y - text.y) * 0.5f), colorOf(accent), label);
    ImGui::Dummy(size);
}

void divider()
{
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(p.x, p.y + 6.0f), ImVec2(p.x + width, p.y + 6.0f), pack(kPalette.cardBorder), 1.0f);
    ImGui::Dummy(ImVec2(width, 12.0f));
}

bool combo(const char* id, int* current, const char* const items[], int count)
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kPalette.field);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, kPalette.card);
    ImGui::PushStyleColor(ImGuiCol_Header, kPalette.greenDim);
    ImGui::PushStyleColor(ImGuiCol_Text, kPalette.text);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
    const bool changed = ImGui::Combo(id, current, items, count);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
    return changed;
}

bool chip(const char* label, bool selected, Accent accent)
{
    const ImVec2 text = ImGui::CalcTextSize(label);
    const ImVec2 size(text.x + 22.0f, 28.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    ImGui::InvisibleButton("##chip", size);
    const bool pressed = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    const ImU32 fill = selected ? colorOf(accent) : (hovered ? IM_COL32(48, 52, 58, 255) : pack(kPalette.field));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), fill, size.y * 0.5f);
    const ImU32 textCol = selected ? IM_COL32(12, 18, 16, 255) : pack(kPalette.text);
    drawList->AddText(ImVec2(p.x + 11.0f, p.y + (size.y - text.y) * 0.5f), textCol, label);
    return pressed;
}

bool iconButton(const char* id, IconFn icon, bool active, float side)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(side, side));
    const bool pressed = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    ImU32 bg = IM_COL32(255, 255, 255, 10);
    ImU32 fg = pack(kPalette.text);
    if (active) {
        bg = pack(kPalette.green);
        fg = IM_COL32(16, 28, 20, 255);
    } else if (hovered) {
        bg = IM_COL32(255, 255, 255, 22);
    }
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(p, ImVec2(p.x + side, p.y + side), bg, 14.0f);
    if (icon) {
        icon(drawList, ImVec2(p.x + side * 0.5f, p.y + side * 0.5f), fg);
    }
    return pressed;
}

void iconPlug(ImDrawList* drawList, ImVec2 c, ImU32 color)
{
    stroke(drawList, ImVec2(c.x - 6.0f, c.y - 2.0f), ImVec2(c.x + 6.0f, c.y - 2.0f), color);
    stroke(drawList, ImVec2(c.x - 6.0f, c.y - 2.0f), ImVec2(c.x - 6.0f, c.y + 5.0f), color);
    stroke(drawList, ImVec2(c.x + 6.0f, c.y - 2.0f), ImVec2(c.x + 6.0f, c.y + 5.0f), color);
    stroke(drawList, ImVec2(c.x - 6.0f, c.y + 5.0f), ImVec2(c.x + 6.0f, c.y + 5.0f), color);
    stroke(drawList, ImVec2(c.x - 3.0f, c.y - 7.0f), ImVec2(c.x - 3.0f, c.y - 2.0f), color);
    stroke(drawList, ImVec2(c.x + 3.0f, c.y - 7.0f), ImVec2(c.x + 3.0f, c.y - 2.0f), color);
}

void iconPulse(ImDrawList* drawList, ImVec2 c, ImU32 color)
{
    stroke(drawList, ImVec2(c.x - 8.0f, c.y), ImVec2(c.x - 4.0f, c.y), color);
    stroke(drawList, ImVec2(c.x - 4.0f, c.y), ImVec2(c.x - 1.5f, c.y - 7.0f), color);
    stroke(drawList, ImVec2(c.x - 1.5f, c.y - 7.0f), ImVec2(c.x + 1.5f, c.y + 7.0f), color);
    stroke(drawList, ImVec2(c.x + 1.5f, c.y + 7.0f), ImVec2(c.x + 4.0f, c.y), color);
    stroke(drawList, ImVec2(c.x + 4.0f, c.y), ImVec2(c.x + 8.0f, c.y), color);
}

void iconArm(ImDrawList* drawList, ImVec2 c, ImU32 color)
{
    drawList->AddCircle(ImVec2(c.x - 4.0f, c.y - 4.0f), 3.2f, color, 12, 1.6f);
    stroke(drawList, ImVec2(c.x - 1.0f, c.y - 2.0f), ImVec2(c.x + 5.0f, c.y + 2.0f), color);
    stroke(drawList, ImVec2(c.x + 5.0f, c.y + 2.0f), ImVec2(c.x + 2.0f, c.y + 7.0f), color);
}

void iconArmPair(ImDrawList* drawList, ImVec2 c, ImU32 color)
{
    iconArm(drawList, ImVec2(c.x - 3.0f, c.y), color);
    drawList->AddCircle(ImVec2(c.x + 6.0f, c.y + 5.0f), 2.4f, color, 12, 1.6f);
}

void iconPower(ImDrawList* drawList, ImVec2 c, ImU32 color)
{
    drawList->PathArcTo(c, 6.5f, 0.7f, 5.6f, 16);
    drawList->PathStroke(color, 0, 1.6f);
    stroke(drawList, ImVec2(c.x, c.y - 8.0f), ImVec2(c.x, c.y - 1.0f), color);
}

void iconTiles(ImDrawList* drawList, ImVec2 c, ImU32 color)
{
    const ImVec2 cells[] = {
        ImVec2(c.x - 6.0f, c.y - 6.0f),
        ImVec2(c.x + 1.0f, c.y - 6.0f),
        ImVec2(c.x - 6.0f, c.y + 1.0f),
        ImVec2(c.x + 1.0f, c.y + 1.0f),
    };
    for (const ImVec2& cell : cells) {
        drawList->AddRectFilled(cell, ImVec2(cell.x + 5.0f, cell.y + 5.0f), color, 1.5f);
    }
}

void sparkline(const float* samples, int count, const ImVec2& size, float yMin, float yMax)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), pack(kPalette.field), 12.0f);
    const float step = 14.0f;
    for (float y = p.y + step; y < p.y + size.y; y += step) {
        for (float x = p.x + step; x < p.x + size.x; x += step) {
            drawList->AddCircleFilled(ImVec2(x, y), 1.1f, IM_COL32(255, 255, 255, 28));
        }
    }
    if (!samples || count < 2 || yMax <= yMin) {
        return;
    }
    ImVector<ImVec2> points;
    points.resize(count);
    for (int i = 0; i < count; ++i) {
        const float t = std::clamp((samples[i] - yMin) / (yMax - yMin), 0.0f, 1.0f);
        const float x = p.x + 8.0f + (size.x - 16.0f) * (static_cast<float>(i) / static_cast<float>(count - 1));
        const float y = p.y + size.y - 8.0f - t * (size.y - 16.0f);
        points[i] = ImVec2(x, y);
    }
    drawList->AddPolyline(points.Data, count, pack(kPalette.green), 0, 2.0f);
}

} // namespace tcGUICore::dash
