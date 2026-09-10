#pragma once

#include <sstream>
#include <string>

namespace habr::shared::common {

void SetServiceName(std::string name);

class LogLine {
  public:
    LogLine(const char* level, bool to_stderr);
    ~LogLine();

    LogLine(const LogLine&) = delete;
    LogLine& operator=(const LogLine&) = delete;

    template <typename T>
    LogLine& operator<<(const T& value) {
        buffer_ << value;
        return *this;
    }

  private:
    std::ostringstream buffer_;
    bool to_stderr_;
};

LogLine LogInfo();
LogLine LogWarn();
LogLine LogError();

} // namespace habr::shared::common
