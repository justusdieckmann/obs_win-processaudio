#include <obs-module.h>
#include <windows.h>
#include <stdio.h>
#include <util/platform.h>
#include <ks.h>
#include <ksmedia.h>
#include "injector.hpp"
#include "shared.h"
#include <audioengineendpoint.h>

typedef struct {
    const char *processname;
    float searchtime;
    HANDLE process;
    bool active;
    obs_source_t *src;
    HANDLE hook_capture;
    HANDLE hook_shutdown;
    HANDLE hook_exit;
    HANDLE hook_heartbeat;
    HANDLE mutex;
    HANDLE memory_handle;
    mem_layout *memory;
    HANDLE thread;
    BOOL shutdown;
} process_audio_source;


void closeAndEmptyHandle(HANDLE *handle) {
    CloseHandle(*handle);
    *handle = NULL;
}

void freeHook(process_audio_source *pas) {
    UnmapViewOfFile(pas->memory);
    closeAndEmptyHandle(&pas->memory_handle);
    closeAndEmptyHandle(&pas->hook_shutdown);
    closeAndEmptyHandle(&pas->hook_exit);
    closeAndEmptyHandle(&pas->hook_capture);
    closeAndEmptyHandle(&pas->mutex);
    closeAndEmptyHandle(&pas->thread);
    closeAndEmptyHandle(&pas->process);
    pas->active = FALSE;
    pas->shutdown = FALSE;
}

static const char *audiosource_name(void *type_data) {
    return obs_module_text("CaptureProcessAudio");
}

static void audiosource_destroy(void *data) {
    process_audio_source *pas = data;
    blog(LOG_INFO, "Destroy");

    if (pas->hook_shutdown) {
        blog(LOG_INFO, "Sending shutdown!");
        SetEvent(pas->hook_shutdown);
    }
}

static void audiosource_update(void *data, obs_data_t *settings) {
    process_audio_source *pas = data;
}

static void audiosource_save(void *data, obs_data_t *settings) {
    process_audio_source *pas = data;

    blog(LOG_INFO, "Save");

	if (pas->hook_shutdown != NULL) {
        pas->shutdown = TRUE;
	    SetEvent(pas->hook_shutdown);
	}

    const char* setting_process_name = obs_data_get_string(settings, "process_name");



	pas->processname = calloc(strlen(setting_process_name) + 1, sizeof(char));
    strcpy(pas->processname, setting_process_name);
    pas->searchtime = 5;
}

static void *audiosource_create(obs_data_t *settings, obs_source_t *source) {
    process_audio_source *context = bzalloc(sizeof(process_audio_source));
    context->src = source;

    audiosource_save(context, settings);

    return context;
}

static obs_properties_t *audiosource_get_properties(void *unused) {
    UNUSED_PARAMETER(unused);

    obs_properties_t *props = obs_properties_create();

    obs_properties_add_text(props, "process_name", "Process Name", OBS_TEXT_DEFAULT);

    return props;
}

static enum speaker_layout ConvertSpeakerLayout(DWORD layout, WORD channels) {
    switch (layout) {
        case KSAUDIO_SPEAKER_2POINT1:
            return SPEAKERS_2POINT1;
        case KSAUDIO_SPEAKER_SURROUND:
            return SPEAKERS_4POINT0;
        case (KSAUDIO_SPEAKER_SURROUND | SPEAKER_LOW_FREQUENCY): // OBS_KSAUDIO_SPEAKER_4POINT1
            return SPEAKERS_4POINT1;
        case KSAUDIO_SPEAKER_5POINT1_SURROUND:
            return SPEAKERS_5POINT1;
        case KSAUDIO_SPEAKER_7POINT1_SURROUND:
            return SPEAKERS_7POINT1;
    }

    return (enum speaker_layout) channels;
}

