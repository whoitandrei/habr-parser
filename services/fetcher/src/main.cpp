#include "config.hpp"
#include "fetcher.hpp"

#include "common/log.hpp"

#include <curl/curl.h>

int main() {
    habr::shared::common::SetServiceName("fetcher");

    curl_global_init(CURL_GLOBAL_DEFAULT);

    int exit_code = 1;
    try {
        habr::services::fetcher::Fetcher service(habr::services::fetcher::LoadConfigFromEnv());
        exit_code = service.Run();
    } catch (const std::exception& e) {
        habr::shared::common::LogError() << "Fatal: " << e.what();
    }

    curl_global_cleanup();
    return exit_code;
}
