#pragma once

struct ImFont;

namespace tcGUICore {

void applyIndustrialDarkTheme();
void loadDefaultFonts();
// 标题栏用的加粗字体；系统里没有粗体文件时返回空。
ImFont* titleFont();

} // namespace tcGUICore
