#ifndef _STDLIB_WIN_H
#define _STDLIB_WIN_H

#ifdef PLATFORM_WINDOWS
	#include <stdlib.h>
	#include <malloc.h> // _alloca
#else
#include <alloca.h>

#include<stdio.h>
#include<stdlib.h> // itoa

#ifdef __cplusplus
extern "C" {
#endif

void _splitpath (const char*, char*, char*, char*, char*);

#define _alloca alloca

#ifdef __cplusplus
}
#endif
// Decimal only: every caller passes radix 10.
inline void _itoa(int value, char* str, int /*radix*/) { sprintf(str, "%d", value); }

#endif // PLATFORM_WINDOWS

#endif /* Not _STDLIB_WIN_H */
