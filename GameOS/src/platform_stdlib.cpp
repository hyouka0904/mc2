#ifndef PLATFORM_WINDOWS
#include "platform_stdlib.h"

#include <libgen.h>
#include <string.h>
#include <stdlib.h> // free
#include <assert.h> 
#include <stdint.h>

// or 
//#define _GNU_SOURCE
//#include <string.h>

void _splitpath (const char* path, char* drive, char* dir, char* fname, char* ext)
{
    char* copy_base = strdup(path);
    // switch \ to /
    size_t len = strlen(copy_base);
    for(size_t i=0; i<len; ++i) {
        if(copy_base[i] == '\\')
            copy_base[i] = '/';
    }
    char* copy_dir = strdup(copy_base);
    char* d = dirname(copy_dir);
    char* b = basename(copy_base);

    if(dir) {
        strcpy(dir, d);
        strcat(dir, "/"); // looks like this is what Windows version does
    }
    if(fname) {
        strcpy(fname, b);
        char* dot = strrchr(fname, '.');
        if(dot) {
            *dot='\0';
        }
    }

    if(drive) {
        strcpy(drive, "");
    }

    if(ext) {
        char* dot = strrchr(b, '.');
        if(dot) {
            strcpy(ext, dot); // including '.'
        } else {
            strcpy(ext, "");
        }
    }

    free(copy_dir);
    free(copy_base);

}

#endif // PLATFORM_WINDOWS
