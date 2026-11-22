// SPDX-License-Identifier: GPL-3.0-only
// Windows: mc2.exe is a GUI program (no console window when it is started
// from the Start menu or Explorer). When it is started from a console
// (cmd, PowerShell, a script), write stdout / stderr there so -version,
// error messages and test output still show up.
#ifdef _WIN32
#include <windows.h>
#include <stdio.h>

void gos_AttachParentConsole()
{
    // Output already redirected to a file or pipe: keep it
    if (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_UNKNOWN)
        return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
}
#endif
