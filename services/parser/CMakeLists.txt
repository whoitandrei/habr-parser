cmake_minimum_required(VERSION 3.16)
project(parser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET habr_shared)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../shared/cpp habr_shared_build)
endif()

find_package(LibXml2 REQUIRED)

add_executable(parser
    src/main.cpp
    src/config.cpp
    src/html_parser.cpp
    src/parser_service.cpp
)

target_link_libraries(parser PRIVATE
    habr_shared
    LibXml2::LibXml2
)

target_compile_options(parser PRIVATE -Wall -Wextra)
