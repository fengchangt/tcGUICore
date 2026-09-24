#include "Theme.h"

#include "Common/Log.h"
#include "Common/Platform.h"

#include "imgui.h"

#include <filesystem>
#include <string>
#include <vector>

namespace tcGUICore {

namespace {
ImFont* gTitleFont = nullptr;
}

ImFont* titleFont()
{
    return gTitleFont;
}

void applyIndustrialDarkTheme()
{
    // 贴近 imgui_explorer / Dear ImGui 默认 Dark：高对比正文、略透窗口底。
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 14.0f;
    s.ChildRounding = 10.0f;
    s.FrameRounding = 6.0f;
    s.GrabRounding = 6.0f;
    s.PopupRounding = 10.0f;
    s.ScrollbarRounding = 8.0f;
    s.TabRounding = 8.0f;
    s.WindowBorderSize = 1.0f;
    s.ChildBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.PopupBorderSize = 1.0f;
    s.FramePadding = ImVec2(8.0f, 4.0f);
    s.ItemSpacing = ImVec2(8.0f, 6.0f);
    s.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    s.WindowPadding = ImVec2(10.0f, 8.0f);
    s.GrabMinSize = 10.0f;
    s.ScrollbarSize = 14.0f;
    s.Alpha = 1.0f;
    s.DisabledAlpha = 0.50f;
    s.FontSizeBase = 18.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = ImVec4(0.93f, 0.95f, 0.96f, 0.60f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.58f, 0.62f, 0.60f);
    c[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.09f, 0.10f, 0.96f);
    c[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);
    c[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.18f, 0.36f, 0.96f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.26f, 0.48f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.05f, 0.12f, 0.26f, 1.00f);
    c[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.06f, 0.07f, 0.55f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.16f, 0.18f, 0.72f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.06f, 0.07f, 0.45f);
    c[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.90f, 0.86f, 1.00f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.75f, 0.78f, 0.85f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.55f, 0.90f, 0.86f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.08f, 0.18f, 0.36f, 0.96f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.12f, 0.26f, 0.48f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.05f, 0.12f, 0.26f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.30f, 0.38f, 0.46f, 0.90f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.38f, 0.48f, 0.58f, 1.00f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.32f, 0.40f, 1.00f);
    c[ImGuiCol_Separator] = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);
    c[ImGuiCol_SeparatorHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);
    c[ImGuiCol_SeparatorActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);
    c[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);
    c[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
}

void loadDefaultFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    struct Candidate {
        const char* path;
        int fontNo;
    };
    const Candidate fonts[] = {
        {"C:/Windows/Fonts/msyh.ttc", 0},
        {"C:/Windows/Fonts/msyh.ttf", 0},
        {"C:/Windows/Fonts/simhei.ttf", 0},
        {"C:/Windows/Fonts/segoeui.ttf", 0},
        {"/System/Library/Fonts/PingFang.ttc", 0},
        {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 0},
        {"/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf", 0},
        {"/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc", 0},
        {"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 0},
        {"/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", 0},
        {"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", 0},
        {"/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc", 0},
        {"/usr/share/fonts/truetype/arphic/uming.ttc", 0},
        {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 0},
    };

    ImFont* loaded = nullptr;
    std::string used;
    for (const auto& f : fonts) {
        std::error_code ec;
        if (!std::filesystem::exists(f.path, ec)) {
            continue;
        }
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = true;
        cfg.FontNo = f.fontNo;
        loaded = io.Fonts->AddFontFromFileTTF(f.path, 18.0f, &cfg);
        if (loaded) {
            used = f.path;
            break;
        }
    }

    if (!loaded) {
        logWarn(u8"未找到中文字体，界面汉字可能无法显示");
        io.Fonts->AddFontDefault();
        return;
    }
    io.FontDefault = loaded;
    logInfo(std::string(u8"已加载字体 ") + used);

    const Candidate boldFonts[] = {
        {"C:/Windows/Fonts/msyhbd.ttc", 0},
        {"C:/Windows/Fonts/msyhbd.ttf", 0},
        {"C:/Windows/Fonts/segoeuib.ttf", 0},
        {"/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc", 0},
        {"/usr/share/fonts/opentype/noto/NotoSansCJKsc-Bold.otf", 0},
        {"/usr/share/fonts/truetype/noto/NotoSansCJK-Bold.ttc", 0},
        {"/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc", 0},
        {"/usr/share/fonts/google-noto-cjk/NotoSansCJK-Bold.ttc", 0},
    };
    for (const auto& f : boldFonts) {
        std::error_code ec;
        if (!std::filesystem::exists(f.path, ec)) {
            continue;
        }
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = true;
        cfg.FontNo = f.fontNo;
        gTitleFont = io.Fonts->AddFontFromFileTTF(f.path, 16.0f, &cfg);
        if (gTitleFont) {
            logInfo(std::string(u8"已加载标题粗体 ") + f.path);
            break;
        }
    }
}

} // namespace tcGUICore
