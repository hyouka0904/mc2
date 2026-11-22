#ifndef PLATFORM_MBSTRIG_H
#define PLATFORM_MBSTRIG_H

#ifndef PLATFORM_WINDOWS
// Multibyte string helpers for the single-byte character sets the game data
// uses (no double-byte lead bytes).
inline int isleadbyte(int) { return 0; }
inline unsigned char* _mbsinc(const unsigned char* current)
{
    return const_cast<unsigned char*>(current + 1);
}
inline unsigned char* _mbsdec(const unsigned char* start, const unsigned char* current)
{
    return start >= current ? 0 : const_cast<unsigned char*>(current - 1);
}
#else
#include <mbstring.h>
#endif

#endif // PLATFORM_MBSTRIG_H
