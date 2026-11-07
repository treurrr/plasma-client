# Sound. miniaudio 0.11.21 (github.com/mackron/miniaudio, tag 0.11.21) and
# stb_vorbis 1.22 (github.com/nothings/stb at 1ee679ca) are vendored in
# third_party/, both public domain or MIT, their licences at the end of each.
enable_language(C)

add_library(mcpp_audio_vendor STATIC stb_vorbis_impl.c miniaudio_impl.c)
target_include_directories(mcpp_audio_vendor SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(mcpp_audio_vendor PRIVATE -w)
target_link_libraries(mcpp_audio_vendor PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)

add_library(mcpp_audio STATIC
    sounds.cpp
    block_sounds.cpp
    pcm.cpp
    decoder.cpp
    mixer.cpp
    engine.cpp
    footsteps.cpp
    level_events.cpp
    ambience.cpp
    voices.cpp)
target_include_directories(mcpp_audio PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(mcpp_audio PUBLIC mcpp_net mcpp_audio_vendor)
mcpp_defaults(mcpp_audio)

add_executable(plasma-soundlog soundlog_main.cpp)
target_link_libraries(plasma-soundlog PRIVATE mcpp_audio)
mcpp_defaults(plasma-soundlog)

target_link_libraries(plasma-client PRIVATE mcpp_audio)
target_link_libraries(test_sound PRIVATE mcpp_audio)
