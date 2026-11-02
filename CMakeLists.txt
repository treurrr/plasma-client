cmake_minimum_required(VERSION 3.25)
project(mc_cpp LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

option(MCPP_SANITIZE "Build with address/undefined sanitizers" OFF)
if(MCPP_SANITIZE OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

function(mcpp_defaults target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endfunction()

add_library(mcpp_net STATIC
    src/byte_buffer.cpp
    src/tcp_socket.cpp
    src/packet_io.cpp
    src/json.cpp
    src/nbt.cpp
    src/session.cpp
    src/translations.cpp
    src/world/chunk.cpp
    src/proto/packet_ids_gen.cpp
)
target_include_directories(mcpp_net PUBLIC include)
target_link_libraries(mcpp_net PUBLIC ZLIB::ZLIB Threads::Threads)
mcpp_defaults(mcpp_net)

add_executable(plodder-client src/tools/client_main.cpp src/features.cpp)
target_link_libraries(plodder-client PRIVATE mcpp_net)
mcpp_defaults(plodder-client)

add_executable(mc-ping src/tools/mc_ping.cpp)
target_link_libraries(mc-ping PRIVATE mcpp_net)
mcpp_defaults(mc-ping)

enable_testing()
add_executable(test_protocol tests/test_protocol.cpp)
target_link_libraries(test_protocol PRIVATE mcpp_net)
mcpp_defaults(test_protocol)
add_test(NAME protocol COMMAND test_protocol)
