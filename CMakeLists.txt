cmake_minimum_required(VERSION 3.22)
project(zrpc)

set(CMAKE_EXPORT_COMPILE_COMMANDS on)
set(CMAKE_CXX_STANDARD 17)

find_package(cppzmq)
find_package(ZeroMQ)
find_package(spdlog)
find_package(fmt)
#find_package(msgpackc-cxx)

add_executable(server src/main.cc)
target_include_directories(server PRIVATE src)
message("zmq:" ${ZeroMQ_LIBRARIES})
message("spdlog:" spdlog::spdlog)
target_link_libraries(server spdlog::spdlog zmq)
