cmake_minimum_required(VERSION 3.16)
project(bili_history_delete CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(httplib CONFIG REQUIRED)

add_executable(bili_history_delete
    src/main.cpp
    src/http_client.cpp
    src/bili_api.cpp
    src/selection_server.cpp
)

target_include_directories(bili_history_delete PRIVATE src)

target_link_libraries(bili_history_delete PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
    httplib::httplib
)

if (MSVC)
    target_compile_options(bili_history_delete PRIVATE /utf-8)
endif()