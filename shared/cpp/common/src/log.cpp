#include "common/log.hpp"

#include "common/time_util.hpp"

#include <iostream>

namespace habr::shared::common {

namespace {

std::string& ServiceNameStorage() {
    static std::string name = "service";
    return name;
}

} // namespace

void SetServiceName(std::string name) {
    ServiceNameStorage() = std::move(name);
}

LogLine::LogLine(const char* level, bool to_stderr) : to_stderr_(to_stderr) {
    buffer_ << NowIso8601Utc() << " [" << level << "] [" << ServiceNameStorage() << "] ";
}

LogLine::~LogLine() {
    std::ostream& out = to_stderr_ ? std::cerr : std::cout;
    out << buffer_.str() << std::endl;
}

LogLine LogInfo() {
    return LogLine("INFO", false);
}

LogLine LogWarn() {
    return LogLine("WARN", true);
}

LogLine LogError() {
    return LogLine("ERROR", true);
}

} // namespace habr::shared::common
