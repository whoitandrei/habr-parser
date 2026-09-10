#include "http_client.hpp"

#include <curl/curl.h>

#include <memory>

namespace habr::services::fetcher {

namespace {

size_t WriteCallback(char* data, size_t size, size_t nmemb, void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    const size_t total = size * nmemb;
    body->append(data, total);
    return total;
}

struct CurlEasyDeleter {
    void operator()(CURL* handle) const { curl_easy_cleanup(handle); }
};

struct CurlSlistDeleter {
    void operator()(curl_slist* list) const { curl_slist_free_all(list); }
};

} // namespace

HttpClient::HttpClient(std::string user_agent, uint32_t timeout_seconds)
    : user_agent_(std::move(user_agent)), timeout_seconds_(timeout_seconds) {}

HttpResponse HttpClient::Get(const std::string& url) const {
    HttpResponse response;

    std::unique_ptr<CURL, CurlEasyDeleter> curl(curl_easy_init());
    if (!curl) {
        response.error = "curl_easy_init failed";
        return response;
    }

    curl_slist* header_list = nullptr;
    header_list = curl_slist_append(header_list, "Accept: text/html,application/xhtml+xml");
    header_list = curl_slist_append(header_list, "Accept-Language: ru,en;q=0.9");
    std::unique_ptr<curl_slist, CurlSlistDeleter> headers(header_list);

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, user_agent_.c_str());

    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds_));
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 5L);

    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 3L);

    curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, "");

    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);

    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response.body);

    const CURLcode result = curl_easy_perform(curl.get());
    if (result != CURLE_OK) {
        response.body.clear();
        response.error = curl_easy_strerror(result);
        return response;
    }

    long status_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status_code);

    response.status_code = status_code;
    response.ok = status_code >= 200 && status_code < 300;
    if (!response.ok) {
        response.body.clear();
        response.error = "unexpected HTTP status " + std::to_string(status_code);
    }

    return response;
}

} // namespace habr::services::fetcher
