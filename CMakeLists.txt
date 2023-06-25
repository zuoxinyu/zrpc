cmake_minimum_required(VERSION 3.22)
project(zrpc)

set(CMAKE_EXPORT_COMPILE_COMMANDS on)
set(CMAKE_CXX_STANDARD 17)

find_package(ZeroMQ)
find_package(cppzmq)
find_package(spdlog)
find_package(fmt)

add_executable(server examples/server.cc)
target_include_directories(server PRIVATE src)
target_link_libraries(server zmq spdlog::spdlog)

add_executable(client examples/client.cc)
target_include_directories(client PRIVATE src)
target_link_libraries(client zmq spdlog::spdlog)
