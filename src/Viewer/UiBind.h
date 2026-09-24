#pragma once

// example 用这些函数把 JSON 里的变量绑到自己的控件上。
// 键格式: "plcId.name"，例如 "plc1.bEnable"
// 仅 direction=output 的变量允许改写；input 只显示。

namespace tcGUICore {

bool uiCheckbox(const char* key);
bool uiDragFloat(const char* key, float vmin = -1.0e6f, float vmax = 1.0e6f);
bool uiDragInt(const char* key, int vmin = -1000000, int vmax = 1000000);
void uiValueText(const char* key);

} // namespace tcGUICore
