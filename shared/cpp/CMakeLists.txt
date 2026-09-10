if(TARGET habr_shared)
    return()
endif()

find_package(amqpcpp REQUIRED)
find_package(Libevent REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

find_package(OpenSSL REQUIRED)

add_library(habr_shared STATIC
    common/src/log.cpp
    common/src/time_util.cpp
    messaging/src/amqp_service.cpp
)

target_include_directories(habr_shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/messaging/include
)

target_link_libraries(habr_shared PUBLIC
    amqpcpp
    libevent::core
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    Threads::Threads
)

target_compile_features(habr_shared PUBLIC cxx_std_17)
target_compile_options(habr_shared PRIVATE -Wall -Wextra)
