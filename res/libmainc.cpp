#include "libmain.h"
#include <cstdlib>

DLL_EXPORT void CDECL initStringResources()
{
    initStringResources__(std::getenv("MC2_LANGUAGE"));
}

DLL_EXPORT void CDECL initStringResourcesForLanguage(const char* language)
{
    initStringResources__(language);
}

DLL_EXPORT void CDECL freeStringResources()
{
    freeStringResources__();
}

DLL_EXPORT const char* CDECL getStringById(unsigned int id)
{
    return getStringById__(id);
}


