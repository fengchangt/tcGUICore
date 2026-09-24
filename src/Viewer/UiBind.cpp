#include "UiBind.h"

#include "tcGUICore.h"

#include "Common/I18n.h"

#include "imgui.h"

#include <cstring>
#include <string>

namespace tcGUICore {

namespace {

RuntimeVariable* requireVar(const char* key)
{
    if (!Application::instance() || !key) {
        return nullptr;
    }
    return Application::instance()->ads().variable(key);
}

std::string widgetLabel(const RuntimeVariable& var)
{
    std::string lab = var.config.label.get(currentLanguage());
    if (lab.empty()) {
        lab = var.config.name;
    }
    lab += "###";
    lab += variableKey(var.config.plcId, var.config.name);
    return lab;
}

} // namespace

bool uiCheckbox(const char* key)
{
    RuntimeVariable* var = requireVar(key);
    if (!var || var->config.type != ValueType::Bool) {
        ImGui::TextDisabled("%s", key ? key : "?");
        return false;
    }
    bool value = var->data[0] != 0;
    const bool writable = var->config.direction == IoDirection::Output && var->netIdOk;
    ImGui::BeginDisabled(!writable);
    const bool changed = ImGui::Checkbox(widgetLabel(*var).c_str(), &value);
    ImGui::EndDisabled();
    if (changed && writable) {
        const uint8_t b = value ? 1 : 0;
        Application::instance()->ads().writeFromUi(*var, &b, 1);
    }
    return changed;
}

bool uiDragFloat(const char* key, float vmin, float vmax)
{
    RuntimeVariable* var = requireVar(key);
    if (!var || (var->config.type != ValueType::Float && var->config.type != ValueType::Double)) {
        ImGui::TextDisabled("%s", key ? key : "?");
        return false;
    }
    float value = 0.f;
    if (var->config.type == ValueType::Float) {
        std::memcpy(&value, var->data.data(), sizeof(float));
    } else {
        double d = 0;
        std::memcpy(&d, var->data.data(), sizeof(double));
        value = static_cast<float>(d);
    }
    const bool writable = var->config.direction == IoDirection::Output && var->netIdOk;
    ImGui::BeginDisabled(!writable);
    const bool changed = ImGui::DragFloat(widgetLabel(*var).c_str(), &value, 0.01f, vmin, vmax, "%.4f");
    ImGui::EndDisabled();
    if (changed && writable) {
        if (var->config.type == ValueType::Float) {
            Application::instance()->ads().writeFromUi(*var, &value, sizeof(float));
        } else {
            const double d = static_cast<double>(value);
            Application::instance()->ads().writeFromUi(*var, &d, sizeof(double));
        }
    }
    return changed;
}

bool uiDragInt(const char* key, int vmin, int vmax)
{
    RuntimeVariable* var = requireVar(key);
    if (!var) {
        ImGui::TextDisabled("%s", key ? key : "?");
        return false;
    }
    int value = 0;
    switch (var->config.type) {
    case ValueType::Int8:
        value = static_cast<int8_t>(var->data[0]);
        break;
    case ValueType::UInt8:
        value = var->data[0];
        break;
    case ValueType::Int16: {
        int16_t v = 0;
        std::memcpy(&v, var->data.data(), sizeof(v));
        value = v;
        break;
    }
    case ValueType::UInt16: {
        uint16_t v = 0;
        std::memcpy(&v, var->data.data(), sizeof(v));
        value = v;
        break;
    }
    case ValueType::Int32:
        std::memcpy(&value, var->data.data(), sizeof(int32_t));
        break;
    default:
        ImGui::TextDisabled("%s", key);
        return false;
    }
    const bool writable = var->config.direction == IoDirection::Output && var->netIdOk;
    ImGui::BeginDisabled(!writable);
    const bool changed = ImGui::DragInt(widgetLabel(*var).c_str(), &value, 1.0f, vmin, vmax);
    ImGui::EndDisabled();
    if (changed && writable) {
        uint8_t buf[8] = {};
        const std::size_t n = valueTypeSize(var->config.type);
        switch (var->config.type) {
        case ValueType::Int8:
            buf[0] = static_cast<uint8_t>(static_cast<int8_t>(value));
            break;
        case ValueType::UInt8:
            buf[0] = static_cast<uint8_t>(value);
            break;
        case ValueType::Int16: {
            const int16_t v = static_cast<int16_t>(value);
            std::memcpy(buf, &v, sizeof(v));
            break;
        }
        case ValueType::UInt16: {
            const uint16_t v = static_cast<uint16_t>(value);
            std::memcpy(buf, &v, sizeof(v));
            break;
        }
        case ValueType::Int32:
            std::memcpy(buf, &value, sizeof(int32_t));
            break;
        default:
            break;
        }
        Application::instance()->ads().writeFromUi(*var, buf, n);
    }
    return changed;
}

void uiValueText(const char* key)
{
    RuntimeVariable* var = requireVar(key);
    if (!var) {
        ImGui::TextDisabled("%s", key ? key : "?");
        return;
    }
    const std::string lab = var->config.label.get(currentLanguage());
    if (!var->netIdOk) {
        ImGui::Text("%s: %s", lab.c_str(), tr("NetId 不匹配", "NetId mismatch"));
        return;
    }
    if (!var->ok) {
        ImGui::Text("%s: —", lab.c_str());
        return;
    }
    if (var->config.type == ValueType::Bool) {
        ImGui::Text("%s: %s", lab.c_str(), var->data[0] ? "TRUE" : "FALSE");
    } else if (var->config.type == ValueType::Float) {
        float v = 0;
        std::memcpy(&v, var->data.data(), sizeof(v));
        ImGui::Text("%s: %.4f", lab.c_str(), v);
    } else if (var->config.type == ValueType::Double) {
        double v = 0;
        std::memcpy(&v, var->data.data(), sizeof(v));
        ImGui::Text("%s: %.6f", lab.c_str(), v);
    } else {
        ImGui::Text("%s", lab.c_str());
        ImGui::SameLine();
        uiDragInt(key);
    }
}

} // namespace tcGUICore
