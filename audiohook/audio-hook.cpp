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
#include <inspectable.h>
#include <AudioEngineEndpoint.h>
#include "uuids.h"


// 1CB9AD4C-DBFA-4c32-B178-C2F568A703B2
DEFINE_GUID(m_IID_IAudioClient, 0x1CB9AD4C, 0xDBFA, 0x4c32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);

// A95664D2-9614-4F35-A746-DE8DB63617E6
DEFINE_GUID(m_IID_IMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);

// F294ACFC-3146-4483-A7BF-ADDCA7C260E2
DEFINE_GUID(m_IID_IAudioRenderClient, 0xF294ACFC, 0x3146, 0x4483, 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2);

// AF86E2E0-B12D-4C6A-9C5A-D7AA65101E90
DEFINE_GUID(m_IID_IInspectable, 0xAF86E2E0, 0xB12D, 0x4C6A, 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90);

// 30A99515-1527-4451-AF9F-00C5F0234DAF
DEFINE_GUID(m_IID_IAudioEndpoint, 0x30A99515, 0x1527, 0x4451, 0xAF, 0x9F, 0x00, 0xC5, 0xF0, 0x23, 0x4D, 0xAF);

WORD bytesPerFrame = 0;

int OFFSET_IAudioRenderClient_Client = 0;

// HANDLES
HANDLE event;
HANDLE mutex;
HANDLE memory_handle;
mem_layout *memory;
HANDLE shutdown_event;
HANDLE exit_event;

bool exitViaOBS = false;
bool setFormat = false;

void onFormat(const WAVEFORMATEX *pFormat) {
    bytesPerFrame = pFormat->nChannels * (pFormat->wBitsPerSample / 8);

    WaitForSingleObject(mutex, INFINITY);
    memory->wBitsPerSample = pFormat->wBitsPerSample;
    memory->nSamplesPerSec = pFormat->nSamplesPerSec;
    memory->formatTag = pFormat->wFormatTag;
    memory->nChannels = pFormat->nChannels;
    if (memory->formatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE *formatextensible = (WAVEFORMATEXTENSIBLE *) pFormat;
        memory->dwChannelMask = formatextensible->dwChannelMask;
        memory->isFloat = formatextensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT || formatextensible->SubFormat == MEDIASUBTYPE_IEEE_FLOAT;
        memory->subFormat = formatextensible->SubFormat;

        LPOLESTR string;
        StringFromIID(formatextensible->SubFormat, &string);
        printf("bitsPerSample: %u, samplesPerSec: %lu, formatTag: %u, channels: %lu, channelMask: %lu, subformat: %ls\n",
               pFormat->wBitsPerSample, pFormat->nSamplesPerSec, memory->formatTag, memory->nChannels, memory->dwChannelMask, string);
        CoTaskMemFree(string);
    } else {
        memory->isFloat = pFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        memory->dwChannelMask = NULL;
        memset(&memory->subFormat, 0, sizeof(GUID));
        printf("bitsPerSample: %u, samplesPerSec: %lu, formatTag: %u, channels: %lu, channelMask: %lu\n",
               pFormat->wBitsPerSample, pFormat->nSamplesPerSec, memory->formatTag, memory->nChannels, memory->dwChannelMask);
    }
    ReleaseMutex(mutex);
    setFormat = true;
}

void guessTheFormat(IAudioRenderClient* client) {
    IInspectable* pInspectable;
    HRESULT hr = client->lpVtbl->QueryInterface(client, m_IID_IInspectable, (void**) &pInspectable);

    if (pInspectable) {
        pInspectable->lpVtbl->Release(pInspectable);
    }
    typedef HRESULT (*myfunc)(void **, REFIID, IAudioEndpoint**);

#ifdef _WIN64
    myfunc** func = *(myfunc***) &client[((int)hr < 0) * 2 + 0xc];

    int offset = ((int)hr < 0) * 2 + 0xc;
    IUnknown *iUnknown = ((IUnknown**) client)[offset];
#else
    size_t pRenderClientAsInt = *(size_t*)&client;
    myfunc **func = *(myfunc ***) (pRenderClientAsInt + 0x34);

    int offset = 0x34;

    //IUnknown *iUnknown = ((IUnknown**) client)[offset];
    IUnknown *iUnknown = *(IUnknown**) (pRenderClientAsInt + 0x34);
#endif

    IAudioEndpoint* pAudioEndpoint;

    iUnknown->lpVtbl->QueryInterface(iUnknown, m_IID_IAudioEndpoint, (void**) &pAudioEndpoint);

    WAVEFORMATEX* waveformatex;
    pAudioEndpoint->lpVtbl->GetFrameFormat(pAudioEndpoint, &waveformatex);
    onFormat(waveformatex);
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

    printf("Initialize: %x, %lx\n", ShareMode, StreamFlags);
    onFormat(pFormat);

    return RealInitialize(This, ShareMode, StreamFlags, hnsBufferDuration, hnsPeriodicity, pFormat, AudioSessionGuid);
}

