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
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
# Optional: without it the client still renders to PNG, it just has no window.
find_package(glfw3 QUIET)

# Off by default on purpose: a build that nobody configured must be the one
# that checks ownership. Turning this on only removes the gate for local
# testing against an offline-mode server; it never ships.
option(MCPP_DEV_OFFLINE "Allow joining without a verified Microsoft account" OFF)
if(MCPP_DEV_OFFLINE)
    add_compile_definitions(MCPP_DEV_OFFLINE)
    message(WARNING "MCPP_DEV_OFFLINE: this build joins servers without checking game ownership")
endif()

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
mcpp_shader(world.vert WorldVert)
mcpp_shader(world.frag WorldFrag)
mcpp_shader(text.vert TextVert)
mcpp_shader(text.frag TextFrag)
mcpp_shader(sky.vert SkyVert)
mcpp_shader(sky.frag SkyFrag)
mcpp_shader(outline.vert OutlineVert)
mcpp_shader(outline.frag OutlineFrag)
mcpp_shader(ui.vert UiVert)
mcpp_shader(ui.frag UiFrag)
add_custom_target(mcpp_shaders DEPENDS ${MCPP_SHADER_HEADERS})

if(glfw3_FOUND)
    set(MCPP_WINDOW_SOURCES src/gpu/window.cpp)
endif()

add_library(mcpp_net STATIC
    src/byte_buffer.cpp
    src/crypto.cpp
    src/tcp_socket.cpp
    src/packet_io.cpp
    src/json.cpp
    src/nbt.cpp
    src/session.cpp
    src/session_world.cpp
    src/session_entities.cpp
    src/session_player.cpp
    src/session_ui.cpp
    src/session_sound.cpp
    src/session_data.cpp
    src/status.cpp
    src/assets/object_store.cpp
    src/registries.cpp
    src/translations.cpp
    src/auth.cpp
    src/world/chunk.cpp
    src/world/block_table.cpp
    src/world/mesher.cpp
    src/world/physics.cpp
    src/world/light.cpp
    src/world/atlas.cpp
    src/world/biomes.cpp
    src/gpu/device.cpp
    src/gpu/offscreen.cpp
    src/gpu/world_renderer.cpp
    src/gpu/sky.cpp
    src/gpu/outline.cpp
    src/gpu/text.cpp
    src/gpu/ui.cpp
    ${MCPP_WINDOW_SOURCES}
    src/image_png.cpp
    src/image_png_read.cpp
    src/options.cpp
    src/keybinds.cpp
    src/ui/text_component.cpp
    src/ui/gui_graphics.cpp
    src/ui/layout.cpp
    src/ui/items.cpp
    src/ui/container.cpp
    src/ui/screen.cpp
    src/ui/widgets.cpp
    src/ui/server_list.cpp
    src/proto/packet_ids_gen.cpp
)
target_include_directories(mcpp_net PUBLIC include ${CMAKE_BINARY_DIR}/generated)
add_dependencies(mcpp_net mcpp_shaders)
target_link_libraries(mcpp_net PUBLIC ZLIB::ZLIB Threads::Threads Vulkan::Vulkan CURL::libcurl OpenSSL::Crypto)
if(glfw3_FOUND)
    target_link_libraries(mcpp_net PUBLIC glfw)
    target_compile_definitions(mcpp_net PUBLIC MCPP_HAVE_WINDOW)
else()
    message(STATUS "glfw3 not found: building without the window path")
endif()
mcpp_defaults(mcpp_net)

# The interactive client needs a window; the subcommands in client_main.cpp do not.
if(glfw3_FOUND)
    set(MCPP_PLAY_SOURCES
        src/client/play.cpp
        src/client/world.cpp
        src/client/entities.cpp
        src/client/player.cpp
        src/client/screens.cpp
        src/client/sound.cpp)
endif()
add_executable(plasma-client src/tools/client_main.cpp src/features.cpp src/ui/menu.cpp
    src/client/common.cpp ${MCPP_PLAY_SOURCES})
target_include_directories(plasma-client PRIVATE src)
target_link_libraries(plasma-client PRIVATE mcpp_net)
mcpp_defaults(plasma-client)

add_executable(mc-ping src/tools/mc_ping.cpp)
target_link_libraries(mc-ping PRIVATE mcpp_net)
mcpp_defaults(mc-ping)

enable_testing()
# One line per test: tests/test_<area>.cpp becomes test_<area>.
function(mcpp_test area)
    add_executable(test_${area} tests/test_${area}.cpp)
    target_link_libraries(test_${area} PRIVATE mcpp_net)
    mcpp_defaults(test_${area})
    add_test(NAME ${area} COMMAND test_${area})
endfunction()

mcpp_test(protocol)
mcpp_test(packet_ids)
mcpp_test(assets)
mcpp_test(ui)
mcpp_test(jmath)
