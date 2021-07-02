#include <obs-module.h>
#include "audiosource.hpp"

/* Defines common functions (required) */
OBS_DECLARE_MODULE()

/* Implements common ini-based locale (optional) */
OBS_MODULE_USE_DEFAULT_LOCALE("win-processaudio", "en-US")

bool obs_module_load(void)
{
	win_processaudio_register_source();
	return true;
}
