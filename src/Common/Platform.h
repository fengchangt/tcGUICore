#pragma once

#include <string>
#include <vector>

namespace tcGUICore::platform {

std::string executableStem();
std::string executableDir();
std::string firstExistingFile(const std::vector<std::string>& candidates);
std::string chineseFontPath();

} // namespace tcGUICore::platform
