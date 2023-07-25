cmake_minimum_required(VERSION 3.22)
project(zrpc)

set(CMAKE_EXPORT_COMPILE_COMMANDS on)
set(CMAKE_CXX_STANDARD 17)

find_package(ZeroMQ)
find_package(cppzmq)
find_package(spdlog)
find_package(fmt)
find_package(magic_enum)

add_definitions(-DZMQ_BUILD_DRAFT_API=1)
add_definitions(-DZMQ_HAVE_POLLER=1)

add_executable(server examples/server.cc)
target_include_directories(server PRIVATE src)
target_link_libraries(server libzmq-static spdlog::spdlog magic_enum::magic_enum)

add_executable(client examples/client.cc)
target_include_directories(client PRIVATE src)
target_link_libraries(client libzmq-static spdlog::spdlog magic_enum::magic_enum)
