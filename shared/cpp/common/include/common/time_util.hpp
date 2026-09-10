#pragma once

#include <string>

namespace habr::shared::common {

std::string NowIso8601Utc();

std::string GenerateTraceId();

} // namespace habr::shared::common
