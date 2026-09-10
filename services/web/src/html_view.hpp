#pragma once

#include "messaging/messages.hpp"

#include <deque>
#include <string>

namespace habr::services::web {

std::string EscapeHtml(const std::string& value);

std::string RenderPage(const std::deque<shared::messaging::DigestReadyMessage>& digests);

} // namespace habr::services::web
