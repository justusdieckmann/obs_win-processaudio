project(audio-hook)

find_package(Detours REQUIRED)

set(audio-hook_HEADERS
	audio-hook.h)

set(audio-hook_SOURCES
		audio-hook.cpp)

if(MSVC)
	add_compile_options("$<IF:$<CONFIG:Debug>,/MTd,/MT>")
endif()

add_library(audio-hook MODULE
	${audio-hook_SOURCES}
	${audio-hook_HEADERS})

target_include_directories(audio-hook PUBLIC
	${DETOURS_INCLUDE_DIR})

target_link_libraries(audio-hook
	${DETOURS_LIBRARIES}
	WindowsApp.lib)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_output_suffix "64")
else()
	set(_output_suffix "32")
endif()

set_target_properties(audio-hook
	PROPERTIES
		FOLDER "plugins/win-capture"
		OUTPUT_NAME "audio-hook${_output_suffix}")

install_obs_datatarget(audio-hook "obs-plugins/win-processaudio")
