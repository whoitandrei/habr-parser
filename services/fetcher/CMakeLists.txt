cmake_minimum_required(VERSION 3.16)
project(fetcher CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET habr_shared)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../shared/cpp habr_shared_build)
endif()

find_package(CURL REQUIRED)

add_executable(fetcher
    src/main.cpp
    src/config.cpp
    src/http_client.cpp
    src/fetcher.cpp
)

target_link_libraries(fetcher PRIVATE
    habr_shared
    CURL::libcurl
)

target_compile_options(fetcher PRIVATE -Wall -Wextra)
