#include "html_parser.hpp"

#include "common/log.hpp"

#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>

namespace habr::services::parser {

namespace {

using shared::common::LogWarn;
using shared::messaging::ParsedArticle;

struct XmlDocDeleter {
    void operator()(xmlDoc* doc) const { xmlFreeDoc(doc); }
};
struct XPathContextDeleter {
    void operator()(xmlXPathContext* ctx) const { xmlXPathFreeContext(ctx); }
};
struct XPathObjectDeleter {
    void operator()(xmlXPathObject* obj) const { xmlXPathFreeObject(obj); }
};
struct XmlCharDeleter {
    void operator()(xmlChar* value) const { xmlFree(value); }
};

using DocPtr = std::unique_ptr<xmlDoc, XmlDocDeleter>;
using ContextPtr = std::unique_ptr<xmlXPathContext, XPathContextDeleter>;
using ObjectPtr = std::unique_ptr<xmlXPathObject, XPathObjectDeleter>;
using StringPtr = std::unique_ptr<xmlChar, XmlCharDeleter>;

std::string HasClass(const char* class_name) {
    return std::string("contains(concat(' ', normalize-space(@class), ' '), ' ") + class_name +
           " ')";
}

ObjectPtr Evaluate(xmlXPathContext* ctx, xmlNode* node, const std::string& expression) {
    ctx->node = node;
    return ObjectPtr(
        xmlXPathEvalExpression(reinterpret_cast<const xmlChar*>(expression.c_str()), ctx));
}

xmlNode* FirstNode(const ObjectPtr& object) {
    if (!object || object->nodesetval == nullptr || object->nodesetval->nodeNr == 0) {
        return nullptr;
    }
    return object->nodesetval->nodeTab[0];
}

std::string Trim(const std::string& value) {
    static const std::string kNbsp = "\xC2\xA0";

    size_t begin = 0;
    size_t end = value.size();

    for (bool trimmed = true; trimmed && begin < end;) {
        trimmed = false;
        if (std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
            ++begin;
            trimmed = true;
        } else if (end - begin >= kNbsp.size() && value.compare(begin, kNbsp.size(), kNbsp) == 0) {
            begin += kNbsp.size();
            trimmed = true;
        }
    }

    for (bool trimmed = true; trimmed && begin < end;) {
        trimmed = false;
        if (std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
            --end;
            trimmed = true;
        } else if (end - begin >= kNbsp.size() &&
                   value.compare(end - kNbsp.size(), kNbsp.size(), kNbsp) == 0) {
            end -= kNbsp.size();
            trimmed = true;
        }
    }

    return value.substr(begin, end - begin);
}

std::string TextOf(xmlNode* node) {
    if (node == nullptr) {
        return {};
    }
    StringPtr content(xmlNodeGetContent(node));
    if (!content) {
        return {};
    }
    return Trim(reinterpret_cast<const char*>(content.get()));
}

std::string AttributeOf(xmlNode* node, const char* name) {
    if (node == nullptr) {
        return {};
    }
    StringPtr value(xmlGetProp(node, reinterpret_cast<const xmlChar*>(name)));
    if (!value) {
        return {};
    }
    return Trim(reinterpret_cast<const char*>(value.get()));
}

std::string TextAt(xmlXPathContext* ctx, xmlNode* node, const std::string& expression) {
    return TextOf(FirstNode(Evaluate(ctx, node, expression)));
}

std::string AttributeAt(xmlXPathContext* ctx, xmlNode* node, const std::string& expression,
                        const char* attribute) {
    return AttributeOf(FirstNode(Evaluate(ctx, node, expression)), attribute);
}

int64_t ParseCount(const std::string& raw) {
    std::string cleaned;
    cleaned.reserve(raw.size());

    double multiplier = 1.0;
    for (size_t i = 0; i < raw.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(raw[i]);
        if (std::isdigit(ch) != 0 || ch == '-' || ch == '.' || ch == ',') {
            cleaned.push_back(ch == ',' ? '.' : static_cast<char>(ch));
        } else if (ch == 'K' || ch == 'k') {
            multiplier = 1000.0;
        } else if (ch == 'M' || ch == 'm') {
            multiplier = 1000000.0;
        } else if (raw.compare(i, 2, "\xD0\x9A") == 0) {
            multiplier = 1000.0;
        } else if (raw.compare(i, 2, "\xD0\x9C") == 0) {
            multiplier = 1000000.0;
        }
    }

    if (cleaned.empty() || cleaned == "-" || cleaned == ".") {
        return 0;
    }

    try {
        return static_cast<int64_t>(std::stod(cleaned) * multiplier);
    } catch (const std::exception&) {
        return 0;
    }
}

} // namespace

void InitHtmlParserLibrary() {
    xmlInitParser();
}

void ShutdownHtmlParserLibrary() {
    xmlCleanupParser();
}

HtmlParser::HtmlParser(std::string base_url) : base_url_(std::move(base_url)) {}

ParseResult HtmlParser::Parse(const std::string& html) const {
    ParseResult result;

    if (html.empty()) {
        return result;
    }

    const int options =
        HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET;

    DocPtr doc(htmlReadMemory(html.data(), static_cast<int>(html.size()), "habr-listing.html",
                              "UTF-8", options));
    if (!doc) {
        LogWarn() << "libxml2 could not parse the document at all";
        return result;
    }

    ContextPtr ctx(xmlXPathNewContext(doc.get()));
    if (!ctx) {
        return result;
    }

    const std::string articles_query = "//article[" + HasClass("tm-articles-list__item") + "]";
    ObjectPtr articles = Evaluate(ctx.get(), nullptr, articles_query);
    if (!articles || articles->nodesetval == nullptr) {
        return result;
    }

    const std::string title_query = ".//a[" + HasClass("tm-title__link") + "]";
    const std::string author_query = ".//a[" + HasClass("tm-user-info__username") + "]";
    const std::string time_query = ".//time[@datetime]";
    const std::string views_query =
        ".//span[" + HasClass("tm-icon-counter__value") + " and @title]";
    const std::string votes_query = ".//span[" + HasClass("tm-votes-meter__value") + "]";
    const std::string bookmarks_query =
        ".//button[" + HasClass("bookmarks-button") + "]//span[" + HasClass("counter") + "]";
    const std::string comments_query =
        ".//a[" + HasClass("article-comments-counter-link") + "]//span[" + HasClass("value") + "]";
    const std::string hubs_query = ".//a[" + HasClass("tm-publication-hub__link") + "]";
    const std::string complexity_query =
        ".//span[" + HasClass("tm-article-complexity__label") + "]";
    const std::string reading_time_query =
        ".//span[" + HasClass("tm-article-reading-time__label") + "]";

    for (int i = 0; i < articles->nodesetval->nodeNr; ++i) {
        xmlNode* article_node = articles->nodesetval->nodeTab[i];

        ParsedArticle article;
        article.article_id = AttributeOf(article_node, "id");

        ObjectPtr title_object = Evaluate(ctx.get(), article_node, title_query);
        xmlNode* title_node = FirstNode(title_object);
        article.title = TextOf(title_node);

        const std::string href = AttributeOf(title_node, "href");
        if (!href.empty()) {
            article.url = (href.rfind("http", 0) == 0) ? href : base_url_ + href;
        }

        if (article.article_id.empty() || article.title.empty()) {
            ++result.skipped;
            continue;
        }

        article.author = TextAt(ctx.get(), article_node, author_query);
        article.published_at = AttributeAt(ctx.get(), article_node, time_query, "datetime");
        article.complexity = TextAt(ctx.get(), article_node, complexity_query);
        article.reading_time = TextAt(ctx.get(), article_node, reading_time_query);

        const std::string views_title = AttributeAt(ctx.get(), article_node, views_query, "title");
        article.views = ParseCount(
            views_title.empty() ? TextAt(ctx.get(), article_node, views_query) : views_title);

        article.votes = ParseCount(TextAt(ctx.get(), article_node, votes_query));
        article.bookmarks = ParseCount(TextAt(ctx.get(), article_node, bookmarks_query));
        article.comments = ParseCount(TextAt(ctx.get(), article_node, comments_query));

        ObjectPtr hubs = Evaluate(ctx.get(), article_node, hubs_query);
        if (hubs && hubs->nodesetval != nullptr) {
            for (int h = 0; h < hubs->nodesetval->nodeNr; ++h) {
                std::string hub = TextOf(hubs->nodesetval->nodeTab[h]);
                if (!hub.empty()) {
                    article.hubs.push_back(std::move(hub));
                }
            }
        }

        result.articles.push_back(std::move(article));
    }

    return result;
}

} // namespace habr::services::parser
