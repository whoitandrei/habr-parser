#include "config.hpp"
#include "html_parser.hpp"
#include "parser_service.hpp"

#include "common/log.hpp"

int main() {
    habr::shared::common::SetServiceName("parser");
    habr::services::parser::InitHtmlParserLibrary();

    int exit_code = 1;
    try {
        habr::services::parser::ParserService service(
            habr::services::parser::LoadConfigFromEnv());
        exit_code = service.Run();
    } catch (const std::exception& e) {
        habr::shared::common::LogError() << "Fatal: " << e.what();
    }

    habr::services::parser::ShutdownHtmlParserLibrary();
    return exit_code;
}
