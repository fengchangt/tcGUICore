#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace tcGUICore {

enum class LogLevel { Info, Warn, Error };

struct LogLine {
    LogLevel level = LogLevel::Info;
    std::string text;
};

class LogBuffer {
public:
    static LogBuffer& instance();

    void push(LogLevel level, std::string text);
    std::vector<LogLine> snapshot() const;
    void clear();

private:
    mutable std::mutex mutex_;
    std::vector<LogLine> lines_;
    static constexpr std::size_t kMax = 400;
};

void logInfo(const std::string& text);
void logWarn(const std::string& text);
void logError(const std::string& text);

} // namespace tcGUICore
