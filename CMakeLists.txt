cmake_minimum_required(VERSION 3.10)

project(tiny_webserver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${PROJECT_SOURCE_DIR}/include)

file(GLOB_RECURSE SERVER_SOURCES
    ${PROJECT_SOURCE_DIR}/src/*.cpp
)

add_executable(tiny_webserver ${SERVER_SOURCES})

target_compile_options(tiny_webserver PRIVATE -Wall -Wextra -Wpedantic)

target_link_libraries(tiny_webserver pthread)
