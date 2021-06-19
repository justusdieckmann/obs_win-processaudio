//////////////////////////////////////////////////////////////////////////////
//
//  Detour Test Program (wrotei.cpp of wrotei.dll)
//
//  Microsoft Research Detours Package
//
//  Copyright (c) Microsoft Corporation.  All rights reserved.
//
//  An example dynamically detouring a function.
//
#include <cstdio>

//////////////////////////////////////////////////////////////////////////////
//
//  WARNING:
//
//  CINTERFACE must be defined so that the lpVtbl pointer is visible
//  on COM interfaces.  However, once we've defined it, we must use
//  coding conventions when accessing interface members, for example:
//      i->lpVtbl->Write
//  instead of the C++ syntax:
//      i->Write.
//  We must also pass the implicit "this" parameter explicitly:
//      i->lpVtbl->Write(i, pb, 0, NULL)
//  instead of the C++ syntax:
//      i->Write(pb, 0, NULL)
//

#define CINTERFACE
#include <ole2.h>
#include <detours.h>
#include <Audioclient.h>
#include <Mmdeviceapi.h>
#include <initguid.h>
#include <iostream>
#include <thread>
#include "../shared.h"


// 1CB9AD4C-DBFA-4c32-B178-C2F568A703B2
DEFINE_GUID(m_IID_IAudioClient, 0x1CB9AD4C, 0xDBFA, 0x4c32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);

// A95664D2-9614-4F35-A746-DE8DB63617E6
DEFINE_GUID(m_IID_IMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);

// F294ACFC-3146-4483-A7BF-ADDCA7C260E2
DEFINE_GUID(m_IID_IAudioRenderClient, 0xF294ACFC, 0x3146, 0x4483, 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2);

WORD bytesPerFrame = 0;

int OFFSET_IAudioRenderClient_Client = 0;

// HANDLES
HANDLE event;
HANDLE mutex;
HANDLE memory_handle;
mem_layout *memory;
HANDLE shutdown_event;
HANDLE exit_event;
HANDLE heartbeat_event;

bool exitViaOBS = false;

void onFormat(const WAVEFORMATEX *pFormat) {
    bytesPerFrame = pFormat->nChannels * (pFormat->wBitsPerSample / 8);
    WaitForSingleObject(mutex, INFINITY);
    printf("wBitsPerSample: %u, samplesPerSec: %lu\n", pFormat->wBitsPerSample, pFormat->nSamplesPerSec);
    memory->wBitsPerSample = pFormat->wBitsPerSample;
    memory->nSamplesPerSec = pFormat->nSamplesPerSec;
    memory->formatTag = pFormat->wFormatTag;
    memory->nChannels = pFormat->nChannels;
    if (memory->formatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE *formatextensible = (WAVEFORMATEXTENSIBLE *) pFormat;
        memory->dwChannelMask = formatextensible->dwChannelMask;
    } else {
        memory->dwChannelMask = NULL;
    }
    ReleaseMutex(mutex);
}

//////////////////////////////////////////////////////////////////////////////
HRESULT (STDMETHODCALLTYPE *RealInitialize)(IAudioClient * This,
                                            AUDCLNT_SHAREMODE  ShareMode,
                                            DWORD              StreamFlags,
                                            REFERENCE_TIME     hnsBufferDuration,
                                            REFERENCE_TIME     hnsPeriodicity,
                                            const WAVEFORMATEX *pFormat,
                                            LPCGUID            AudioSessionGuid
) = NULL;

HRESULT STDMETHODCALLTYPE MineInitialize(IAudioClient *This,
                                         AUDCLNT_SHAREMODE ShareMode,
                                         DWORD StreamFlags,
                                         REFERENCE_TIME hnsBufferDuration,
                                         REFERENCE_TIME hnsPeriodicity,
                                         const WAVEFORMATEX *pFormat,
                                         LPCGUID AudioSessionGuid) {

    onFormat(pFormat);

    return RealInitialize(This, ShareMode, StreamFlags, hnsBufferDuration, hnsPeriodicity, pFormat, AudioSessionGuid);
}

BYTE **yoppData;

HRESULT (STDMETHODCALLTYPE *RealGetBuffer)(IAudioRenderClient * This,
                                           UINT32  NumFramesRequested,
                                           BYTE    **ppData) = NULL;

