#pragma once

#include <windows.h>
#include "listerplugin.h"

// WLX Unicode entry points
extern "C" {
    HWND __stdcall ListLoadW(HWND ParentWin, wchar_t* FileToLoad, int ShowFlags);
    int __stdcall ListLoadNextW(HWND ParentWin, HWND PluginWin, wchar_t* FileToLoad, int ShowFlags);
    void __stdcall ListCloseWindow(HWND ListWin);
    void __stdcall ListGetDetectString(char* DetectString, int maxlen);
    int __stdcall ListSendCommand(HWND ListWin, int Command, int Parameter);
    void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps);
}
