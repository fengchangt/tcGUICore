#include "Log.h"

namespace tcGUICore {

LogBuffer& LogBuffer::instance()
{
    static LogBuffer g;
    return g;
}

void LogBuffer::push(LogLevel level, std::string text)
{
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.push_back({level, std::move(text)});
    if (lines_.size() > kMax) {
        lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(lines_.size() - kMax));
    }
}

std::vector<LogLine> LogBuffer::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lines_;
}

void LogBuffer::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.clear();
}

void logInfo(const std::string& text)
{
    LogBuffer::instance().push(LogLevel::Info, text);
}

void logWarn(const std::string& text)
{
    LogBuffer::instance().push(LogLevel::Warn, text);
}

void logError(const std::string& text)
{
    LogBuffer::instance().push(LogLevel::Error, text);
}

} // namespace tcGUICore
