cmake_minimum_required(VERSION 3.13)

project(webserver)

include_directories("./include")
add_executable(a.out ./src/main.cpp ./src/http_conn.cpp)
find_package(Threads REQUIRED)
target_link_libraries(a.out Threads::Threads) 