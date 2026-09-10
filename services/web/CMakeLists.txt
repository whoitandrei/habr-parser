cmake_minimum_required(VERSION 3.16)
project(web CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET habr_shared)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../shared/cpp habr_shared_build)
endif()

find_package(Libevent REQUIRED)

add_executable(web
    src/main.cpp
    src/config.cpp
    src/html_view.cpp
    src/web_service.cpp
)

target_link_libraries(web PRIVATE
    habr_shared
    libevent::extra
)

target_compile_options(web PRIVATE -Wall -Wextra)
