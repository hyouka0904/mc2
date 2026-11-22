#ifndef PLATFORM_WINDOWS

#include <stdlib.h>
#include <wchar.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include "platform_windows.h"

#define _In_
#define _Out_opt_ 
#define _In_opt_  


// Conversions use the C library's current locale; the code page argument is
// ignored. As in Win32, a source length of -1 means "null-terminated, and
// count the terminator", and a null or empty destination returns the size
// needed. Returns 0 on failure (unconvertible input or too small buffer; a
// too small buffer still receives the truncated, terminated string).
int WideCharToMultiByte(
  _In_      UINT    /*CodePage*/,
  _In_      DWORD   /*dwFlags*/,
  _In_      LPCWSTR src,
  _In_      int     srcLen,
  _Out_opt_ LPSTR   dst,
  _In_      int     dstLen,
  _In_opt_  LPCSTR  /*lpDefaultChar*/,
  _Out_opt_ LPBOOL  /*lpUsedDefaultChar*/
)
{
    const size_t n = srcLen < 0 ? wcslen(src) + 1 : (size_t)srcLen;
    const bool measure = !dst || dstLen <= 0;
    mbstate_t state = {};
    char tmp[MB_LEN_MAX];
    int out = 0;
    for (size_t i = 0; i < n; ++i) {
        const size_t k = wcrtomb(tmp, src[i], &state);
        if (k == (size_t)-1)
            return 0;
        if (!measure) {
            if (out + (int)k > dstLen) {
                dst[dstLen - 1] = '\0'; // keep a truncated string usable
                return 0;
            }
            memcpy(dst + out, tmp, k);
        }
        out += (int)k;
    }
    return out;
}

int MultiByteToWideChar(
        _In_      UINT   /*CodePage*/,
        _In_      DWORD  /*dwFlags*/,
        _In_      LPCSTR src,
        _In_      int    srcLen,
        _Out_opt_ LPWSTR dst,
        _In_      int    dstLen
        )
{
    const size_t n = srcLen < 0 ? strlen(src) + 1 : (size_t)srcLen;
    const bool measure = !dst || dstLen <= 0;
    mbstate_t state = {};
    int out = 0;
    size_t i = 0;
    while (i < n) {
        wchar_t wc;
        size_t k = mbrtowc(&wc, src + i, n - i, &state);
        if (k == (size_t)-1 || k == (size_t)-2)
            return 0;
        if (k == 0) // embedded or final '\0'
            k = 1;
        if (!measure) {
            if (out >= dstLen) {
                dst[dstLen - 1] = L'\0'; // keep a truncated string usable
                return 0;
            }
            dst[out] = wc;
        }
        ++out;
        i += k;
    }
    return out;
}

BOOL GetStringTypeEx(
        LCID    Locale,
        DWORD   dwInfoType,
        LPCTSTR lpSrcStr,
        int     cchSrc,
        LPWORD  lpCharType
        ) {

    if(dwInfoType == CT_CTYPE1)
    {
        for(int i=0;i < cchSrc; ++i)
        {
            lpCharType[i] = 0;
            if(isdigit(lpSrcStr[i])) {
                lpCharType[i] = C1_DIGIT;
                if(isxdigit(lpSrcStr[i])) {
                    lpCharType[i] |= C1_XDIGIT;
                }
            } else if(isblank(lpSrcStr[i])) {
                if(isspace(lpSrcStr[i])) {
                    lpCharType[i] |= C1_SPACE;
                }
            } else if(isalpha(lpSrcStr[i])) {
                lpCharType[i] = C1_ALPHA;
                if(islower(lpSrcStr[i])) {
                    lpCharType[i] |= C1_LOWER;
                } else if(isupper(lpSrcStr[i])) {
                    lpCharType[i] |= C1_UPPER;
                }
            }

            if(ispunct(lpSrcStr[i])) {
                lpCharType[i] |= C1_PUNCT;
            }
            if(iscntrl(lpSrcStr[i])) {
                lpCharType[i] |= C1_CNTRL;
            }
        }            
        return true;
    } else {
        return false;
    }
}

BOOL IsDBCSLeadByte(BYTE TestChar) {
    // sebi: NB!
    return FALSE;
}

#endif // PLATFORM_WINDOWS
