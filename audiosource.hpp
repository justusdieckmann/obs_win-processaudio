//
// Created by Justus on 02.07.2021.
//

#ifndef OBS_STUDIO_AUDIOSOURCE_HPP
#define OBS_STUDIO_AUDIOSOURCE_HPP

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

EXTERNC void win_processaudio_register_source();

#endif //OBS_STUDIO_AUDIOSOURCE_HPP
