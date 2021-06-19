project(win-processaudio)

find_package(Detours REQUIRED)

set(win-processaudio_SOURCES
        plugin-main.c
        audiosource.c
        injector.cpp)

set(win-processaudio_HEADERS
        injector.hpp
        shared.h)

add_library(win-processaudio MODULE
        ${win-processaudio_SOURCES}
        ${win-processaudio_HEADERS})

target_include_directories(win-processaudio PUBLIC
        ${DETOURS_INCLUDE_DIR})

target_link_libraries(win-processaudio
        libobs
        ${DETOURS_LIBRARIES})

install_obs_plugin_with_data(win-processaudio data)

add_subdirectory(audiohook)