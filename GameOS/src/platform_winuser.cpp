#ifndef PLATFORM_WINDOWS

#include<stdio.h>
#include<wctype.h>
#include<ctype.h> // tolower...

#include"platform_windows.h"
#include"platform_str.h"

// There is no native message box on this platform, so messages are written
// to stderr and the box is answered automatically.
//
// The answer must never be "retry": the game's missing-file prompts loop on
// MessageBox until the user cancels, and with nobody to insert a CD that loop
// would never end. So any box that offers a cancel/no button is answered with
// it, and plain OK boxes are answered with OK.
static int auto_answer_message_box(UINT type)
{
    switch (type & MB_TYPEMASK) {
        case MB_OKCANCEL:
        case MB_RETRYCANCEL:
        case MB_YESNOCANCEL:
            return IDCANCEL;
        case MB_YESNO:
            return IDNO;
        default:
            return IDOK;
    }
}

int GetSystemMetrics(int)
{
    // Only queried for SM_SWAPBUTTON; SDL already reports mouse buttons with
    // the OS-level swap applied, so "not swapped" is the correct answer here.
    return 0;
}

SHORT GetAsyncKeyState(int)
{
    // Unused: input is polled through gos_GetKeyStatus()/gos_GetMouseInfo().
    return 0;
}

int MessageBoxA(HWND, LPCSTR message, LPCSTR caption, UINT type)
{
    fprintf(stderr, "MSGBOX: %s : %s\n", caption ? caption : "", message ? message : "");
    return auto_answer_message_box(type);
}

int MessageBoxW(HWND, LPCWSTR message, LPCWSTR caption, UINT type)
{
    fprintf(stderr, "MSGBOX: %S : %S\n", caption ? caption : L"", message ? message : L"");
    return auto_answer_message_box(type);
}

LPSTR CharLowerA(LPSTR s)
{
    char* p = s;
    while ((*p = (char)tolower((unsigned char)*p))) p++;
    return s;
}

LPSTR CharUpperA(LPSTR s)
{
    char* p = s;
    while ((*p = (char)toupper((unsigned char)*p))) p++;
    return s;
}

LPWSTR CharLowerW(LPWSTR s)
{
    wchar_t* p = s;
    while ((*p = towlower( *p ))) p++;
    return s;
}
LPWSTR CharUpperW(LPWSTR s)
{
    wchar_t* p = s;
    while ((*p = towupper( *p ))) p++;
    return s;

}

#endif // PLATFORM_WINDOWS