BYTE *pAudioBuffer = nullptr;

HRESULT (STDMETHODCALLTYPE *RealGetBuffer)(IAudioRenderClient * This,
                                           UINT32  NumFramesRequested,
                                           BYTE    **ppData) = NULL;

HRESULT STDMETHODCALLTYPE MineGetBuffer(IAudioRenderClient * This,
                                        UINT32  NumFramesRequested,
                                        BYTE    **ppData) {

    if (!setFormat) {
        printf("Format by MAGIC: ");
        guessTheFormat(This);
    }

    HRESULT hr = RealGetBuffer(This, NumFramesRequested, ppData);
    if (hr == S_OK) {
        pAudioBuffer = *ppData;
    } else {
        printf("GetBuffer failed: %lx", hr);
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

    if (!(dwFlags & AUDCLNT_BUFFERFLAGS_SILENT) && setFormat && pAudioBuffer != nullptr) {
        WaitForSingleObject(mutex, INFINITE);
        DWORD realsize = NumFramesWritten * bytesPerFrame;
        if (realsize > AUDIO_CAPTURE_SHARED_MEMORY_SIZE) {
            realsize = AUDIO_CAPTURE_SHARED_MEMORY_SIZE;
            printf("WARNING: Buffer exceeds max size: %lu", realsize);
        }
        memory->size = realsize;
        memcpy((uint8_t*) memory->data, pAudioBuffer, realsize);
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
    if (hr != S_OK) {
        printf("Cannot CoCreateInstance\n");
    }
    EXIT_ON_ERROR(hr);

    hr = pEnumerator->lpVtbl->GetDefaultAudioEndpoint(pEnumerator, eRender, eConsole, &pDevice);
    if (hr != S_OK) {
        printf("Cannot GetDefaultAudioEntpoint\n");
    }
    EXIT_ON_ERROR(hr);

    hr = pDevice->lpVtbl->Activate(pDevice,
                                   m_IID_IAudioClient, CLSCTX_ALL,
                                   NULL, (void**)&pAudioClient);
    if (hr != S_OK) {
        printf("Cannot Activate\n");
    }
    EXIT_ON_ERROR(hr);

    hr = pAudioClient->lpVtbl->GetMixFormat(pAudioClient, &pwfx);
    if (hr != S_OK) {
        printf("Cannot GetMixFormat\n");
    }
    EXIT_ON_ERROR(hr);

    hr = pAudioClient->lpVtbl->Initialize(pAudioClient,
                                          AUDCLNT_SHAREMODE_SHARED,
                                          0,
                                          hnsRequestedDuration,
                                          0,
                                          pwfx,
                                          NULL);
    if (hr != S_OK) {
        printf("Cannot Initialize2\n");
    }
    EXIT_ON_ERROR(hr);

    hr = pAudioClient->lpVtbl->GetService(pAudioClient, m_IID_IAudioRenderClient, (void**)&pRenderClient);
    if (hr != S_OK) {
        printf("Cannot GetService\n");
    }

    for (int i = 1; i < 100; i++) {
        if (((IAudioClient**) pRenderClient)[i] == pAudioClient) {
            OFFSET_IAudioRenderClient_Client = i;
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
}

HINSTANCE hinstance;
HANDLE waitingThread;

DWORD WINAPI waitForShutdown(void *data) {
    std::cout << "Wait for Shutdown" << std::endl;
    WaitForSingleObject(shutdown_event, INFINITE);
    std::cout << "Shutdown!" << std::endl;
    exitViaOBS = true;
    waitingThread = nullptr;
    FreeLibraryAndExitThread(hinstance, 0);
}

void freeHook() {
    CloseHandle(event);
    CloseHandle(mutex);
    UnmapViewOfFile(memory);
    CloseHandle(memory_handle);
    CloseHandle(shutdown_event);
    CloseHandle(exit_event);
}

void initFilelog() {
    TCHAR filepath[MAX_PATH];

    char *appdata = getenv("APPDATA");
    char buffer[MAX_PATH];

    GetModuleFileNameW(NULL, filepath, MAX_PATH);

    wchar_t *filename = wcsrchr(filepath, '\\');

    snprintf(buffer, MAX_PATH, "%s/obs-studio/logs/win-processlog_%ls.txt", appdata, (filename + 1));
    freopen(buffer, "w", stdout);
    setbuf(stdout, nullptr);
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved) {
    hinstance = hinst;
    (void)hinst;
    (void)reserved;

    if (DetourIsHelperProcess()) {
        return TRUE;
    }

    if (dwReason == DLL_PROCESS_ATTACH) {
        initFilelog();
        initHandles(GetCurrentProcessId());

        setupIAudioHooks();

        waitingThread = CreateThread(NULL, 0, waitForShutdown, NULL, 0, NULL);

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

        freeHook();

        fflush(stdout);
        fclose(stdout);
    }
    return TRUE;
}
