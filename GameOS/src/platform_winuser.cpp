#ifndef PLATFORM_WINDOWS

#include<stdio.h>
#include<wctype.h>
#include<ctype.h> // tolower...

#include"platform_windows.h"
#include"platform_str.h"

// Default MessageBox handler: messages are written to stderr and the box is
// answered automatically.
//
// The answer must never be "retry": the game's missing-file prompts loop on
// MessageBox until the user cancels, and with nobody to insert a CD that loop
// would never end. So any box that offers a cancel/no button is answered with
// it, and plain OK boxes are answered with OK.
int platform_auto_answer_message_box(UINT type)
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

static int stderr_message_box(const char* caption, const char* message, UINT type)
{
    fprintf(stderr, "MSGBOX: %s : %s\n", caption, message);
    return platform_auto_answer_message_box(type);
}

static platform_message_box_fn g_message_box = stderr_message_box;

void platform_set_message_box_handler(platform_message_box_fn handler)
{
    g_message_box = handler ? handler : stderr_message_box;
}

int MessageBoxA(HWND, LPCSTR message, LPCSTR caption, UINT type)
{
    return g_message_box(caption ? caption : "", message ? message : "", type);
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
