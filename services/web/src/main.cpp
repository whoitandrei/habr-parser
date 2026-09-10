#include "config.hpp"
#include "web_service.hpp"

#include "common/log.hpp"

int main() {
    habr::shared::common::SetServiceName("web");

    try {
        habr::services::web::WebService service(habr::services::web::LoadConfigFromEnv());
        return service.Run();
    } catch (const std::exception& e) {
        habr::shared::common::LogError() << "Fatal: " << e.what();
        return 1;
    }
}
