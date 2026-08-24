#include "pc_speech_input.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sapi.h>
#include <stdio.h>

static volatile LONG sListening;
static volatile LONG sResultReady;
static volatile LONG sCancel;
static char sResult[512];

static void store_result(const WCHAR* result) {
    if (result == NULL || result[0] == L'\0') return;
    int written = WideCharToMultiByte(CP_UTF8, 0, result, -1, sResult,
                                      (int)sizeof(sResult), NULL, NULL);
    if (written > 1) {
        MemoryBarrier();
        InterlockedExchange(&sResultReady, 1);
    }
}

static DWORD WINAPI recognition_worker(LPVOID parameter) {
    (void)parameter;
    ISpRecoContext* context = NULL;
    ISpRecoGrammar* grammar = NULL;
    HRESULT comResult = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool comInitialized = SUCCEEDED(comResult);
    HRESULT result = CoCreateInstance(CLSID_SpSharedRecoContext, NULL,
                                      CLSCTX_ALL, IID_ISpRecoContext,
                                      (void**)&context);
    if (SUCCEEDED(result)) result = context->SetNotifyWin32Event();
    if (SUCCEEDED(result)) {
        result = context->SetInterest(SPFEI(SPEI_RECOGNITION),
                                      SPFEI(SPEI_RECOGNITION));
    }
    if (SUCCEEDED(result)) result = context->CreateGrammar(1, &grammar);
    if (SUCCEEDED(result)) result = grammar->LoadDictation(NULL, SPLO_STATIC);
    if (SUCCEEDED(result)) result = grammar->SetDictationState(SPRS_ACTIVE);

    if (SUCCEEDED(result)) {
        HANDLE eventHandle = context->GetNotifyEventHandle();
        ULONGLONG deadline = GetTickCount64() + 15000ULL;
        bool complete = false;
        while (!complete && GetTickCount64() < deadline &&
               InterlockedCompareExchange(&sCancel, 0, 0) == 0) {
            DWORD remaining = (DWORD)(deadline - GetTickCount64());
            DWORD waitResult = WaitForSingleObject(
                eventHandle, remaining < 100 ? remaining : 100);
            if (waitResult == WAIT_TIMEOUT) continue;
            if (waitResult != WAIT_OBJECT_0) break;
            SPEVENT event = {};
            ULONG fetched = 0;
            while (context->GetEvents(1, &event, &fetched) == S_OK &&
                   fetched == 1) {
                if (event.eEventId == SPEI_RECOGNITION && event.lParam != 0) {
                    ISpRecoResult* recognition = (ISpRecoResult*)event.lParam;
                    WCHAR* text = NULL;
                    if (SUCCEEDED(recognition->GetText(
                            0, SP_GETWHOLEPHRASE, TRUE, &text, NULL))) {
                        store_result(text);
                        CoTaskMemFree(text);
                    }
                    recognition->Release();
                    event.lParam = 0;
                    complete = true;
                }
            }
        }
    }
    if (grammar != NULL) {
        grammar->SetDictationState(SPRS_INACTIVE);
        grammar->Release();
    }
    if (context != NULL) context->Release();
    if (comInitialized) CoUninitialize();
    InterlockedExchange(&sListening, 0);
    return 0;
}

extern "C" bool pc_speech_recognition_start(void) {
    if (InterlockedCompareExchange(&sListening, 1, 0) != 0) {
        InterlockedExchange(&sCancel, 1);
        return true;
    }
    InterlockedExchange(&sCancel, 0);
    InterlockedExchange(&sResultReady, 0);
    sResult[0] = '\0';
    HANDLE thread = CreateThread(NULL, 0, recognition_worker, NULL, 0, NULL);
    if (thread == NULL) {
        InterlockedExchange(&sListening, 0);
        return false;
    }
    CloseHandle(thread);
    return true;
}

extern "C" void pc_speech_recognition_cancel(void) {
    InterlockedExchange(&sCancel, 1);
}

extern "C" bool pc_speech_recognition_poll(char* text, size_t textSize) {
    if (text == NULL || textSize == 0 ||
        InterlockedExchange(&sResultReady, 0) == 0) return false;
    MemoryBarrier();
    snprintf(text, textSize, "%s", sResult);
    return text[0] != '\0';
}

extern "C" bool pc_speech_recognition_is_listening(void) {
    return InterlockedCompareExchange(&sListening, 0, 0) != 0;
}

#else
extern "C" bool pc_speech_recognition_start(void) { return false; }
extern "C" void pc_speech_recognition_cancel(void) { }
extern "C" bool pc_speech_recognition_poll(char* text, size_t textSize) {
    (void)text; (void)textSize; return false;
}
extern "C" bool pc_speech_recognition_is_listening(void) { return false; }
#endif
