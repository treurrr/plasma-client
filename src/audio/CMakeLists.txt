# Sound. miniaudio 0.11.21 (github.com/mackron/miniaudio, tag 0.11.21) and
# stb_vorbis 1.22 (github.com/nothings/stb at 1ee679ca) are vendored in
# third_party/, both public domain or MIT, their licences at the end of each.
enable_language(C)

add_library(mcpp_audio_vendor STATIC stb_vorbis_impl.c miniaudio_impl.c)
target_include_directories(mcpp_audio_vendor SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(mcpp_audio_vendor PRIVATE -w)
target_link_libraries(mcpp_audio_vendor PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)

# The block sound table is compiled in, so the client never looks for it.
set(MCPP_BLOCK_SOUNDS_FILE ${CMAKE_SOURCE_DIR}/data/block_sounds.txt)
file(READ ${MCPP_BLOCK_SOUNDS_FILE} MCPP_BLOCK_SOUNDS)
configure_file(block_sounds_data.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/block_sounds_data.cpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MCPP_BLOCK_SOUNDS_FILE})
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/block_sounds_data.cpp
    PROPERTIES COMPILE_OPTIONS -Wno-overlength-strings)

add_library(mcpp_audio STATIC
    sounds.cpp
    block_sounds.cpp
    pcm.cpp
    mixer.cpp
    engine.cpp
    footsteps.cpp
    level_events.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/block_sounds_data.cpp)
target_include_directories(mcpp_audio PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(mcpp_audio PUBLIC mcpp_net mcpp_audio_vendor)
mcpp_defaults(mcpp_audio)

add_executable(plasma-soundlog soundlog_main.cpp)
target_link_libraries(plasma-soundlog PRIVATE mcpp_audio)
mcpp_defaults(plasma-soundlog)

target_link_libraries(plasma-client PRIVATE mcpp_audio)
target_link_libraries(test_sound PRIVATE mcpp_audio)
