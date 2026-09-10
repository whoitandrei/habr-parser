#include "config.hpp"
#include "storage_service.hpp"

#include "common/log.hpp"

int main() {
    habr::shared::common::SetServiceName("storage");

    try {
        habr::services::storage::StorageService service(
            habr::services::storage::LoadConfigFromEnv());
        return service.Run();
    } catch (const std::exception& e) {
        habr::shared::common::LogError() << "Fatal: " << e.what();
        return 1;
    }
}
