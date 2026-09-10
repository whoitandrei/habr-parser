#pragma once

#include "config.hpp"

#include "messaging/messages.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace habr::services::storage {

class ArticleStore {
  public:
    explicit ArticleStore(const std::string& database_path);
    ~ArticleStore();

    ArticleStore(const ArticleStore&) = delete;
    ArticleStore& operator=(const ArticleStore&) = delete;
    size_t UpsertBatch(const std::vector<shared::messaging::ParsedArticle>& articles,
                       const std::string& trace_id);

    std::vector<shared::messaging::DigestArticle> TopArticles(uint32_t period_hours, uint32_t limit,
                                                              const ScoreWeights& weights) const;

    int64_t CountArticles() const;

  private:
    void ApplySchema();

    sqlite3* db_ = nullptr;
};

} // namespace habr::services::storage
