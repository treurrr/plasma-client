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
find_package(Vulkan REQUIRED)

option(MCPP_SANITIZE "Build with address/undefined sanitizers" OFF)
if(MCPP_SANITIZE OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

function(mcpp_defaults target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endfunction()

find_program(MCPP_GLSLANG NAMES glslangValidator REQUIRED)

set(MCPP_SHADER_DIR ${CMAKE_BINARY_DIR}/generated/mcpp/gpu/shaders)
file(MAKE_DIRECTORY ${MCPP_SHADER_DIR})

function(mcpp_shader source symbol)
    set(spv ${CMAKE_BINARY_DIR}/${symbol}.spv)
    set(header ${MCPP_SHADER_DIR}/${symbol}.hpp)
    add_custom_command(
        OUTPUT ${header}
        COMMAND ${MCPP_GLSLANG} -V --target-env vulkan1.3 -o ${spv}
                ${CMAKE_SOURCE_DIR}/shaders/${source}
        COMMAND ${CMAKE_COMMAND} -E env python3
                ${CMAKE_SOURCE_DIR}/tools/spv_to_header.py ${spv} ${header} ${symbol}
        DEPENDS ${CMAKE_SOURCE_DIR}/shaders/${source}
                ${CMAKE_SOURCE_DIR}/tools/spv_to_header.py
        COMMENT "SPIR-V ${source}"
        VERBATIM)
    set(MCPP_SHADER_HEADERS ${MCPP_SHADER_HEADERS} ${header} PARENT_SCOPE)
endfunction()

mcpp_shader(triangle.vert TriangleVert)
mcpp_shader(triangle.frag TriangleFrag)
add_custom_target(mcpp_shaders DEPENDS ${MCPP_SHADER_HEADERS})

add_library(mcpp_net STATIC
    src/byte_buffer.cpp
    src/tcp_socket.cpp
    src/packet_io.cpp
    src/json.cpp
    src/nbt.cpp
    src/session.cpp
    src/translations.cpp
    src/world/chunk.cpp
    src/world/block_table.cpp
    src/world/mesher.cpp
    src/gpu/device.cpp
    src/gpu/offscreen.cpp
    src/image_png.cpp
    src/proto/packet_ids_gen.cpp
)
target_include_directories(mcpp_net PUBLIC include ${CMAKE_BINARY_DIR}/generated)
add_dependencies(mcpp_net mcpp_shaders)
target_link_libraries(mcpp_net PUBLIC ZLIB::ZLIB Threads::Threads Vulkan::Vulkan)
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
