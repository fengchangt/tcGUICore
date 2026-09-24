#include "I18n.h"

namespace tcGUICore {

namespace {
Language g_language = Language::Zh;
}

Language currentLanguage()
{
    return g_language;
}

void setLanguage(Language lang)
{
    g_language = lang;
}

void toggleLanguage()
{
    g_language = (g_language == Language::Zh) ? Language::En : Language::Zh;
}

const char* tr(const char* zh, const char* en)
{
    return (g_language == Language::En) ? en : zh;
}

const char* connectionStateText(ConnectionState state)
{
    switch (state) {
    case ConnectionState::Disconnected:
        return tr("未连接", "Disconnected");
    case ConnectionState::Connecting:
        return tr("连接中", "Connecting");
    case ConnectionState::Connected:
        return tr("已连接", "Connected");
    case ConnectionState::Error:
        return tr("故障", "Fault");
    }
    return "?";
}

const char* ioDirectionText(IoDirection dir)
{
    return (dir == IoDirection::Output) ? tr("输出", "Out") : tr("输入", "In");
}

std::string displayNameText(const LocalizedText& text)
{
    const std::string& s = text.get(g_language);
    return s;
}

} // namespace tcGUICore
