#pragma once

#include <cstdint>
#include <string>

namespace habr::services::fetcher {

struct HttpResponse {
    bool ok = false;
    long status_code = 0;
    std::string body;
    // Empty when ok == true.
    std::string error;
};

class HttpClient {
  public:
    HttpClient(std::string user_agent, uint32_t timeout_seconds);

    HttpResponse Get(const std::string& url) const;

  private:
    std::string user_agent_;
    uint32_t timeout_seconds_;
};

} // namespace habr::services::fetcher
