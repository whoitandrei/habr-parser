#pragma once

#include "messaging/messages.hpp"

#include <string>

namespace habr::services::notifier {

std::string RenderDigestMarkdown(const shared::messaging::DigestReadyMessage& digest);

void WriteFileAtomically(const std::string& path, const std::string& content);

} // namespace habr::services::notifier
