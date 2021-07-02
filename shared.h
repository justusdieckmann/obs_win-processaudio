//
// Created by Justus on 18.06.2021.
//

#ifndef OBS_STUDIO_OBJECT_NAMES_H
#define OBS_STUDIO_OBJECT_NAMES_H

#define AUDIO_CAPTURE_EVENT "AudioCaptureEvent"
#define AUDIO_CAPTURE_MUTEX "AudioCaptureMutex"
#define AUDIO_CAPTURE_SHARED_MEMORY "AudioCaptureMem"
#define AUDIO_CAPTURE_SHARED_MEMORY_SIZE 2048 * 8 * 4
#define AC_SHUTDOWN_EVENT "ACShutdown"
#define AC_EXIT_EVENT "ACExit"
#define AC_HEARTBEAT_EVENT "ACHeartbeat"

typedef struct {
    // Filled by Format (WAVEFORMATEX)
    WORD formatTag;
    DWORD nChannels;
    DWORD nSamplesPerSec;
    WORD wBitsPerSample;
    DWORD dwChannelMask;
    BOOL isFloat;
    GUID subFormat;
    // Size of data
    DWORD size;
    // Contains the actual data;
    uint8_t data[AUDIO_CAPTURE_SHARED_MEMORY_SIZE];
} mem_layout;

#endif //OBS_STUDIO_OBJECT_NAMES_H
