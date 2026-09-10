#include "config.hpp"
#include "notifier_service.hpp"

#include "common/log.hpp"

int main() {
    habr::shared::common::SetServiceName("notifier");

    try {
        habr::services::notifier::NotifierService service(
            habr::services::notifier::LoadConfigFromEnv());
        return service.Run();
    } catch (const std::exception& e) {
        habr::shared::common::LogError() << "Fatal: " << e.what();
        return 1;
    }
}
