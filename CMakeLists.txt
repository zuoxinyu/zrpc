cmake_minimum_required(VERSION 3.22)
project(zrpc)

set(CMAKE_EXPORT_COMPILE_COMMANDS on)
set(CMAKE_CXX_STANDARD 17)
option(BUILD_EXAMPLES on)

find_package(ZeroMQ REQUIRED)
find_package(cppzmq CONFIG REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(nameof REQUIRED)
find_package(magic_enum REQUIRED)
#find_package(Boost COMPONENTS pfr)

add_definitions(-DZMQ_BUILD_DRAFT_API=1)
add_definitions(-DZMQ_HAVE_POLLER=1)

set(LIBS
    cppzmq-static
    libzmq-static
    fmt::fmt-header-only
    spdlog::spdlog_header_only
    magic_enum::magic_enum
    nameof::nameof
)

if (BUILD_EXAMPLES)
    add_executable(server examples/server.cc)
    target_include_directories(server PRIVATE include)
    target_link_libraries(server ${LIBS})
    
    add_executable(client examples/client.cc)
    target_include_directories(client PRIVATE include)
    target_link_libraries(client ${LIBS})
endif()
