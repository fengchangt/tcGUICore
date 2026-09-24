#pragma once

#include "Common/Types.h"

#include <string>

namespace tcGUICore {

Language currentLanguage();
void setLanguage(Language lang);
void toggleLanguage();

// 界面字符串：中文 / English。日志与按钮都走这里。
const char* tr(const char* zh, const char* en);

const char* connectionStateText(ConnectionState state);
const char* ioDirectionText(IoDirection dir);
std::string displayNameText(const LocalizedText& text);

} // namespace tcGUICore