HRESULT STDMETHODCALLTYPE MineGetBuffer(IAudioRenderClient * This,
                                        UINT32  NumFramesRequested,
                                        BYTE    **ppData) {
    if (bytesPerFrame == NULL && OFFSET_IAudioRenderClient_Client) {
        IAudioClient* audioClient = ((IAudioClient**)This)[OFFSET_IAudioRenderClient_Client];

        WAVEFORMATEX* pFormat = NULL;
        audioClient->lpVtbl->GetMixFormat(audioClient, &pFormat);
        onFormat(pFormat);

        printf("Get Format with magic (MAGIC!) (%i, %i, %lu, %lu, %i, %i, %i)\n",
               pFormat->wFormatTag,
               pFormat->nChannels,
               pFormat->nSamplesPerSec,
               pFormat->nAvgBytesPerSec,
               pFormat->nBlockAlign,
               pFormat->wBitsPerSample,
               pFormat->cbSize);
    }

    HRESULT hr = RealGetBuffer(This, NumFramesRequested, ppData);
    if (hr == S_OK) {
        yoppData = ppData;
    }
    return hr;
}

HRESULT (STDMETHODCALLTYPE *RealReleaseBuffer)(IAudioRenderClient * This,
                                               UINT32 NumFramesWritten,
                                               DWORD dwFlags
) = NULL;

HRESULT STDMETHODCALLTYPE MineReleaseBuffer(IAudioRenderClient * This,
                                            UINT32 NumFramesWritten,
                                            DWORD dwFlags) {

    if (!(dwFlags & AUDCLNT_BUFFERFLAGS_SILENT)) {
        WaitForSingleObject(mutex, INFINITE);
        DWORD realsize = NumFramesWritten * bytesPerFrame;
        memory->size = realsize;
        memcpy((uint8_t*) memory->data, *yoppData, realsize);
        ReleaseMutex(mutex);
        SetEvent(event);
    }

    return RealReleaseBuffer(This, NumFramesWritten, dwFlags);
}

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);

#define EXIT_ON_ERROR(hres)  \
    if (FAILED(hres)) { goto Exit; }

void setupIAudioHooks() {
    HRESULT hr;
    IMMDeviceEnumerator *pEnumerator = NULL;
    IMMDevice *pDevice = NULL;
    IAudioClient *pAudioClient = NULL;
    IAudioRenderClient *pRenderClient = NULL;
    WAVEFORMATEX *pwfx = NULL;
    REFERENCE_TIME hnsRequestedDuration = 34;

    // We couldn't call CoInitializeEx in DllMain,
    // so we detour the vtable entries here...?
    LONG error;

    hr = CoInitialize(NULL);
    EXIT_ON_ERROR(hr);

    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL,
                          CLSCTX_ALL, m_IID_IMMDeviceEnumerator, (void**)&pEnumerator);
    EXIT_ON_ERROR(hr);

    hr = pEnumerator->lpVtbl->GetDefaultAudioEndpoint(pEnumerator, eRender, eConsole, &pDevice);
    EXIT_ON_ERROR(hr);

    hr = pDevice->lpVtbl->Activate(pDevice,
                                   m_IID_IAudioClient, CLSCTX_ALL,
                                   NULL, (void**)&pAudioClient);
    EXIT_ON_ERROR(hr);

    hr = pAudioClient->lpVtbl->GetMixFormat(pAudioClient, &pwfx);
    EXIT_ON_ERROR(hr);

    hr = pAudioClient->lpVtbl->Initialize(pAudioClient,
                                          AUDCLNT_SHAREMODE_SHARED,
                                          0,
                                          hnsRequestedDuration,
                                          0,
                                          pwfx,
                                          NULL);
    EXIT_ON_ERROR(hr);

    hr = pAudioClient->lpVtbl->GetService(pAudioClient, m_IID_IAudioRenderClient, (void**)&pRenderClient);


    for (int i = 1; i < 100; i++) {
        if (((IAudioClient**) pRenderClient)[i] == pAudioClient) {
            OFFSET_IAudioRenderClient_Client = i;
            std::cout << "Yay!!: " << i << std::endl;
        }
    }

    if (OFFSET_IAudioRenderClient_Client == 0) {
        std::cout << "No Luck finding Offset" << std::endl;
    }

    EXIT_ON_ERROR(hr);

    // Apply the detour to the vtable.
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    if (pAudioClient != NULL) {
        RealInitialize = pAudioClient->lpVtbl->Initialize;
        DetourAttach(&(PVOID&) RealInitialize, MineInitialize);
    }

    if (pRenderClient != NULL) {
        RealGetBuffer = pRenderClient->lpVtbl->GetBuffer;
        DetourAttach(&(PVOID&) RealGetBuffer, MineGetBuffer);
        RealReleaseBuffer = pRenderClient->lpVtbl->ReleaseBuffer;
        DetourAttach(&(PVOID&) RealReleaseBuffer, MineReleaseBuffer);
    }
    error = DetourTransactionCommit();

    if (error == NO_ERROR) {
        printf("Detours: Detoured IAudioRenderClient::GetBuffer() and ::ReleaseBuffer(), IAudioClient::Initialize().\n");
    } else {
        printf("Detours: Error detouring: %ld\n", error);
    }

    fflush(stdout);

    Exit:
    if (pRenderClient != NULL) {
        pRenderClient->lpVtbl->Release(pRenderClient);
        pAudioClient = NULL;
    }

    if (pAudioClient != NULL) {
        pAudioClient->lpVtbl->Release(pAudioClient);
        pAudioClient = NULL;
    }

    if (pDevice != NULL) {
        pDevice->lpVtbl->Release(pDevice);
        pDevice = NULL;
    }

    if (pEnumerator != NULL) {
        pEnumerator->lpVtbl->Release(pEnumerator);
        pEnumerator = NULL;
    }
}

