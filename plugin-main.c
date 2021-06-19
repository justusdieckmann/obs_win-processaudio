#include <obs-module.h>

/* Defines common functions (required) */
OBS_DECLARE_MODULE()

/* Implements common ini-based locale (optional) */
OBS_MODULE_USE_DEFAULT_LOCALE("win-processaudio", "en-US")

extern struct obs_source_info win_processaudio_source;

bool obs_module_load(void)
{
	obs_register_source(&win_processaudio_source);
	return true;
}
