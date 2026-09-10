#include "digest_writer.hpp"

#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace habr::services::notifier {

using shared::messaging::DigestArticle;
using shared::messaging::DigestReadyMessage;

namespace {

std::string JoinHubs(const std::vector<std::string>& hubs) {
    std::ostringstream joined;
    for (size_t i = 0; i < hubs.size(); ++i) {
        if (i > 0) {
            joined << ", ";
        }
        joined << hubs[i];
    }
    return joined.str();
}

} // namespace

std::string RenderDigestMarkdown(const DigestReadyMessage& digest) {
    std::ostringstream out;

    out << "# Habr digest — " << digest.generated_at << "\n\n";
    out << "Top " << digest.articles.size() << " of the last " << digest.period_hours
        << " hours. _trace_id: " << digest.trace_id << "_\n\n";

    int position = 1;
    for (const DigestArticle& article : digest.articles) {
        out << position << ". **[" << article.title << "](" << article.url << ")**\n";

        if (!article.author.empty()) {
            out << "   - author: " << article.author << "\n";
        }
        if (!article.hubs.empty()) {
            out << "   - hubs: " << JoinHubs(article.hubs) << "\n";
        }
        if (!article.published_at.empty()) {
            out << "   - published: " << article.published_at << "\n";
        }

        out << "   - rating " << article.votes << " · views " << article.views << " · bookmarks "
            << article.bookmarks << " · comments " << article.comments << "\n";
        out << "   - score " << std::fixed << std::setprecision(1) << article.score << "\n\n";

        ++position;
    }

    return out.str();
}

void WriteFileAtomically(const std::string& path, const std::string& content) {
    const std::string temporary = path + ".tmp";

    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("cannot open " + temporary + " for writing");
        }
        file << content;
        if (!file) {
            throw std::runtime_error("write to " + temporary + " failed");
        }
    }

    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::remove(temporary.c_str());
        throw std::runtime_error("cannot rename " + temporary + " to " + path);
    }
}

} // namespace habr::services::notifier