DWORD WINAPI audio_capture_thread(LPVOID data) {
    process_audio_source *pas = data;
    mem_layout temp;

    while (WaitForSingleObject(pas->hook_shutdown, 0) == WAIT_TIMEOUT && !pas->shutdown) {
        if (WaitForSingleObject(pas->hook_capture, 500) != WAIT_OBJECT_0) {
            continue;
        }

        if (WaitForSingleObject(pas->mutex, INFINITE) == WAIT_ABANDONED) {
            ReleaseMutex(pas->mutex);
            break;
        }

        DWORD size = pas->memory->size;
        memcpy(&temp, pas->memory, sizeof(mem_layout));
        ReleaseMutex(pas->mutex);

        DWORD nChannels = temp.nChannels;
        DWORD bytesperframe = nChannels * (temp.wBitsPerSample / 8);
        DWORD dwChannelMask = temp.dwChannelMask;

        if (bytesperframe == 0)
            continue;

        struct obs_source_audio data;
        data.data[0] = temp.data;
	    data.frames = size / bytesperframe;
        data.speakers = ConvertSpeakerLayout(dwChannelMask, nChannels);
        data.samples_per_sec = 48000;
        data.timestamp = os_gettime_ns();
        if (temp.isFloat) {
            data.format = AUDIO_FORMAT_FLOAT;
        } else {
            switch (temp.wBitsPerSample) {
                case 16:
                    data.format = AUDIO_FORMAT_16BIT;
                    break;
                case 8:
                    data.format = AUDIO_FORMAT_U8BIT;
                    break;
                case 32:
                    data.format = AUDIO_FORMAT_32BIT;
                default:
                    data.format = AUDIO_FORMAT_FLOAT;
            }
        }
        data.timestamp -= util_mul_div64(size / bytesperframe, 1000000000ULL,
                                         data.samples_per_sec);

        if (pas->active)
            obs_source_output_audio(pas->src, &data);
    }
    blog(LOG_INFO, "Exiting Capture Thread!");
    freeHook(pas);
    return 0;
}

void init_audiocapture(process_audio_source *pas, DWORD procID) {
    char newname[64];
    sprintf(newname, "%s/%lu", AUDIO_CAPTURE_EVENT, procID);
    pas->hook_capture = CreateEventA(NULL, FALSE, FALSE, newname);

    sprintf(newname, "%s/%lu", AC_SHUTDOWN_EVENT, procID);
    pas->hook_shutdown = CreateEventA(NULL, FALSE, FALSE, newname);

    sprintf(newname, "%s/%lu", AC_EXIT_EVENT, procID);
    pas->hook_exit = CreateEventA(NULL, FALSE, FALSE, newname);

    sprintf(newname, "%s/%lu", AC_HEARTBEAT_EVENT, procID);
    pas->hook_heartbeat = OpenEventA(EVENT_ALL_ACCESS, FALSE, newname);

    sprintf(newname, "%s/%lu", AUDIO_CAPTURE_MUTEX, procID);
    pas->mutex = CreateMutexA(NULL, FALSE, newname);

    sprintf(newname, "%s/%lu", AUDIO_CAPTURE_SHARED_MEMORY, procID);
    pas->memory_handle = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(mem_layout),
                                            newname);
    pas->memory = MapViewOfFile(pas->memory_handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(mem_layout));
    memset(pas->memory, 0, sizeof(mem_layout));

    pas->thread = CreateThread(NULL, 0, audio_capture_thread, pas, 0, NULL);
    pas->active = true;

    blog(LOG_INFO, "%s: Found Process!", pas->processname);
    pas->process = NULL;
    inject_hook(procID, &pas->process);
    blog(LOG_INFO, "%s: Injected Process!", pas->processname);
}

static void audiosource_tick(void *data, float seconds) {
    process_audio_source *pas = data;
    pas->searchtime += seconds;
    if (pas->searchtime > 1 || true) {
        pas->searchtime = 0;
        if (pas->thread == NULL && !pas->shutdown && !pas->active) {
            DWORD procID = findProcess(pas->processname);
            if (procID) {
                init_audiocapture(pas, procID);
            }
        } else {
            DWORD exitCode;
            if (!GetExitCodeProcess(pas->process, &exitCode)) {
                blog(LOG_INFO,"GetExitCodeProcess Failed: %lu", GetLastError());
            }
            if (exitCode != STILL_ACTIVE) {
                blog(LOG_INFO, "%s: Process aborted", pas->processname);
                SetEvent(pas->hook_shutdown);
            } else {
                blog(LOG_INFO, "%s: Still active", pas->processname);
            }
        }
    }
}

struct obs_source_info win_processaudio_source = {
        .id           = "processaudio_source",
        .type         = OBS_SOURCE_TYPE_INPUT,
        .output_flags = OBS_SOURCE_AUDIO,
        .get_name     = audiosource_name,
        .create       = audiosource_create,
        .destroy      = audiosource_destroy,
        .update       = audiosource_update,
        .get_properties = audiosource_get_properties,
        .icon_type    = OBS_ICON_TYPE_AUDIO_OUTPUT,
        .video_tick   = audiosource_tick,
        .save = audiosource_save
};
