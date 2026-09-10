#include "article_store.hpp"

#include "common/log.hpp"
#include "common/time_util.hpp"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <chrono>
#include <stdexcept>

namespace habr::services::storage {

namespace {

using shared::messaging::DigestArticle;
using shared::messaging::ParsedArticle;

int64_t NowEpochSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

class Statement {
  public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("sqlite prepare failed: ") + sqlite3_errmsg(db));
        }
    }

    ~Statement() { sqlite3_finalize(stmt_); }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void BindText(int index, const std::string& value) {
        sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT);
    }
    void BindInt64(int index, int64_t value) { sqlite3_bind_int64(stmt_, index, value); }
    void BindDouble(int index, double value) { sqlite3_bind_double(stmt_, index, value); }

    bool Step() {
        const int code = sqlite3_step(stmt_);
        if (code == SQLITE_ROW) {
            return true;
        }
        if (code == SQLITE_DONE) {
            return false;
        }
        throw std::runtime_error(std::string("sqlite step failed: ") + sqlite3_errmsg(db_));
    }

    void Reset() {
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
    }

    std::string ColumnText(int index) const {
        const unsigned char* text = sqlite3_column_text(stmt_, index);
        return text == nullptr ? std::string() : reinterpret_cast<const char*>(text);
    }
    int64_t ColumnInt64(int index) const { return sqlite3_column_int64(stmt_, index); }
    double ColumnDouble(int index) const { return sqlite3_column_double(stmt_, index); }

  private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};

void Exec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = (error != nullptr) ? error : "unknown error";
        sqlite3_free(error);
        throw std::runtime_error("sqlite exec failed: " + message);
    }
}

constexpr const char* kSchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS articles (
    article_id       TEXT PRIMARY KEY,
    url              TEXT NOT NULL,
    title            TEXT NOT NULL,
    author           TEXT NOT NULL DEFAULT '',
    hubs             TEXT NOT NULL DEFAULT '[]',
    published_at     TEXT NOT NULL DEFAULT '',
    complexity       TEXT NOT NULL DEFAULT '',
    reading_time     TEXT NOT NULL DEFAULT '',
    votes            INTEGER NOT NULL DEFAULT 0,
    views            INTEGER NOT NULL DEFAULT 0,
    bookmarks        INTEGER NOT NULL DEFAULT 0,
    comments         INTEGER NOT NULL DEFAULT 0,
    first_seen_at    TEXT NOT NULL,
    first_seen_epoch INTEGER NOT NULL,
    updated_at       TEXT NOT NULL,
    updated_epoch    INTEGER NOT NULL,
    trace_id         TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_articles_first_seen ON articles(first_seen_epoch);
)SQL";

constexpr const char* kUpsertSql = R"SQL(
INSERT INTO articles (
    article_id, url, title, author, hubs, published_at, complexity, reading_time,
    votes, views, bookmarks, comments,
    first_seen_at, first_seen_epoch, updated_at, updated_epoch, trace_id
) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17)
ON CONFLICT(article_id) DO UPDATE SET
    url          = excluded.url,
    title        = excluded.title,
    author       = excluded.author,
    hubs         = excluded.hubs,
    published_at = excluded.published_at,
    complexity   = excluded.complexity,
    reading_time = excluded.reading_time,
    votes        = excluded.votes,
    views        = excluded.views,
    bookmarks    = excluded.bookmarks,
    comments     = excluded.comments,
    updated_at   = excluded.updated_at,
    updated_epoch= excluded.updated_epoch,
    trace_id     = excluded.trace_id
)SQL";

constexpr const char* kTopSql = R"SQL(
SELECT article_id, url, title, author, hubs, published_at,
       votes, views, bookmarks, comments, trace_id,
       (votes * ?1 + bookmarks * ?2 + comments * ?3 + views * ?4) AS score
FROM articles
WHERE first_seen_epoch >= ?5
ORDER BY score DESC, votes DESC, article_id ASC
LIMIT ?6
)SQL";

