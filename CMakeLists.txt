project(win-processaudio)

set(win-processaudio_SOURCES
        plugin-main.c
        audiosource.cpp
        injector.cpp)

set(win-processaudio_HEADERS
        injector.hpp
        audiosource.hpp
        shared.h)

add_library(win-processaudio MODULE
        ${win-processaudio_SOURCES}
        ${win-processaudio_HEADERS})

target_link_libraries(win-processaudio
        libobs)

install_obs_plugin_with_data(win-processaudio data)

add_subdirectory(audiohook)