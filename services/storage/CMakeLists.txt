cmake_minimum_required(VERSION 3.16)
project(storage CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET habr_shared)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../shared/cpp habr_shared_build)
endif()

find_package(SQLite3 REQUIRED)

add_executable(storage
    src/main.cpp
    src/config.cpp
    src/article_store.cpp
    src/storage_service.cpp
)

target_link_libraries(storage PRIVATE
    habr_shared
    SQLite::SQLite3
)

target_compile_options(storage PRIVATE -Wall -Wextra)