std::vector<std::string> DecodeHubs(const std::string& encoded) {
    try {
        return nlohmann::json::parse(encoded).get<std::vector<std::string>>();
    } catch (const std::exception&) {
        return {};
    }
}

} // namespace

ArticleStore::ArticleStore(const std::string& database_path) {
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    if (sqlite3_open_v2(database_path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
        const std::string message = (db_ != nullptr) ? sqlite3_errmsg(db_) : "out of memory";
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("cannot open database " + database_path + ": " + message);
    }

    sqlite3_busy_timeout(db_, 5000);

    ApplySchema();

    shared::common::LogInfo() << "Database ready at " << database_path << " (" << CountArticles()
                              << " articles)";
}

ArticleStore::~ArticleStore() {
    sqlite3_close(db_);
}

void ArticleStore::ApplySchema() {
    Exec(db_, "PRAGMA journal_mode=WAL;");
    Exec(db_, "PRAGMA synchronous=NORMAL;");
    Exec(db_, kSchemaSql);
}

size_t ArticleStore::UpsertBatch(const std::vector<ParsedArticle>& articles,
                                 const std::string& trace_id) {
    if (articles.empty()) {
        return 0;
    }

    const std::string now_iso = shared::common::NowIso8601Utc();
    const int64_t now_epoch = NowEpochSeconds();

    Exec(db_, "BEGIN IMMEDIATE");

    try {
        Statement statement(db_, kUpsertSql);
        for (const ParsedArticle& article : articles) {
            statement.BindText(1, article.article_id);
            statement.BindText(2, article.url);
            statement.BindText(3, article.title);
            statement.BindText(4, article.author);
            statement.BindText(5, nlohmann::json(article.hubs).dump());
            statement.BindText(6, article.published_at);
            statement.BindText(7, article.complexity);
            statement.BindText(8, article.reading_time);
            statement.BindInt64(9, article.votes);
            statement.BindInt64(10, article.views);
            statement.BindInt64(11, article.bookmarks);
            statement.BindInt64(12, article.comments);
            statement.BindText(13, now_iso);
            statement.BindInt64(14, now_epoch);
            statement.BindText(15, now_iso);
            statement.BindInt64(16, now_epoch);
            statement.BindText(17, trace_id);

            statement.Step();
            statement.Reset();
        }
    } catch (...) {
        Exec(db_, "ROLLBACK");
        throw;
    }

    Exec(db_, "COMMIT");
    return articles.size();
}

std::vector<DigestArticle> ArticleStore::TopArticles(uint32_t period_hours, uint32_t limit,
                                                     const ScoreWeights& weights) const {
    const int64_t cutoff = NowEpochSeconds() - static_cast<int64_t>(period_hours) * 3600;

    Statement statement(db_, kTopSql);
    statement.BindDouble(1, weights.votes);
    statement.BindDouble(2, weights.bookmarks);
    statement.BindDouble(3, weights.comments);
    statement.BindDouble(4, weights.views);
    statement.BindInt64(5, cutoff);
    statement.BindInt64(6, limit);

    std::vector<DigestArticle> top;
    while (statement.Step()) {
        DigestArticle article;
        article.article_id = statement.ColumnText(0);
        article.url = statement.ColumnText(1);
        article.title = statement.ColumnText(2);
        article.author = statement.ColumnText(3);
        article.hubs = DecodeHubs(statement.ColumnText(4));
        article.published_at = statement.ColumnText(5);
        article.votes = statement.ColumnInt64(6);
        article.views = statement.ColumnInt64(7);
        article.bookmarks = statement.ColumnInt64(8);
        article.comments = statement.ColumnInt64(9);
        article.source_trace_id = statement.ColumnText(10);
        article.score = statement.ColumnDouble(11);
        top.push_back(std::move(article));
    }

    return top;
}

int64_t ArticleStore::CountArticles() const {
    Statement statement(db_, "SELECT COUNT(*) FROM articles");
    if (!statement.Step()) {
        return 0;
    }
    return statement.ColumnInt64(0);
}

} // namespace habr::services::storage
