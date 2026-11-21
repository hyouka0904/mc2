#ifndef PLATFORM_WINDOWS

#include "platform_windef.h"
#include "platform_mmsystem.h"
#include "platform_clock.h"

DWORD timeGetTime()
{
    // milliseconds, wrapping like the Win32 function
    return (DWORD)(platform_clock::now_ns() / 1000000u);
}

#endif // PLATFORM_WINDOWS