void initHandles(DWORD procID) {
    char newname[64];
    sprintf(newname, "%s/%lu", AUDIO_CAPTURE_EVENT, procID);
    event = OpenEventA(EVENT_ALL_ACCESS, FALSE, newname);

    sprintf(newname, "%s/%lu", AUDIO_CAPTURE_MUTEX, procID);
    mutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, newname);

    sprintf(newname, "%s/%lu", AUDIO_CAPTURE_SHARED_MEMORY, procID);
    memory_handle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, newname);
    memory = (mem_layout*) MapViewOfFile(memory_handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(mem_layout));

    sprintf(newname, "%s/%lu", AC_SHUTDOWN_EVENT, procID);
    shutdown_event = OpenEventA(EVENT_ALL_ACCESS, FALSE, newname);

    sprintf(newname, "%s/%lu", AC_EXIT_EVENT, procID);
    exit_event = OpenEventA(EVENT_ALL_ACCESS, FALSE, newname);

    sprintf(newname, "%s/%lu", AC_HEARTBEAT_EVENT, procID);
    heartbeat_event = OpenEventA(EVENT_ALL_ACCESS, FALSE, newname);
}

HINSTANCE hinstance;
HANDLE waitingThread;
HANDLE heartbeatThread;

DWORD WINAPI waitForShutdown(void *data) {
    std::cout << "Wait for Shutdown" << std::endl;
    WaitForSingleObject(shutdown_event, INFINITE);
    std::cout << "Shutdown!" << std::endl;
    exitViaOBS = true;
    waitingThread = nullptr;
    FreeLibraryAndExitThread(hinstance, 0);
}

[[noreturn]] DWORD WINAPI respondHeartbeat(void *data) {
    while (true) {
        if (WaitForSingleObject(heartbeat_event, 500) == WAIT_TIMEOUT) {
            continue;
        }
        SetEvent(heartbeat_event);
    }
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved) {
    hinstance = hinst;
    (void)hinst;
    (void)reserved;

    if (DetourIsHelperProcess()) {
        return TRUE;
    }

    if (dwReason == DLL_PROCESS_ATTACH) {
        AllocConsole();
        freopen("CONOUT$", "w", stdout);

        initHandles(GetCurrentProcessId());
        setupIAudioHooks();

        waitingThread = CreateThread(NULL, 0, waitForShutdown, NULL, 0, NULL);
        // heartbeatThread = CreateThread(NULL, 0, respondHeartbeat, NULL, 0, NULL);
        fflush(stdout);
    } else if (dwReason == DLL_PROCESS_DETACH) {
        SetEvent(exit_event);
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        if (RealInitialize != NULL) {
            DetourDetach(&(PVOID&) RealInitialize, (PVOID) MineInitialize);
        }
        if (RealGetBuffer != NULL) {
            DetourDetach(&(PVOID&) RealGetBuffer, (PVOID) MineGetBuffer);
        }
        if (RealReleaseBuffer != NULL) {
            DetourDetach(&(PVOID&) RealReleaseBuffer, (PVOID) MineReleaseBuffer);
        }
        LONG error = DetourTransactionCommit();

        if (error == NO_ERROR) {
            printf("Detours: Detached!\n");
        } else {
            printf("Detours: Error Detaching: %ld\n", error);
        }

        if (waitingThread) {
            TerminateThread(waitingThread, 0);
        }
        if (heartbeatThread) {
            TerminateThread(heartbeatThread, 0);
        }

        printf("\n\n(THIS IS EXITED, YOU CAN CLOSE THE CONSOLE)\n");
        fflush(stdout);

        FreeConsole();
    }
    return TRUE;
}