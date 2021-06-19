//
// Created by Justus on 18.06.2021.
//

#ifndef OBS_STUDIO_INJECTOR_HPP
#define OBS_STUDIO_INJECTOR_HPP

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

EXTERNC DWORD findProcess(const char*);

EXTERNC bool inject_hook(DWORD procID, HANDLE* handle);

#endif //OBS_STUDIO_INJECTOR_HPP
