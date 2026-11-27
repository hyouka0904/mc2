#include "gameos.hpp"
#include "strres.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>

// SDL's loader wraps dlopen / LoadLibrary, so one code path serves every
// platform.
static void* DL_Open(const char* name) {
	return SDL_LoadObject(name);
}
static void DL_Close(void* handle) {
	SDL_UnloadObject((SDL_SharedObject*)handle);
}
static void* DL_LoadFunction(void* handle, const char* name) {
	return (void*)SDL_LoadFunction((SDL_SharedObject*)handle, name);
}
// mimics dlerror(): returns and clears the last error, NULL if none
static const char* DL_GetError() {
	static char last[512];
	const char* err = SDL_GetError();
	if (!err || !*err)
		return NULL;
	strncpy(last, err, sizeof(last) - 1);
	last[sizeof(last) - 1] = 0;
	SDL_ClearError();
	return last;
}

static bool traditionalChineseActive = false;

bool gos_TraditionalChineseActive()
{
    return traditionalChineseActive;
}

HSTRRES __stdcall gos_OpenResourceDLL(char const* FileName, const char** strings, int num, const char* language)
{
    const char *error;
    void *module;
    get_string_by_id_fptr fptr;

	DL_GetError();

    /* A bare file name is looked up next to the executable (or in the .app
       bundle's resources), then in the current (game) directory. */
    module = NULL;
    if (!strchr(FileName, '/') && !strchr(FileName, '\\')) {
        if (const char* base = SDL_GetBasePath()) {
            char path[1024];
            snprintf(path, sizeof(path), "%s%s", base, FileName);
            module = DL_Open(path);
        }
        if (!module) {
            char path[1024];
            snprintf(path, sizeof(path), "./%s", FileName);
            module = DL_Open(path);
        }
    } else {
        module = DL_Open(FileName);
    }
    gosASSERT(module);
    if (!module) {
        fprintf(stderr, "Couldn't open resourse dll: %s\n", DL_GetError());
        return NULL;
    }

	DL_GetError();    /* Clear any existing error */

    /* Get symbol */
    fptr = (get_string_by_id_fptr)DL_LoadFunction(module, "getStringById");
    if ((error = DL_GetError())) {
        fprintf(stderr, "Couldn't find hello: %s\n", error);
        return NULL;
    }

    init_string_resources_fptr init_fptr;
    init_fptr = (init_string_resources_fptr)DL_LoadFunction(module, "initStringResources");
    if ((error = DL_GetError())) {
        fprintf(stderr, "Couldn't find hello: %s\n", error);
        return NULL;
    }
    if (!language)
        language = getenv("MC2_LANGUAGE");
    auto initLocalized = reinterpret_cast<void (*)(const char*)>(
        DL_LoadFunction(module, "initStringResourcesForLanguage"));
    if (!initLocalized)
        language = getenv("MC2_LANGUAGE");
    traditionalChineseActive = language && strcmp(language, "zh-TW") == 0;

    if (initLocalized)
        initLocalized(traditionalChineseActive ? "zh-TW" : "en");
    else {
        // An older resource library still supports its original initializer.
        DL_GetError();
        init_fptr();
    }

    gos_StringRes* pstrres = new gos_StringRes();
    pstrres->getStringByIdFptr = fptr;
    pstrres->module = module;

    /* deprecated */
    pstrres->strings = NULL;
    pstrres->num_strings = 0;

    return pstrres;
}
void __stdcall gos_CloseResourceDLL(HSTRRES handle)
{
    gos_StringRes* pstrres = (gos_StringRes*)handle;
    gosASSERT(pstrres && pstrres->module);

    free_string_resources_fptr free_fptr;
    DL_GetError();

    const char *error;
    free_fptr = (free_string_resources_fptr)DL_LoadFunction(pstrres->module, "freeStringResources");
    if ((error = DL_GetError())) {
        fprintf(stderr, "Couldn't find hello: %s\n", error);
    } else {
        free_fptr();
    }

    DL_Close(pstrres->module);
    delete pstrres;
}

const char* __stdcall gos_GetResourceString(HSTRRES handle, DWORD id)
{
    static const char* dummy_string_res = "missing string res";
    gosASSERT(handle);
    gos_StringRes* pstrres = (gos_StringRes*)(handle);
    const char* str = pstrres->getStringByIdFptr(id);

    if(NULL == str) {
        fprintf(stderr, "Requested string id: %d not found, return dummy string\n", id);
        return dummy_string_res;
    }
    return str;
}
