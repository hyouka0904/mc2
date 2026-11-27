#include "gameos.hpp"

#ifdef LINUX_BUILD
#include <cstdarg>
#else
#include <windows.h>
#endif

#include <cstdio>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include "utils/string_utils.h"

#ifdef _DEBUG
static int enter_debugger = 1;
#else
static int enter_debugger = 0;
#endif

static const int MAX_LOG_LINE = 1024;

static void output_debug_text(const char* text)
{
#ifdef LINUX_BUILD
    fprintf(stderr, "%s", text);
#else
	OutputDebugString(text);
#endif

}

static void logmsg(const char* header, const char* fmt, va_list ap) 
{
	assert(fmt);
	if (!strlen(fmt)) return;

	char				text[MAX_LOG_LINE] = {0};

    size_t header_len = strlen(header);

    sprintf(text, "%s: ", header);
	vsnprintf(text + header_len, MAX_LOG_LINE - header_len - 1, fmt, ap);

	size_t len = strlen(text);
	text[len] = '\0';
    output_debug_text(text);
}

// those are game specific callbacks (should move them outside to game)
int __cdecl InternalFunctionStop( const char* fmt, ... )
{
	assert(fmt);
	va_list	ap;

    va_start(ap, fmt);
    logmsg("STOP", fmt, ap);
    va_end(ap);

    //exit(1);
    return enter_debugger;
}

int __cdecl InternalFunctionStop( const char* Message, const char* value)
{
    fprintf(stderr, Message, value);
    return enter_debugger;
}

int __cdecl InternalFunctionPause( const char* fmt, ... )
{
	assert(fmt);
	va_list	ap;

    va_start(ap, fmt);
    logmsg("PAUSE", fmt, ap);
    va_end(ap);

    // A PAUSE is a warning the game carries on from (e.g. a quick save from
    // another build): unattended runs (MC2_NO_DIALOGS) must not stop on it
    // in Debug builds.
    return enter_debugger && !getenv("MC2_NO_DIALOGS");

}
void __cdecl InternalFunctionSpew( const char* Group, const char* fmt, ... )
{
	assert(fmt);
	va_list	ap;

    char buf[256];
    StringFormat(buf, sizeof(buf)-1, "SPEW: <%s>", Group ? Group : "");

    va_start(ap, fmt);
    logmsg(buf, fmt, ap);
    va_end(ap);
}

// Declared in gameos.hpp; set by callers that pass gos_ErrorFileLine.
char* gosErrorFile = NULL;
DWORD gosErrorLine = 0;

int __stdcall ErrorHandler( int Flags, const char* Text )
{
    const bool message_only = (Flags & gos_ErrorMessage) != 0;
    const char* kind = message_only ? "REPORT" :
                       (Flags & gos_ErrorAssert) ? "ASSERT" :
                       (Flags & gos_ErrorVerify) ? "VERIFY" : "ERROR";
    if ((Flags & gos_ErrorFileLine) && gosErrorFile)
        fprintf(stderr, "%s: %s (%s:%u)\n", kind, Text ? Text : "", gosErrorFile, (unsigned)gosErrorLine);
    else
        fprintf(stderr, "%s: %s\n", kind, Text ? Text : "");

    // gos_ErrorMessage means "display without a stop", so never break into
    // the debugger for it; everything else follows the global setting.
    return message_only ? 0 : enter_debugger;
}
