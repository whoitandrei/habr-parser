cmake_minimum_required(VERSION 3.16)
project(habr_digest CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(shared/cpp)
add_subdirectory(services/fetcher)
add_subdirectory(services/parser)
add_subdirectory(services/storage)
add_subdirectory(services/notifier)
add_subdirectory(services/web)
