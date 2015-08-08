#include "stdafx.h"

using std::wstring;

void print_error(DWORD err, const wstring &action) {
    wchar_t buffer[4096];
    if (FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        err,
        0,
        buffer,
        sizeof(buffer) / sizeof(buffer[0]),
        nullptr
        ) == 0) {
        wprintf_s(L"Failed to get error message: 0x%08x\n", GetLastError());
        return;
    }

    wprintf_s(L"Error while %s: %s\n", action.c_str(), buffer);
}
