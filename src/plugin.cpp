#include <windows.h>
#include "listerplugin.h"
#include "config.h"
#include "rtf_builder.h"

extern "C" {

HWND __stdcall ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
    return nullptr;
}

int __stdcall ListLoadNext(HWND ParentWin, HWND PluginWin, char* FileToLoad, int ShowFlags) {
    return LISTPLUGIN_ERROR;
}

void __stdcall ListCloseWindow(HWND ListWin) {}

void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    strncpy(DetectString, "EXT=\"MD\"", maxlen - 1);
}

int __stdcall ListSearchText(HWND ListWin, char* SearchString, int SearchParameter) {
    return LISTPLUGIN_ERROR;
}

int __stdcall ListSearchTextW(HWND ListWin, WCHAR* SearchString, int SearchParameter) {
    return LISTPLUGIN_ERROR;
}

int __stdcall ListPrint(HWND ListWin, char* FileToPrint, char* DefPrinter, int PrintFlags, RECT* Margins) {
    return LISTPLUGIN_ERROR;
}

int __stdcall ListSendCommand(HWND ListWin, int Command, int Parameter) {
    return LISTPLUGIN_ERROR;
}

} // extern "C"
