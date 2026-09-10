#include "html_view.hpp"

#include <iomanip>
#include <sstream>

namespace habr::services::web {

using shared::messaging::DigestArticle;
using shared::messaging::DigestReadyMessage;

namespace {

constexpr const char* kStyle = R"CSS(
:root {
  --bg: #f6f7f9; --card: #ffffff; --text: #1a1d21; --muted: #6b7280;
  --accent: #2563eb; --line: #e3e6ea; --chip: #eef1f5;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #16181c; --card: #1e2126; --text: #e8eaed; --muted: #9aa3ad;
    --accent: #7aa2f7; --line: #2c3138; --chip: #262b32;
  }
}
* { box-sizing: border-box; }
body {
  margin: 0; padding: 32px 20px 64px;
  background: var(--bg); color: var(--text);
  font: 15px/1.55 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
}
.wrap { max-width: 820px; margin: 0 auto; }
header { margin-bottom: 28px; }
h1 { margin: 0 0 6px; font-size: 24px; letter-spacing: -0.01em; }
.meta { color: var(--muted); font-size: 13px; }
.meta code { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 12px; }
.item {
  background: var(--card); border: 1px solid var(--line); border-radius: 10px;
  padding: 14px 16px; margin-bottom: 10px; display: flex; gap: 14px;
}
.rank { color: var(--muted); font-variant-numeric: tabular-nums; font-size: 18px; min-width: 26px; }
.body { min-width: 0; flex: 1; }
.title { font-size: 16px; font-weight: 600; margin-bottom: 5px; }
.title a { color: var(--text); text-decoration: none; }
.title a:hover { color: var(--accent); text-decoration: underline; }
.byline { color: var(--muted); font-size: 13px; margin-bottom: 8px; }
.hubs { display: flex; flex-wrap: wrap; gap: 5px; margin-bottom: 8px; }
.hub { background: var(--chip); color: var(--muted); border-radius: 5px; padding: 2px 7px; font-size: 12px; }
.stats { display: flex; flex-wrap: wrap; gap: 14px; font-size: 13px; color: var(--muted);
         font-variant-numeric: tabular-nums; }
.stats b { color: var(--text); font-weight: 600; }
.empty {
  background: var(--card); border: 1px dashed var(--line); border-radius: 10px;
  padding: 32px; text-align: center; color: var(--muted);
}
.archive { margin-top: 34px; border-top: 1px solid var(--line); padding-top: 16px; }
.archive h2 { font-size: 13px; text-transform: uppercase; letter-spacing: 0.06em;
              color: var(--muted); margin: 0 0 10px; font-weight: 600; }
.archive ul { margin: 0; padding-left: 18px; color: var(--muted); font-size: 13px; }
footer { margin-top: 28px; color: var(--muted); font-size: 12px; }
footer a { color: var(--accent); }
)CSS";

std::string FormatNumber(int64_t value) {
    std::string digits = std::to_string(value < 0 ? -value : value);
    std::string out;
    int seen = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (seen > 0 && seen % 3 == 0) {
            out.push_back(' ');
        }
        out.push_back(*it);
        ++seen;
    }
    if (value < 0) {
        out.push_back('-');
    }
    return std::string(out.rbegin(), out.rend());
}

std::string FormatScore(double score) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << score;
    return out.str();
}

void RenderArticle(std::ostringstream& out, size_t rank, const DigestArticle& article) {
    out << "<div class=\"item\">";
    out << "<div class=\"rank\">" << rank << "</div>";
    out << "<div class=\"body\">";

    out << "<div class=\"title\"><a href=\"" << EscapeHtml(article.url)
        << "\" target=\"_blank\" rel=\"noopener noreferrer\">" << EscapeHtml(article.title)
        << "</a></div>";

    if (!article.author.empty()) {
        out << "<div class=\"byline\">" << EscapeHtml(article.author);
        if (!article.published_at.empty()) {
            out << " · " << EscapeHtml(article.published_at);
        }
        out << "</div>";
    }

    if (!article.hubs.empty()) {
        out << "<div class=\"hubs\">";
        for (const std::string& hub : article.hubs) {
            out << "<span class=\"hub\">" << EscapeHtml(hub) << "</span>";
        }
        out << "</div>";
    }

    out << "<div class=\"stats\">"
        << "<span>рейтинг <b>" << FormatNumber(article.votes) << "</b></span>"
        << "<span>просмотры <b>" << FormatNumber(article.views) << "</b></span>"
        << "<span>закладки <b>" << FormatNumber(article.bookmarks) << "</b></span>"
        << "<span>комментарии <b>" << FormatNumber(article.comments) << "</b></span>"
        << "<span>score <b>" << FormatScore(article.score) << "</b></span>"
        << "</div>";

    out << "</div></div>";
}

} // namespace

std::string EscapeHtml(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        case '\'':
            escaped += "&#39;";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

std::string RenderPage(const std::deque<DigestReadyMessage>& digests) {
    std::ostringstream out;

    out << "<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\">"
        << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        << "<meta http-equiv=\"refresh\" content=\"30\">"
        << "<title>Habr digest</title><style>" << kStyle << "</style></head><body><div class=\"wrap\">";

    out << "<header><h1>Habr digest</h1>";

    if (digests.empty()) {
        out << "<div class=\"meta\">Ожидаем первый дайджест от storage.</div></header>";
        out << "<div class=\"empty\">Пока пусто.<br>Дайджест появится здесь, как только storage "
               "опубликует его в <code>digest_ready</code>.</div>";
    } else {
        const DigestReadyMessage& latest = digests.front();

        out << "<div class=\"meta\">Топ " << latest.articles.size() << " за последние "
            << latest.period_hours << " ч · собран " << EscapeHtml(latest.generated_at)
            << " · trace_id <code>" << EscapeHtml(latest.trace_id) << "</code></div></header>";

        size_t rank = 1;
        for (const DigestArticle& article : latest.articles) {
            RenderArticle(out, rank, article);
            ++rank;
        }

        if (digests.size() > 1) {
            out << "<div class=\"archive\"><h2>Предыдущие дайджесты</h2><ul>";
            for (size_t i = 1; i < digests.size(); ++i) {
                out << "<li>" << EscapeHtml(digests[i].generated_at) << " — "
                    << digests[i].articles.size() << " статей</li>";
            }
            out << "</ul></div>";
        }
    }

    out << "</div></body></html>";
    return out.str();
}

} // namespace habr::services::web
