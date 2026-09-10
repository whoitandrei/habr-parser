#pragma once

#include "messaging/messages.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace habr::services::parser {

struct ParseResult {
    std::vector<shared::messaging::ParsedArticle> articles;
    size_t skipped = 0;
};

class HtmlParser {
  public:
    explicit HtmlParser(std::string base_url = "https://habr.com");

    ParseResult Parse(const std::string& html) const;

  private:
    std::string base_url_;
};

void InitHtmlParserLibrary();
void ShutdownHtmlParserLibrary();

} // namespace habr::services::parser
