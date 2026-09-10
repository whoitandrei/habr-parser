cmake_minimum_required(VERSION 3.16)
project(notifier CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET habr_shared)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../shared/cpp habr_shared_build)
endif()

add_executable(notifier
    src/main.cpp
    src/config.cpp
    src/digest_writer.cpp
    src/notifier_service.cpp
)

target_link_libraries(notifier PRIVATE habr_shared)

target_compile_options(notifier PRIVATE -Wall -Wextra)
